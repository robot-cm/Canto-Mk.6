// ElenixOS 笔记 — 自带键盘圆屏重排 P0.5 + P1 打字行为
// P0.5：弃用系统输入页，自带键盘。标题 statusbar / 工具栏 y32 / 文本卡 y56 / 键盘四行
//       红线：禁 flex / opa 裸数字 / hex 数字（hex() 包裹）/ 胶囊 r999 / label align CENTER /
//            文本卡 SCROLLABLE+scrollbar OFF / 行宽 ≤ min(chord顶,chord底)−16 / 逐行自验
// P1：打字行为。点键追加 / 删 删末 / 完成=eos.config 存草稿+状态闪"已存" / 取消=回滚 baseline
//     连点 100 键账本恒定不闪退。
// P2：草稿持久化（启动从 eos.config 恢复 notes.draft）+ 123 符号页（第二页数字/符号，ABC 切回）。
var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "笔记");

var BG = 0x12121A;
var WHITE = 0xFFFFFF;
var ACCENT = 0x4A90D9;
var GREY = 0x8A8F98;
var KEY_OPA = 16;      // 键常态 Opa16
var KEY_OPA_PRESS = 50; // 按压态（P1 行为用）

function hex(v) { return lv.color.hex(v); }

// 按压反馈：PRESSED 时抬高键底 opa，RELEASED 时回落。原键盘无任何按压视觉反馈，
// 真机上只有光标在动 -> 用户误判"键盘没反应"。本 fork EVENT_CLICKED 断开，故用
// PRESSED+RELEASED 配对实现瞬时高亮（与 Draw 用 EVENT_RELEASED 一致）。
function bindFeedback(obj, baseOpa) {
    obj.addEventCb(function () { obj.setStyleBgOpa(Math.min(baseOpa + 34, 255), 0); }, lv.EVENT_PRESSED, null);
    obj.addEventCb(function () { obj.setStyleBgOpa(baseOpa, 0); }, lv.EVENT_RELEASED, null);
}

function chord(y) { return Math.floor(2 * Math.sqrt(14400 - (y - 120) * (y - 120))); }
// 新红线：任意矩形行宽 ≤ min(chord(顶), chord(底)) − 16
function rowMax(yt, yb) { return Math.min(chord(yt), chord(yb)) - 16; }

var R = {};
R.root = new lv.obj(view);
R.root.setSize(240, 240);
R.root.setPos(0, 0);
R.root.setStyleRadius(0, 0);
R.root.setStyleBgOpa(255, 0);
R.root.setStyleBgColor(hex(BG), 0);
R.root.setStylePadAll(0, 0);
R.root.setStyleBorderWidth(0, 0);   /* 默认 border=2 会让子对象坐标整体偏移 2px */
R.root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
R.root.setScrollbarMode(0);

// ===================== 工具栏 y32 h20 =====================
// 取消 x48 w40（白 Opa16）/ 完成 x152 w40（强调填充）→ 跨 48..192 = 144 ≤ min(163,197)−16=147 ✓
function toolPill(x, text, accent) {
    var b = new lv.button(R.root);
    b.setSize(40, 20);
    b.setPos(x, 32);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(accent ? 255 : KEY_OPA, 0);
    b.setStyleBgColor(hex(accent ? ACCENT : WHITE), 0);
    b.setStyleBorderWidth(0, 0);
    b.setStylePadAll(0, 0);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    var l = new lv.label(b);
    l.setSize(40, 20);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(11);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setText(text);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    bindFeedback(b, accent ? 255 : KEY_OPA);
    return b;
}
var toolCancel = toolPill(48, "取消", false);
var toolDone   = toolPill(152, "完成", true);

// ===================== 文本卡 y56 h56 =====================
// x32 w176 ≤ min(203,239)−16=187 ✓
var card = new lv.obj(R.root);
card.setSize(176, 56);
card.setPos(32, 56);
card.setStyleRadius(8, 0);
card.setStyleBgOpa(8, 0);
card.setStyleBgColor(hex(WHITE), 0);
card.setStylePadAll(0, 0);
card.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// body 占卡内 y0..44（留底部 12px 给 status 行）
var body = new lv.label(card);
body.setSize(176, 44);
body.setPos(0, 0);
body.setStylePadAll(6, 0);
body.setStyleTextColor(hex(WHITE), 0);
body.setFontSize(12);
body.setText("");
body.setScrollbarMode(0);

// 占位符：无字时显示 Opa60（P1 打字时随内容显隐）
var placeholder = new lv.label(card);
placeholder.setSize(176, 44);
placeholder.setPos(0, 0);
placeholder.setStylePadAll(6, 0);
placeholder.setStyleTextColor(hex(GREY), 0);
placeholder.setFontSize(12);
placeholder.setText("输入备忘...");
placeholder.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// 光标 1×14 强调色（P1 跟随文末估算，卡内 body 区）
var caret = new lv.obj(card);
caret.setSize(1, 14);
caret.setPos(6, 6);
caret.setStyleRadius(0, 0);
caret.setStyleBgOpa(255, 0);
caret.setStyleBgColor(hex(ACCENT), 0);
caret.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// 状态行：卡内 y44..56（h12），文本居中，初空；完成/取消短暂显示反馈
var status = new lv.label(card);
status.setSize(176, 12);
status.setPos(0, 44);
status.setStylePadAll(0, 0);
status.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
status.setStyleTextColor(hex(GREY), 0);
status.setFontSize(11);
status.setText("");
status.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== 键盘四行（收边留 margin） =====================
function key(x, y, w, h, t, fs) {
    var k = new lv.obj(R.root);
    k.setSize(w, h);
    k.setPos(x, y);
    k.setStyleRadius(4, 0);
    k.setStyleBgOpa(KEY_OPA, 0);
    k.setStyleBgColor(hex(WHITE), 0);
    k.setStyleBorderWidth(0, 0);
    k.setStylePadAll(0, 0);
    k.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    k.addFlag(lv.OBJ_FLAG_CLICKABLE);   /* 叶子按键需可点击，否则 PRESSED 不派发（真机+headless 均收不到） */
    bindFeedback(k, KEY_OPA);
    var l = new lv.label(k);
    l.setSize(w, h);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(fs || 11);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setText(t);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return k;
}
// [字符, x, y, w, h, fontsize]
var KEYS = [
    /* 行1 y116 h26：10键 w19 gap1 x0=20 → 20..218 跨199 ≤ min(239,235)−16=219 ✓ */
    ["q",20,116,19,26],["w",40,116,19,26],["e",60,116,19,26],["r",80,116,19,26],
    ["t",100,116,19,26],["y",120,116,19,26],["u",140,116,19,26],["i",160,116,19,26],
    ["o",180,116,19,26],["p",200,116,19,26],
    /* 行2 y144 h26：9键 w19 gap1 x0=30 → 30..208 跨179 ≤ min(235,218)−16=202 ✓ */
    ["a",30,144,19,26],["s",50,144,19,26],["d",70,144,19,26],["f",90,144,19,26],
    ["g",110,144,19,26],["h",130,144,19,26],["j",150,144,19,26],["k",170,144,19,26],
    ["l",190,144,19,26],
    /* 行3 y172 h26：7键 w18 gap1 x0=34 → 34..165 + 删 w26 gap6 x=172..197 跨164 ≤ min(216,182)−16=166 ✓ */
    ["z",34,172,18,26],["x",53,172,18,26],["c",72,172,18,26],["v",91,172,18,26],
    ["b",110,172,18,26],["n",129,172,18,26],["m",148,172,18,26],
    ["删",172,172,26,26,10],
    /* 行4 y200 h24：[123]w25 空格w53 [.]w18 gap2 x0=64 → 64..163 跨100 ≤ min(178,119)−16=103 ✓ */
    ["123",64,200,25,24,10],[" ",91,200,53,24],[".",146,200,18,24]
];
var keys = [];
for (var i = 0; i < KEYS.length; i++)
    keys.push(key(KEYS[i][1], KEYS[i][2], KEYS[i][3], KEYS[i][4], KEYS[i][0], KEYS[i][5]));

// ===================== 符号页（P2 第二页） =====================
// 字母行1-3 + row4（切换/空格/句号）保持与字母页同几何；符号页覆写行1-3。
var KEYS_SYMBOLS = [
    /* 行1 y116 h26：10 数字，同字母行1 几何（x0=20 w19 gap1） */
    ["1",20,116,19,26],["2",40,116,19,26],["3",60,116,19,26],["4",80,116,19,26],
    ["5",100,116,19,26],["6",120,116,19,26],["7",140,116,19,26],["8",160,116,19,26],
    ["9",180,116,19,26],["0",200,116,19,26],
    /* 行2 y144 h26：10 符号，x0=20 同行1 */
    ["@",20,144,19,26],["#",40,144,19,26],["$",60,144,19,26],["%",80,144,19,26],
    ["&",100,144,19,26],["*",120,144,19,26],["-",140,144,19,26],["+",160,144,19,26],
    ["=",180,144,19,26],["/",200,144,19,26],
    /* 行3 y172 h26：7 符号 + 删，同字母行3 几何 */
    ["(",34,172,18,26],[")",53,172,18,26],["_",72,172,18,26],["<",91,172,18,26],
    [":",110,172,18,26],[";",129,172,18,26],['"',148,172,18,26],
    ["删",172,172,26,26,10]
];
var symKeys = [];
for (var i = 0; i < KEYS_SYMBOLS.length; i++)
    symKeys.push(key(KEYS_SYMBOLS[i][1], KEYS_SYMBOLS[i][2], KEYS_SYMBOLS[i][3], KEYS_SYMBOLS[i][4], KEYS_SYMBOLS[i][0], KEYS_SYMBOLS[i][5]));

var toggleKey = keys[keys.length - 3];   // row4 第1个 = 123/ABC 切换键
var currentPage = 0;                       // 0=字母页, 1=符号页

function applyPage() {
    var sym = (currentPage === 1);
    // 符号键：符号页显示，字母页隐藏
    for (var i = 0; i < symKeys.length; i++) {
        if (sym) symKeys[i].removeFlag(lv.OBJ_FLAG_HIDDEN);
        else    symKeys[i].addFlag(lv.OBJ_FLAG_HIDDEN);
    }
    // 字母行1-3 = keys 前 (len-3) 个；row4（切换/空格/句号）始终可见
    for (var i = 0; i < keys.length - 3; i++) {
        if (sym) keys[i].addFlag(lv.OBJ_FLAG_HIDDEN);
        else    keys[i].removeFlag(lv.OBJ_FLAG_HIDDEN);
    }
    // lv.obj 无 setText；切换键的文案在其子 label 上
    toggleKey.getChild(0).setText(sym ? "ABC" : "123");
}

// 符号键绑 PRESSED（与字母键同款：闭包捕获字符）
for (var i = 0; i < KEYS_SYMBOLS.length; i++) {
    (function (ch) {
        symKeys[i].addEventCb(function () { press(ch); }, lv.EVENT_PRESSED, null);
    })(KEYS_SYMBOLS[i][0]);
}

// ===================== P1 状态与行为 =====================
var textBuf = "";   // 当前草稿文本
var baseline = "";  // 进入/完成时的快照，取消回滚用

var PER_LINE = 22;  // body 区（宽164）约容字符数估算（font12 ≈ 7px/字）
var COL_W = 7;
var LINE_H = 14;

function refreshBody() {
    body.setText(textBuf);
    if (textBuf.length === 0) {
        placeholder.setText("输入备忘...");
    } else {
        placeholder.setText("");
    }
    // 光标估算跟随文末（卡内 body 区 y0..44）
    var line = Math.floor(textBuf.length / PER_LINE);
    var col = textBuf.length % PER_LINE;
    var cx = 6 + col * COL_W;
    var cy = 6 + line * LINE_H;
    if (cx > 168) cx = 168;
    if (cy > 30) cy = 30;   // caret 高14，body 区高44 → 上限30
    caret.setPos(cx, cy);
}

// 状态行"闪"：一次 timer 清空（LVGL timer 默认 repeat=0 → 一次后自动删）
function flashStatus(ms) {
    var t = new lv.timer(function () { status.setText(""); }, ms, null);
}

function press(ch) {
    if (ch === "删") {
        if (textBuf.length > 0)
            textBuf = textBuf.substring(0, textBuf.length - 1);
    } else if (ch === "123") {
        currentPage = (currentPage === 0) ? 1 : 0;   // 符号页 P2：123<->ABC 切换
        applyPage();
        return;
    } else {
        textBuf += ch;
    }
    status.setText("");   // 打字即清状态反馈
    refreshBody();
}

// 每个键绑 PRESSED（IIFE 闭包捕获字符，避免循环 var 共享）
for (var i = 0; i < KEYS.length; i++) {
    (function (ch) {
        keys[i].addEventCb(function () { press(ch); }, lv.EVENT_PRESSED, null);
    })(KEYS[i][0]);
}

// 完成：存草稿到 eos.config + 状态闪"已存"
toolDone.addEventCb(function () {
    eos.config.setStr("notes.draft", textBuf);
    baseline = textBuf;
    status.setText("已存");
    flashStatus(800);
}, lv.EVENT_PRESSED, null);

// 取消：回滚到 baseline
toolCancel.addEventCb(function () {
    textBuf = baseline;
    refreshBody();
    status.setText("已取消");
    flashStatus(800);
}, lv.EVENT_PRESSED, null);

// ===================== 草稿持久化 + 启动恢复（P2） =====================
// 启动时从 eos.config 读取上次保存的草稿并恢复；config 缺失/键不存在时返回
// undefined（不抛），首次打开为空草稿 + 占位符。try/catch 兜底极端读失败。
var draft = "";
try { draft = eos.config.getStr("notes.draft"); } catch (e) { draft = ""; }
if (typeof draft === "string" && draft.length > 0) {
    textBuf = draft;
    baseline = draft;   // 取消回滚到持久化草稿，而非空
}

applyPage();       // 初始字母页：隐藏符号键
refreshBody();     // 初始化：显示占位符 / 恢复草稿 + 光标首行

// ===================== audit 自验表 =====================
function audit(o, d, name) {
    var tag = name || "obj";
    if (typeof o.getText === "function") { try { tag += "<" + o.getText() + ">"; } catch (e) {} }
    var c = o.getCoords();
    eos.console.log(new Array(d + 1).join("  ") + tag + " xywh=" + c.x1 + "," + c.y1 + "," +
        (c.x2 - c.x1) + "," + (c.y2 - c.y1) +
        " r=" + o.getStyleRadius(0) + " opa=" + o.getStyleBgOpa(0) + " bw=" + o.getStyleBorderWidth(0));
    for (var i = 0; i < o.getChildCount(); i++) audit(o.getChild(i), d + 1, "#" + i);
}

// 弦宽自验表：逐行 min(chord顶,chord底)−16 vs 实际跨宽
function chordAudit() {
    var rows = [
        { name: "工具栏 y32", yt: 32, yb: 52, w: 144 },
        { name: "文本卡 y56", yt: 56, yb: 112, w: 176 },
        { name: "行1 y116",   yt: 116, yb: 142, w: 199 },
        { name: "行2 y144",   yt: 144, yb: 170, w: 179 },
        { name: "行3 y172",   yt: 172, yb: 198, w: 164 },
        { name: "行4 y200",   yt: 200, yb: 224, w: 100 }
    ];
    for (var i = 0; i < rows.length; i++) {
        var m = rowMax(rows[i].yt, rows[i].yb);
        var ok = rows[i].w <= m;
        eos.console.log("[chord] " + rows[i].name + " 顶=" + rows[i].yt + " 底=" + rows[i].yb +
            " 实际宽=" + rows[i].w + " 上限=" + m +
            (ok ? "  PASS" : "  FAIL"));
    }
    eos.console.log("[count] keys=" + keys.length + " symKeys=" + symKeys.length +
        " tools=2 card=1 caret=1 total_obj=" + (keys.length + symKeys.length + 5));
}

audit(R.root, 0, "R.root");
chordAudit();
