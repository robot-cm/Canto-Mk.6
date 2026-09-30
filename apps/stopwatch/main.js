// Canto Mk.6 秒表（计时器）— 计时器.html 移植 + 动效 v1 + Round 33e 小修
//
// 精度 v2：运行不再累加（elapsed += 100 在 LVGL tick 周期不准时必然漂移）。
// 改为 ms 级绝对时间戳差值：elapsedMs() = accum + (nowMs() - runStartMs)，
// nowMs = dateToSec(...)*1000 + eos.time.getNow().ms（RTC 秒 + tick 内插 ms）。
// 暂停落盘 accum，运行落盘 runStartMs（RTC 绝对毫秒）→ 退出期间 RTC 继续走，
// 重进时 nowMs - runStartMs 自动补上退出时长，无需再存整秒。
// 后台：manifest background=true（切后台计时继续）
// 动效 v1：① 状态点呼吸 800ms ping-pong ② 按钮按压 Opa60→28 120ms（scale 降级）
//          ③ 计次行 fadeIn 120ms ④ 状态切换 fadeIn 120ms ⑤ 重置 Opa 250→80→250
// 性能红线：同屏 anim ≤6；周期 ≥30ms
// 红线：无 arc / 无 border 三件套 / radius<54 / 无 flex / opa 裸数字 / hex 数字

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Stopwatch");

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hex(v) { return lv.color.hex(v); }

// ===================== 图标（drawIcon：全块 ≥8×8；<8px 两维对象 LVGL 不渲染） =====================
// 20×20 图标：play两台阶三角(18×10+9×10) / pause两根8×16 gap4
// list两根16×8 gap4 / reset C 形环(14×8+8×12+14×8) + 8×8 箭头
function drawIcon(parent, type, color, bx, by) {
    function px(w, h, x, y) {                    // 像素块（最小 8×8，直挂 parent 绝对坐标）
        var p = new lv.obj(parent);
        p.setSize(w, h);
        p.setPos(bx + x, by + y);
        p.setStyleBgOpa(255, 0);
        p.setStyleBgColor(hex(color), 0);
        p.setStyleRadius(0, 0);
        p.removeFlag(lv.OBJ_FLAG_CLICKABLE);
        p.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        return p;
    }
    if (type === "play") {                       // 右指三角（两台阶）
        px(18, 10, 0, 0); px(9, 10, 0, 18);
    } else if (type === "pause") {               // 两根 8×16 gap4
        px(8, 16, 0, 4); px(8, 16, 16, 4);
    } else if (type === "list") {                // 两根 16×8 gap4
        px(16, 8, 2, 0); px(16, 8, 2, 18);
    } else if (type === "reset") {               // C 形 3/4 环 + 箭头凸点
        px(14, 8, 3, 0);                         // 上横
        px(8, 12, 14, 4);                        // 右竖
        px(14, 8, 3, 16);                        // 下横
        px(8, 8, 4, 8);                          // 箭头凸点
    }
}

var DIAL_COLOR = 0x12121A;
var ACCENT = 0x4A90D9;           // 运行态点/进度（用户清单：运行 0x4A90D9）
var PAUSE_COLOR = 0xE5A34D;      // 暂停态点
var IDLE_COLOR = 0x8A8F98;       // 待命态点
var WHITE = 0xFFFFFF;

// ===================== dial =====================
var dial = new lv.obj(view);
dial.setSize(240, 240);
dial.setPos(0, 0);
dial.setStyleRadius(0, 0);
dial.setStyleBgOpa(255, 0);
dial.setStyleBgColor(hex(DIAL_COLOR), 0);
dial.setStylePadAll(0, 0);
dial.setStyleBorderWidth(0, 0);
dial.removeFlag(lv.OBJ_FLAG_CLICKABLE);
dial.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== 状态行（点 8×8 + 文字，整体居中） =====================
var statusDot = new lv.obj(dial);
statusDot.setSize(8, 8);
statusDot.setStyleRadius(4, 0);
statusDot.setStyleBgColor(hex(IDLE_COLOR), 0);
statusDot.setStyleBgOpa(255, 0);
statusDot.removeFlag(lv.OBJ_FLAG_CLICKABLE);
statusDot.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var statusTxt = new lv.label(dial);
statusTxt.setStyleTextColor(WHITE, 0);
statusTxt.setStyleTextOpa(160, 0);
statusTxt.setText("READY");

function layoutStatusRow() {
    try { dial.updateLayout(); } catch (e) {}
    var tw = statusTxt.getWidth();
    var total = 8 + 6 + tw;
    var x0 = Math.floor((240 - total) / 2);
    statusDot.setPos(x0, 46);
    statusTxt.setPos(x0 + 14, 36);
}

// ===================== 时间双 label（同 font26 同 y 同基线紧贴） =====================
// 主 "HH:MM:SS" + 百分位 ".X"（1 位，用户"删最后一位"）；都不 setSize → getWidth=文字宽
var timeTxt = new lv.label(dial);
timeTxt.setStyleTextColor(WHITE, 0);
timeTxt.setStyleTextOpa(250, 0);
timeTxt.setFontSize(26);
timeTxt.setStyleTextLetterSpace(2, 0);

var millisTxt = new lv.label(dial);
millisTxt.setStyleTextColor(WHITE, 0);
millisTxt.setStyleTextOpa(160, 0);
millisTxt.setFontSize(26);

function layoutTimeRow() {
    try { dial.updateLayout(); } catch (e) {}
    var w1 = timeTxt.getWidth();               // 文字宽（label 未 setSize）
    var w2 = millisTxt.getWidth();
    var x0 = Math.floor((240 - (w1 + w2 + 2)) / 2);
    timeTxt.setPos(x0, 60);
    millisTxt.setPos(x0 + w1 + 2, 60);
}


// ===================== 按钮行（图标胶囊 w48 h28 @y180 x=36/96/156，gap12 居中，向内靠拢） =====================
var BTN_X = [36, 96, 156];
var BTN_W = 48, BTN_H = 28;
var btnRefs = {};

function styleBtn(b) {                        // 统一胶囊样式（亮态 Opa36，暗底上有形）
    b.setStyleBgOpa(36, 0);
    b.setStyleBgColor(hex(WHITE), 0);
}

function makeBtn(x, label, key) {
    var b = new lv.button(dial);
    b.setSize(BTN_W, BTN_H);
    b.setPos(x, 180);
    b.setStyleRadius(999, 0);                  // 胶囊（LVGL clamp 到 14）
    b.setStyleBgOpa(36, 0);                    // 亮态 Opa36（暗底上有形）
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setExtClickArea(6);                      // 扩大点击区防误触
    var l = new lv.label(b);
    l.setSize(BTN_W, BTN_H);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(12);                         // 英文 5 字符适配 54px 胶囊
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(250, 0);                 // 白 Opa250
    l.setText(label);
    l.align(lv.ALIGN_CENTER, 0, 0);            // 相对父胶囊居中（删 setPos 手摆）
    b.addEventCb(function () {
        pressFx(b);                            // 动效2：按压反馈
        btnHandler(key);
    }, lv.EVENT_PRESSED, null);
    btnRefs[key] = { btn: b, lbl: l };
    return b;
}
makeBtn(BTN_X[0], "START", "start");
makeBtn(BTN_X[1], "LAP", "lap");
makeBtn(BTN_X[2], "RESET", "reset");

// ===================== 计次列表（y96 h80 w180 居中；白Opa8 r12 PadAll4，内容超高可滚动） =====================
var MAX_LAP_ROWS = 40;
var lapsBox = new lv.obj(dial);
lapsBox.setSize(180, 80);
lapsBox.setPos(30, 96);
lapsBox.setStyleBgOpa(12, 0);                   // 白 Opa12 玻璃（暗底上有形）
lapsBox.setStyleRadius(12, 0);
lapsBox.setStylePadAll(4, 0);
lapsBox.setStyleBorderWidth(0, 0);
lapsBox.addFlag(lv.OBJ_FLAG_SCROLLABLE);
lapsBox.setScrollbarMode(0);

var lapsEmpty = new lv.label(lapsBox);
lapsEmpty.setSize(180, 14);
lapsEmpty.setPos(0, 33);                        // 盒内垂直居中（新盒高 80）
lapsEmpty.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
lapsEmpty.setFontSize(10);                      // → 22px（三档）
lapsEmpty.setStyleTextColor(WHITE, 0);
lapsEmpty.setStyleTextOpa(90, 0);
lapsEmpty.setText("— No Laps —");

var lapRows = [];
for (var li = 0; li < MAX_LAP_ROWS; li++) {
    var lr = new lv.label(lapsBox);
    lr.setSize(180, 13);
    lr.setPos(0, 4 + li * 13);                  // 行高 13（PadAll4 内；40 行内容超高 → 盒内可滚动）
    lr.setFontSize(10);
    lr.setStyleTextColor(WHITE, 0);
    lr.setStyleTextOpa(170, 0);
    lr.setStyleTextLetterSpace(1, 0);           // 数字间隔大一点（用户反馈）
    lr.setText("");
    lr.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    lr.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    lapRows.push(lr);
}

// ===================== 状态与逻辑（后台续计时：eos.config 持久化 + 毫秒时间戳补差） =====================
// 退出期间不计时（JS 程序销毁）→ 运行态持久化 runStartMs（RTC 绝对毫秒），
// 重进 elapsedMs() = accum + (nowMs() - runStartMs) 自动补上退出时长。laps 也持久化。
function dateToSec(y, m, d, h, mi, s) {          // days-from-civil（精确，跨月跨年正确）
    var yy = y - (m <= 2 ? 1 : 0);
    var era = Math.floor((yy >= 0 ? yy : yy - 399) / 400);
    var yoe = yy - era * 400;
    var mp = m + (m > 2 ? -3 : 9);
    var doy = Math.floor((153 * mp + 2) / 5) + d - 1;
    var doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
    return ((era * 146097 + doe - 719468) * 86400) + h * 3600 + mi * 60 + s;
}
function nowMs() {                               // ms 级绝对时间戳（RTC 秒 + tick 内插 ms）
    var t = eos.time.getNow();
    return dateToSec(t.year, t.month, t.day, t.hour, t.min, t.sec) * 1000 + (t.ms || 0);
}

var _savedRunning = false, _savedAccum = 0, _savedStart = 0, _savedLaps = [];
try { _savedRunning = eos.config.getBool("timer.running") === true; } catch (e) {}
try { _savedAccum = eos.config.getNumber("timer.accum") || 0; } catch (e) {}
try { _savedStart = eos.config.getNumber("timer.start") || 0; } catch (e) {}
try { var _ls = eos.config.getStr("timer.laps"); if (_ls) { _savedLaps = JSON.parse(_ls) || []; } } catch (e) {}

var running = _savedRunning;
var accum = _savedAccum;
var runStartMs = _savedStart;
if (running && runStartMs <= 0) {                // 旧版数据兼容：无 timer.start → 用旧整秒 sec 近似
    var _oldSec = 0;
    try { _oldSec = eos.config.getNumber("timer.sec") || 0; } catch (e) {}
    if (_oldSec > 0) { runStartMs = _oldSec * 1000; }
    else { running = false; accum = 0; }
}
var laps = _savedLaps;                           // 最新在前（恢复持久化计次）

function elapsedMs() {                           // 当前累计毫秒（运行中实时 = 暂停累计 + 本次运行时长）
    return accum + (running ? Math.max(0, nowMs() - runStartMs) : 0);
}

function saveState() {
    try { eos.config.setBool("app.background", running); } catch (e) {}   // 最优先独立 try：异常不影响
    try {
        eos.config.setNumber("timer.accum", accum);
        eos.config.setBool("timer.running", running);
        eos.config.setNumber("timer.start", running ? runStartMs : 0);
        eos.config.setStr("timer.laps", JSON.stringify(laps));
    } catch (e) {}
}

function fmtMs(ms) {
    var ts = Math.floor(ms / 1000);
    var h = Math.floor(ts / 3600);
    var m = Math.floor((ts % 3600) / 60);
    var s = ts % 60;
    return pad2(h) + ":" + pad2(m) + ":" + pad2(s);
}

function fmtLap(ms) {                            // 计次格式 "MM:SS.xx"（08.25 = 0分8.25秒）
    var ts = Math.floor(ms / 1000);
    var m = Math.floor(ts / 60);
    var s = ts % 60;
    return pad2(m) + ":" + pad2(s) + "." + pad2(Math.floor((ms % 1000) / 10));
}

function update() {
    var e = elapsedMs();
    timeTxt.setText(fmtMs(e));
    millisTxt.setText("." + Math.floor((e % 1000) / 100));   // 百分位 1 位 .0 .1 .2（每 100ms +1）
    layoutTimeRow();
    renderLaps();
}

function renderLaps() {
    if (laps.length === 0) {
        lapsEmpty.setText("— No Laps —");
        for (var i = 0; i < MAX_LAP_ROWS; i++) lapRows[i].setText("");
        return;
    }
    lapsEmpty.setText("");
    for (var j = 0; j < MAX_LAP_ROWS; j++) {
        if (j < laps.length) {
            var txt = pad2(laps.length - j) + " · " + fmtLap(laps[j]);   // "07 · 00:08.25"
            if (lapRows[j].getText() !== txt) {
                lapRows[j].setText(txt);
                if (j === 0) rowFx(lapRows[j]);    // 动效3：新行 fadeIn
            }
        } else {
            lapRows[j].setText("");
        }
    }
}

function btnHandler(key) {
    if (key === "start") {
        if (running) {
            accum += Math.max(0, nowMs() - runStartMs);   // 暂停：本次运行时长并入累计（ms 精确）
            runStartMs = 0; running = false;
            btnRefs.start.lbl.setText("START"); styleBtn(btnRefs.start.btn); setStatus("PAUSED", PAUSE_COLOR); setBreath(false);
            update(); saveState();
        } else {
            runStartMs = nowMs(); running = true;         // 开始：记录 RTC 绝对毫秒起点
            btnRefs.start.lbl.setText("PAUSE"); styleBtn(btnRefs.start.btn); setStatus("RUNNING", ACCENT); setBreath(true);
            saveState();
        }
    } else if (key === "lap") {
        var e = elapsedMs();
        if (e > 0) {
            laps.unshift(e); renderLaps(); saveState();              // 计次即落盘
            try { lapsBox.scrollToY(0, 0); } catch (err2) {}          // 新计次滚回顶部查看最新
        }
    } else if (key === "reset") {
        running = false; accum = 0; runStartMs = 0; laps = [];
        btnRefs.start.lbl.setText("START"); styleBtn(btnRefs.start.btn);
        setStatus("READY", IDLE_COLOR); setBreath(false);
        timeTxt.setText("00:00:00");
        millisTxt.setText(".0");
        layoutTimeRow();
        renderLaps();
        resetFx();                                // 动效5：时间闪烁
        saveState();                              // 重置即落盘（后台不再续）
    }
}

// ===================== 状态（动效4：fadeIn 120ms） =====================
function setStatus(text, color) {
    // 动效4：crossfade — fadeOut(80) → setText → fadeIn(120)
    var a1 = new lv.anim();
    a1.init(); a1.setVar(statusTxt);
    a1.setValues(160, 60); a1.setDuration(80);
    a1.setCustomExecCb(function (an, v) { statusTxt.setStyleTextOpa(Math.round(v), 0); });
    a1.setCompletedCb(function () {
        statusTxt.setText(text);
        layoutStatusRow();
        var a2 = new lv.anim();
        a2.init(); a2.setVar(statusTxt);
        a2.setValues(60, 160); a2.setDuration(120);
        a2.setCustomExecCb(function (an, v) { statusTxt.setStyleTextOpa(Math.round(v), 0); });
    });
    statusDot.setStyleBgColor(hex(color), 0);
    statusDot.setStyleBgOpa(255, 0);
    // 使能态：待命时 计次/重置 淡（fill Opa16 + 文字 Opa90）；运行/暂停恢复 36/250
    var idle = (text === "READY");
    btnRefs.lap.btn.setStyleBgOpa(idle ? 16 : 36, 0);
    btnRefs.lap.lbl.setStyleTextOpa(idle ? 90 : 250, 0);
    btnRefs.reset.btn.setStyleBgOpa(idle ? 16 : 36, 0);
    btnRefs.reset.lbl.setStyleTextOpa(idle ? 90 : 250, 0);
}

// ===================== 动效 =====================
// 动效1：状态点呼吸（运行中 90↔255 800ms ping-pong；停止时强制常亮）
var breathAnim = null;
function setBreath(on) {
    if (on && !breathAnim) {
        var a = new lv.anim();
        a.init(); a.setVar(statusDot);
        a.setValues(90, 255); a.setDuration(400); a.setPlaybackTime(400); a.setRepeatCount(65535);
        a.setPathCb(4);                           // EASE_IN_OUT
        a.setCustomExecCb(function (an, v) {
            if (running) statusDot.setStyleBgOpa(Math.round(v), 0);
            else statusDot.setStyleBgOpa(255, 0);
        });
        breathAnim = a;
    } else if (!on) {
        breathAnim = null;
        statusDot.setStyleBgOpa(255, 0);
    }
}

// 动效2：按钮按压 scale 232→256 120ms（RELEASED 不触发 → 一次性 anim 恢复；胶囊复现 bug 则降级 Opa）
function pressFx(b) {
    b.setStyleTransformScale(232, 0);
    var a = new lv.anim();
    a.init(); a.setVar(b);
    a.setValues(232, 256); a.setDuration(120);
    a.setCustomExecCb(function (an, v) { b.setStyleTransformScale(Math.round(v), 0); });
}

// 动效3：计次行 fadeIn(120) + TranslateY -6→0（全段 try：anim/exec 异常时行直接可见，不吞首行）
function rowFx(lbl) {
    try { lbl.setStyleTextOpa(0, 0); } catch (e) {}
    var a = new lv.anim();
    a.init(); a.setVar(lbl);
    a.setValues(0, 170); a.setDuration(120);
    a.setCustomExecCb(function (an, v) {
        try { lbl.setStyleTextOpa(Math.round(v), 0); } catch (e) {}
    });
    try {
        lbl.setStyleTranslateY(-6, 0);
        var b2 = new lv.anim();
        b2.init(); b2.setVar(lbl);
        b2.setValues(-6, 0); b2.setDuration(120);
        b2.setCustomExecCb(function (an, v) {
            try { lbl.setStyleTranslateY(Math.round(v), 0); } catch (e) {}
        });
    } catch (e) {}
    // 兜底：150ms 后强制行可见（anim 失败也不吞首行）
    var fb = new lv.timer(function () {
        try { lbl.setStyleTextOpa(170, 0); lbl.setStyleTranslateY(0, 0); } catch (e) {}
    }, 150, null);
}

// 动效5：重置反馈 时间双 label Opa 250→80→250 200ms
function resetFx() {
    function flash(lbl) {
        var a = new lv.anim();
        a.init(); a.setVar(lbl);
        a.setValues(250, 80); a.setDuration(100); a.setPlaybackTime(100); a.setRepeatCount(1);
        a.setCustomExecCb(function (an, v) { lbl.setStyleTextOpa(Math.round(v), 0); });
    }
    flash(timeTxt);
    flash(millisTxt);
}

// ===================== tick（100ms；每秒落盘一次供后台续计时） =====================
var _saveCnt = 0;
var tick = new lv.timer(function () {
    if (running) {
        update();                                // 显示基于时间戳实时计算，不再累加
        _saveCnt++;
        if (_saveCnt >= 10) { _saveCnt = 0; saveState(); }   // 每 1s 落盘（运行态 start/accum 不变）
    }
}, 100, null);
tick.setRepeatCount(-1);

if (running) {                                 // 上次运行中 → 恢复续计时 UI（elapsedMs 自动补退出时长）
    btnRefs.start.lbl.setText("PAUSE");
    setStatus("RUNNING", ACCENT); setBreath(true);
} else if (accum > 0) {                        // 暂停后退出 → 恢复暂停态
    btnRefs.start.lbl.setText("START");
    setStatus("PAUSED", PAUSE_COLOR);
} else {
    setStatus("READY", IDLE_COLOR);
}
saveState();          // 启动即落盘初始标志（待命/已暂停 → app.background=false → 退出不挂后台标）
layoutTimeRow();
update();

// ===== audit（协议第六节）=====
function audit(o, d, name) {
    var tag = name || "obj";
    if (typeof o.getText === "function") { try { tag += "<" + o.getText() + ">"; } catch (e) {} }
    var c = o.getCoords();
    var indent = "";
    for (var k = 0; k < d; k++) { indent += "  "; }
    var r = "?", bo = "?", hid = "?";
    try { r = o.getStyleRadius(0); } catch (e) {}
    try { bo = o.getStyleBgOpa(0); } catch (e) {}
    try { hid = o.hasFlag(lv.OBJ_FLAG_HIDDEN); } catch (e) {}
    eos.console.log(indent + tag +
        " xywh=" + c.x1 + "," + c.y1 + "," + (c.x2 - c.x1) + "," + (c.y2 - c.y1) +
        " r=" + r + " bgOpa=" + bo + " hid=" + hid);
    var n = 0;
    try { n = o.getChildCount(); } catch (e) {}
    for (var i = 0; i < n; i++) {
        var ch = null;
        try { ch = o.getChild(i); } catch (e) {}
        if (ch !== null && ch !== undefined) { audit(ch, d + 1, "#" + i); }
    }
}
try { view.updateLayout(); } catch (e) {}
try { dial.updateLayout(); } catch (e) {}
try { audit(dial, 0, "dial"); }
catch (err) { eos.console.log("AUDIT FAILED: " + err); }

eos.console.log("[timer] 秒表动效版完成");
