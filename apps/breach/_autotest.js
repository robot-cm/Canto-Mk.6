// _autotest.js — headless 门禁：mock lv/cos 加载 main.js，
// 驱动 Boot 转场 + 自动玩 20 整局（含三转场 + retry + 超时），任何 Jerry error = fail。
// 另做"被调用函数 vs 已定义"静态扫描（secondary net）。
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = path.join(__dirname, 'main.js');

// ---------- mock lv / cos ----------
const timers = [];
const breachLogs = [];

// 严格 API 参数个数表（堵"mock 松真引擎严"）：参数不符直接 throw，复现真引擎 Invalid argument count。
const ARG_SPEC = {
    setSize: 2, setPos: 2, setWidth: 1,
    setStyleRadius: 2, setStyleBgOpa: 2, setStyleBgColor: 2,
    setStyleBorderWidth: 2, setStyleBorderColor: 2, setStyleBorderOpa: 2,
    setStyleTextOpa: 2, setStylePadAll: 2, setStyleTextColor: 2,
    setStyleTextLetterSpace: 2, setStyleTextAlign: 2, setFontSize: 1,
    setText: 1, setScrollbarMode: 1, setLongMode: 1,
    removeFlag: 1, addFlag: 1, clearFlag: 1, hasFlag: 1,
    getChild: 1, getChildCount: 0, getText: 0, getCoords: 0, delete: 0, addEventCb: 3,
};
function mkStub(parent) {
    const s = { _text: '', _parent: parent || null, _x: 0, _y: 0, _w: 0, _h: 0 };
    // 严格包装：参数个数必须等于 ARG_SPEC，否则抛错（与真引擎一致）
    for (const name in ARG_SPEC) {
        const n = ARG_SPEC[name];
        s[name] = function (...args) {
            if (args.length !== n) throw new Error('SNI ' + name + ' expects ' + n + ' args, got ' + args.length + ' (mock-strict)');
            return s;
        };
    }
    // 状态型 setter 仍需记录真实值（供 getCoords / getText）
    s.setSize = function (w, h) { if (arguments.length !== 2) throw new Error('SNI setSize expects 2 args, got ' + arguments.length); this._w = w; this._h = h; return s; };
    s.setPos = function (x, y) { if (arguments.length !== 2) throw new Error('SNI setPos expects 2 args, got ' + arguments.length); this._x = x; this._y = y; return s; };
    s.setText = function (t) { if (arguments.length !== 1) throw new Error('SNI setText expects 1 arg'); this._text = t; return s; };
    s.getText = function () { return this._text; };
    s.getCoords = function () {
        let x = this._x, y = this._y, p = this._parent;
        while (p) { x += (p._x || 0); y += (p._y || 0); p = p._parent; }
        return { x1: x, y1: y, x2: x + this._w - 1, y2: y + this._h - 1 };
    };
    s.getChild = function () { return mkStub(s); };
    return s;
}
function mkTimer(cb) {
    const t = {
        cb, alive: true, rep: 0, autodel: false,
        setRepeatCount(n) { this.rep = n; return this; },
        setAutoDelete(b) { this.autodel = b; return this; },
        delete() { this.alive = false; const i = timers.indexOf(this); if (i >= 0) timers.splice(i, 1); },
    };
    timers.push(t);
    return t;
}
const lv = {
    obj: function (p) { return mkStub(p); },
    label: function (p) { return mkStub(p); },
    timer: function (cb) { return mkTimer(cb); },
    color: { hex: function (v) { return v; } },
    OBJ_FLAG_HIDDEN: 1, OBJ_FLAG_SCROLLABLE: 16, OBJ_FLAG_CLICKABLE: 2,
    EVENT_PRESSED: 1, EVENT_RELEASED: 2,
    TEXT_ALIGN_CENTER: 2, TEXT_ALIGN_LEFT: 0,
    LABEL_LONG_CLIP: 4, LABEL_LONG_WRAP: 0,
};
const cos = {
    console: { log: function (msg) { if (typeof msg === 'string' && msg.indexOf('[breach') === 0) breachLogs.push(msg); } },
    activity: {
        current() { return mkStub(); },
        getView() { return mkStub(); },
        setTitle() {}, setAppHeaderVisible() {},
    },
    time: { getNow() { return { year: 2026, month: 8, day: 18, hour: 11, min: 0, sec: 0, ms: 0, day_of_week: 2 }; } },
};

// ---------- run main.js ----------
const ctx = { lv, cos, Math, String, Number, Object, Array, JSON, Boolean, console, parseInt, parseFloat, isNaN, parseInt, undefined };
vm.createContext(ctx);
let loadErr = null;
try {
    vm.runInContext(fs.readFileSync(SRC, 'utf8'), ctx, { filename: 'main.js' });
} catch (e) {
    loadErr = e;
}
if (loadErr) { console.log('STACK:\n' + (loadErr.stack || loadErr.message)); }

// ---------- drive Boot timers (pump until empty or cap) ----------
let pumpErr = null;
try {
    for (let it = 0; it < 200 && timers.length > 0; it++) {
        const snap = timers.slice();
        for (const t of snap) { if (t.alive) { try { t.cb(); } catch (e) { pumpErr = e; } } }
    }
} catch (e) { pumpErr = e; }

const R = ctx.R;
const bootOk = !!R && typeof R.autotest === 'function';
let at = null, atErr = null;

// ---------- layout overlap audit (getCoords 实测盒 + 相交断言) ----------
let overlapOk = false;
try {
    if (ctx.auditLayout) ctx.auditLayout();   // 显式触发（Boot 调度器可能已在 pump 中跑过）
    overlapOk = !!(R && R.overlapOk);
} catch (e) { overlapOk = false; atErr = e; }

// ---------- auto-play 20 games ----------
if (bootOk) {
    try { at = R.autotest(20); } catch (e) { atErr = e; }
}

// ---------- 目标序列数 + 文本诊断（堵 t2 横线：序列数 < 3 → 占位符） ----------
let seqCountOk = false, targetTextOk = true;
const targetTexts = [];
for (const ln of breachLogs) {
    const mm = /^\[breach-target-text\] t\d+="(.*)"$/.exec(ln);
    if (mm) targetTexts.push(mm[1]);
}
const hexPat = /^[0-9A-F]{2}( [0-9A-F]{2})*$/;
for (const tx of targetTexts) {
    if (tx === '-- -- --' || !hexPat.test(tx)) { targetTextOk = false; break; }
}
try { seqCountOk = (R && R.debugSeqCount() === 3); } catch (e) { seqCountOk = false; }

// ---------- static scan: called globals vs declared ----------
let src = fs.readFileSync(SRC, 'utf8');
src = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, ''); // strip comments (avoid min( / cb( false positives)
const declared = new Set();
let m;
const reFn = /function\s+([A-Za-z_$][\w$]*)\s*\(/g;
while ((m = reFn.exec(src))) declared.add(m[1]);
const reVar = /(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=/g;
while ((m = reVar.exec(src))) declared.add(m[1]);
// method calls (obj.foo() ) excluded by requiring no preceding dot
const called = new Set();
const reCall = /(^|[^.\w$])([A-Za-z_$][\w$]*)\s*\(/g;
while ((m = reCall.exec(src))) called.add(m[2]);
const builtins = new Set(['if', 'for', 'while', 'switch', 'catch', 'function', 'return', 'typeof', 'new', 'do', 'else', 'Math', 'String', 'Number', 'Object', 'Array', 'JSON', 'Boolean', 'parseInt', 'parseFloat', 'isNaN', 'setTimeout', 'clearTimeout', 'require', 'console', 'Date', 'Error', 'lv', 'cos', 'R', 'safeTimer', 'cb', 'e', 't', 'r', 'c', 'i', 'j', 'k', 's', 'n', 'p', 'g', 'm', 'L', 'st', 'res', 'seq', 'path', 'cell', 'guard', 'ps', 'idx', 'rc', 'lc', 'ci', 'free', 'pick', 'last', 'pc', 'cc', 'z', 'nf', 'ok', 'okk', 'same', 'tt', 'rng', 'gm', 'ti', 'nums', 'lit', 'full', 'o', 'b', 'l', 'tl', 'so', 'ft', 'tw', 'pt', 'ct', 'v', 'x', 'y', 'w', 'h', 'd', 'a', 'buf', 'set']);
const undefinedCalls = [];
for (const c of called) {
    if (builtins.has(c)) continue;
    if (declared.has(c)) continue;
    undefinedCalls.push(c);
}

// ---------- report ----------
const jerry = (R && R.jerryErrors) || 0;
const pass = !loadErr && !pumpErr && bootOk && !atErr && at && at.errors === 0 && jerry === 0 && overlapOk && seqCountOk && targetTextOk;
console.log('=== breach headless autotest ===');
console.log('loadError      :', loadErr ? (loadErr.message || String(loadErr)) : 'none');
console.log('bootPumpError  :', pumpErr ? (pumpErr.message || String(pumpErr)) : 'none');
console.log('autotestError  :', atErr ? (atErr.message || String(atErr)) : 'none');
console.log('overlapOk      :', overlapOk);
console.log('seqCountOk(=3) :', seqCountOk);
console.log('targetTextOk   :', targetTextOk, '(texts:', targetTexts.join(' | '), ')');
console.log('jerryErrors    :', jerry);
console.log('autotestResult :', at ? JSON.stringify(at) : 'NOT RUN');
console.log('undefinedCalls :', undefinedCalls.length ? undefinedCalls.join(', ') : '(none / clean)');
console.log('breachLogs     :');
for (const ln of breachLogs) console.log('  ' + ln);
console.log('VERDICT        :', pass ? 'PASS' : 'FAIL');
process.exit(pass ? 0 : 1);
