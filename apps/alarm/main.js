// ElenixOS 闹钟 — 闹钟.html 移植（骨架 v1：布局静态，行为 P1，动效 P2）
//
// 布局：状态行 y30 / 当前时间 y50 双label / 日期 y82 / 选择器(时列x86 分列x154) /
//       底部行 y166(开关/设定/取消) / 响铃覆盖层(默认隐藏)
// 红线：opa 裸数字 / hex 数字 / setFontSize / 游标布局 / 胶囊 radius999 禁 border /
//       label.align CENTER / 装饰容器 removeFlag(SCROLLABLE) / timer≥30ms(P1)
// 状态机(P1)：待命(alm_on=0) ⇄ 已设定(alm_on=1)；+/- 回绕只改选择器；
//   设定→config+on=1；取消→回读 config；开关→翻转 on；响铃→覆盖层
// 持久化：eos.config alm_h/alm_m/alm_on

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "闹钟");

var R = {};
var DIAL_COLOR = 0x12121A;
var ACCENT = 0x4A90D9;        // 已设定/响铃强调色
var IDLE_COLOR = 0x8A8F98;    // 待命灰
var WHITE = 0xFFFFFF;
var WEEK = ["周日", "周一", "周二", "周三", "周四", "周五", "周六"];

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hex(v) { return lv.color.hex(v); }

// ===================== R.root 全屏容器 =====================
R.root = new lv.obj(view);
R.root.setSize(240, 240);
R.root.setPos(0, 0);
R.root.setStyleRadius(0, 0);
R.root.setStyleBgOpa(255, 0);
R.root.setStyleBgColor(hex(DIAL_COLOR), 0);
R.root.setStylePadAll(0, 0);
R.root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
R.root.setScrollbarMode(0);

// ===================== helper（计时器同款设计语言） =====================
function stylePill(o, w, h, opa, color) {   // 胶囊：radius999 禁 border，fill 区分
    o.setSize(w, h);
    o.setStyleRadius(999, 0);
    o.setStyleBgOpa(opa, 0);
    o.setStyleBgColor(hex(color), 0);
    o.setStylePadAll(0, 0);
    o.setStyleBorderWidth(0, 0);
    o.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return o;
}
function pillLabel(o, text, size, w, h) {   // label 进胶囊：setSize 胶囊尺寸 + align CENTER
    o.setSize(w, h);
    o.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    o.setFontSize(size);
    o.setStyleTextColor(hex(WHITE), 0);
    o.setText(text);
    o.align(lv.ALIGN_CENTER, 0, 0);
    o.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return o;
}
function centerLabel(parent, x, y, w, h, size, opa) {  // 全宽/定宽居中文字
    var l = new lv.label(parent);
    l.setSize(w, h);
    l.setPos(x, y);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(size);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setStyleTextOpa(opa, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return l;
}

// ===================== 1. 状态行 y30（点 8px + 文案 font12 整组居中） =====================
var statusDot = new lv.obj(R.root);
statusDot.setSize(8, 8);
statusDot.setStyleRadius(4, 0);
statusDot.setStyleBgOpa(255, 0);
statusDot.setStyleBgColor(hex(IDLE_COLOR), 0);
statusDot.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var statusTxt = new lv.label(R.root);
statusTxt.setFontSize(12);
statusTxt.setStyleTextColor(hex(WHITE), 0);
statusTxt.setStyleTextOpa(250, 0);
statusTxt.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
statusTxt.setText("待命");

function layoutStatusRow() {
    try { R.root.updateLayout(); } catch (e) {}
    var w1 = 24;
    try { w1 = statusTxt.getWidth(); } catch (e) {}
    if (!w1 || w1 < 10) w1 = 24;                 // getWidth 异常兜底（"待命" 2字）
    var x0 = Math.floor(120 - (8 + 4 + w1) / 2);
    statusDot.setPos(x0, 32);                    // 点垂直居中于 font12 行
    statusTxt.setPos(x0 + 12, 30);
}
function updateStatus(text, color) {
    statusTxt.setText(text);
    statusDot.setStyleBgColor(hex(color), 0);
    layoutStatusRow();
}

// ===================== 2. 当前时间 y50（双 label 紧贴同基线） =====================
var timeTxt = new lv.label(R.root);
timeTxt.setFontSize(26);
timeTxt.setStyleTextColor(hex(WHITE), 0);
timeTxt.setStyleTextOpa(250, 0);
timeTxt.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var millisTxt = new lv.label(R.root);
millisTxt.setFontSize(26);
millisTxt.setStyleTextColor(hex(WHITE), 0);
millisTxt.setStyleTextOpa(160, 0);
millisTxt.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

function layoutTimeRow() {
    try { R.root.updateLayout(); } catch (e) {}
    var w1 = timeTxt.getWidth();
    var w2 = millisTxt.getWidth();
    var x0 = Math.floor(120 - (w1 + w2 + 2) / 2);
    timeTxt.setPos(x0, 50);
    millisTxt.setPos(x0 + w1 + 2, 50);     // 同 y 同基线
}

// ===================== 3. 日期 y82（不 setSize=文字宽，手动居中） =====================
var dateTxt = new lv.label(R.root);
dateTxt.setFontSize(11);
dateTxt.setStyleTextColor(hex(WHITE), 0);
dateTxt.setStyleTextOpa(160, 0);
dateTxt.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
function layoutDate() {
    try { R.root.updateLayout(); } catch (e) {}
    var w = dateTxt.getWidth();
    dateTxt.setPos(Math.floor(120 - w / 2), 82);
}

// ===================== 4. 选择器（时列 x86 / 分列 x154） =====================
var PICKER_COL = [86, 154];                 // 时 / 分 列中心
var PILL_W = 28, PILL_H = 16;
var VAL_Y = 116;

function makeStep(x, y, ch) {               // +/− 胶囊（w28 h16，label.align）
    var b = stylePill(new lv.button(R.root), PILL_W, PILL_H, 28, WHITE);
    b.setPos(x - PILL_W / 2, y);
    pillLabel(new lv.label(b), ch, 10, PILL_W, PILL_H);
    return b;
}
function makeVal(x, y) {                    // 数值 label（font20 两位补零，列中心对齐）
    var v = centerLabel(R.root, x - 14, y, 28, 22, 20, 250);
    return v;
}

var hPlus  = makeStep(PICKER_COL[0], 98, "+");
var hVal   = makeVal(PICKER_COL[0], VAL_Y);
var hMinus = makeStep(PICKER_COL[0], 138, "-");
var mPlus  = makeStep(PICKER_COL[1], 98, "+");
var mVal   = makeVal(PICKER_COL[1], VAL_Y);
var mMinus = makeStep(PICKER_COL[1], 138, "-");

var colon = centerLabel(R.root, 113, VAL_Y, 14, 22, 20, 250);
colon.setText(":");

function updatePicker() {
    hVal.setText(pad2(alm_h));
    mVal.setText(pad2(alm_m));
}

// ===================== 5. 底部行 y166 h22（开关/设定/取消 w40 gap4） =====================
var BTN_W = 40, BTN_H = 22;
var BTN_X = [56, 100, 144];
var swBtn, swLbl, setBtn, setLbl, canBtn, canLbl;

function makeBottom(x, key) {
    var b = stylePill(new lv.button(R.root), BTN_W, BTN_H, 28, WHITE);
    b.setPos(x, 166);
    var l = pillLabel(new lv.label(b), key, 10, BTN_W, BTN_H);
    return { btn: b, lbl: l };
}
var sw = makeBottom(BTN_X[0], "开关");
var st = makeBottom(BTN_X[1], "设定");
var ca = makeBottom(BTN_X[2], "取消");

function updateSwitch() {                    // 开启=强调色填充 / 关闭=白Opa16
    sw.btn.setStyleBgOpa(alm_on ? 255 : 16, 0);
    sw.btn.setStyleBgColor(hex(alm_on ? ACCENT : WHITE), 0);
}

// ===================== 6. 响铃覆盖层（默认隐藏，P1 触发/停止） =====================
var ringOv = new lv.obj(R.root);
ringOv.setSize(240, 240);
ringOv.setPos(0, 0);
ringOv.setStyleRadius(0, 0);
ringOv.setStyleBgOpa(220, 0);
ringOv.setStyleBgColor(hex(0x0E0E14), 0);
ringOv.setStylePadAll(0, 0);
ringOv.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
ringOv.addFlag(lv.OBJ_FLAG_HIDDEN);

var ringTitle = centerLabel(ringOv, 0, 70, 240, 18, 16, 255);
ringTitle.setText("闹钟响铃！");

var ringTime = centerLabel(ringOv, 0, 100, 240, 30, 28, 255);

var stopBtn = stylePill(new lv.button(ringOv), 64, 26, 255, ACCENT);
stopBtn.setPos(88, 150);
var stopLbl = pillLabel(new lv.label(stopBtn), "停止", 12, 64, 26);

function updateRingTime() {
    ringTime.setText(pad2(alm_h) + ":" + pad2(alm_m));
}

// ===================== 状态与持久化（骨架：读 config，行为 P1） =====================
var alm_h = 7, alm_m = 30, alm_on = false;
try { alm_h = eos.config.getNumber("alm_h"); } catch (e) {}
try { alm_m = eos.config.getNumber("alm_m"); } catch (e) {}
try { alm_on = eos.config.getNumber("alm_on") === 1; } catch (e) {}
if (typeof alm_h !== "number" || isNaN(alm_h)) alm_h = 7;
if (typeof alm_m !== "number" || isNaN(alm_m)) alm_m = 30;

// ===================== 初始显示 =====================
function update() {
    var t = eos.time.getNow();
    timeTxt.setText(pad2(t.hour) + ":" + pad2(t.min) + ":" + pad2(t.sec));
    var c = Math.floor((t.ms % 1000) / 10);
    millisTxt.setText("." + (c < 10 ? "0" : "") + c);
    layoutTimeRow();
    dateTxt.setText(t.year + "年" + t.month + "月" + t.day + "日 " + WEEK[t.day_of_week]);
    layoutDate();
    updatePicker();
    updateSwitch();
    updateRingTime();
    updateStatus(alm_on ? "已设定 " + pad2(alm_h) + ":" + pad2(alm_m) : "待命", alm_on ? ACCENT : IDLE_COLOR);
}
update();
try { R.root.updateLayout(); } catch (e) {}   /* audit 前强制布局，防 getCoords 旧缓存 */

// ===================== P1 状态机（+/- 回绕 / 设定 / 取消 / 开关 / 响铃） =====================
var editing_h = alm_h, editing_m = alm_m;    // 选择器编辑值（+/- 只改这里，设定才落盘）
var ringing = false, ringTick = 0, ringFlash = false;

function saveAlm() {
    try {
        eos.config.setNumber("alm_h", alm_h);
        eos.config.setNumber("alm_m", alm_m);
        eos.config.setNumber("alm_on", alm_on ? 1 : 0);
    } catch (e) {}
}
function refreshStatus() {
    updateSwitch();
    updateStatus(alm_on ? "已设定 " + pad2(alm_h) + ":" + pad2(alm_m) : "待命", alm_on ? ACCENT : IDLE_COLOR);
    updateRingTime();
}
function pickerStep(which, delta) {          // +/- 回绕，只改选择器不自动保存
    if (which === "h") { editing_h = (editing_h + delta + 24) % 24; hVal.setText(pad2(editing_h)); }
    else               { editing_m = (editing_m + delta + 60) % 60; mVal.setText(pad2(editing_m)); }
}
function doSet() {                           // 设定：选择器→config，on=1
    alm_h = editing_h; alm_m = editing_m; alm_on = true;
    saveAlm(); refreshStatus();
}
function doCancel() {                        // 取消：选择器回读 config 已存值
    editing_h = alm_h; editing_m = alm_m; updatePicker();
}
function doToggle() {                        // 开关：翻转 on，不改时间
    alm_on = !alm_on;
    saveAlm(); refreshStatus();
}
function ringStart() {
    if (ringing) return;
    ringing = true; ringTick = 0; ringFlash = false;
    ringOv.removeFlag(lv.OBJ_FLAG_HIDDEN);   // 全屏在最上 → 拦截一切点击（仅停止可点）
}
function ringStop() {
    if (!ringing) return;
    ringing = false;
    ringOv.addFlag(lv.OBJ_FLAG_HIDDEN);
    ringOv.setStyleOpa(255, 0);
}

// 事件绑定（fork 的 CLICKED 断 → 用 PRESSED）
hPlus.addEventCb(function () { pickerStep("h", 1); }, lv.EVENT_PRESSED, null);
hMinus.addEventCb(function () { pickerStep("h", -1); }, lv.EVENT_PRESSED, null);
mPlus.addEventCb(function () { pickerStep("m", 1); }, lv.EVENT_PRESSED, null);
mMinus.addEventCb(function () { pickerStep("m", -1); }, lv.EVENT_PRESSED, null);
sw.btn.addEventCb(doToggle, lv.EVENT_PRESSED, null);
st.btn.addEventCb(doSet, lv.EVENT_PRESSED, null);
ca.btn.addEventCb(doCancel, lv.EVENT_PRESSED, null);
stopBtn.addEventCb(ringStop, lv.EVENT_PRESSED, null);

// tick 100ms：时间刷新 + 响铃检测/闪烁/60s 自动停
var tick = new lv.timer(function () {
    var t = eos.time.getNow();
    timeTxt.setText(pad2(t.hour) + ":" + pad2(t.min) + ":" + pad2(t.sec));
    var c = Math.floor((t.ms % 1000) / 10);
    millisTxt.setText("." + (c < 10 ? "0" : "") + c);
    layoutTimeRow();
    dateTxt.setText(t.year + "年" + t.month + "月" + t.day + "日 " + WEEK[t.day_of_week]);
    layoutDate();
    if (!ringing && alm_on && t.hour === alm_h && t.min === alm_m && t.sec <= 1) ringStart();
    if (ringing) {
        ringTick++;
        if (ringTick % 5 === 0) {              // 500ms 闪烁
            ringFlash = !ringFlash;
            ringOv.setStyleOpa(ringFlash ? 255 : 200, 0);
        }
        if (ringTick >= 600) ringStop();       // 60s 自动停
    }
}, 100, null);
tick.setRepeatCount(-1);

// ===================== audit（弦宽自验，getStyle* 带 sel=0） =====================
function chordMax(yBottom) { return Math.floor(2 * Math.sqrt(14400 - (yBottom - 120) * (yBottom - 120)) - 16); }
function audit(o, d, name) {
    var tag = name || "obj";
    if (typeof o.getText === "function") { try { tag += "<" + o.getText() + ">"; } catch (e) {} }
    var c = o.getCoords();
    eos.console.log(new Array(d + 1).join("  ") + tag + " xywh=" + c.x1 + "," + c.y1 + "," + (c.x2 - c.x1) + "," + (c.y2 - c.y1) +
        " r=" + o.getStyleRadius(0) + " opa=" + o.getStyleBgOpa(0) + " bw=" + o.getStyleBorderWidth(0));
    for (var i = 0; i < o.getChildCount(); i++) audit(o.getChild(i), d + 1, "#" + i);
}
try {
    // 弦宽自验：逐行 行底可用弦宽
    var rows = [
        ["状态行", 30, 8 + 4 + statusTxt.getWidth(), 42],
        ["时间行", 50, timeTxt.getWidth() + millisTxt.getWidth() + 2, 76],
        ["日期行", 82, dateTxt.getWidth(), 96],
        ["选择器行", 98, 28, 138],
        ["底部行", 166, 3 * 40 + 2 * 4, 188],
        ["停止胶囊", 150, 64, 176]
    ];
    for (var ri = 0; ri < rows.length; ri++) {
        var r = rows[ri];
        var ok = r[2] <= chordMax(r[3]);
        eos.console.log("[alarm] 自验 " + r[0] + " w=" + r[2] + " 行底=" + r[3] + " 可用=" + chordMax(r[3]) + " " + (ok ? "OK" : "FAIL"));
    }
    eos.console.log("[alarm] 对象账本 root children=" + R.root.getChildCount() + " (期望 16 = 状态2+时间2+日期1+选择器7+底部3胶囊+覆盖层1)，总对象 23 = 16+底部label3+覆盖层内4)");
} catch (e) {
    eos.console.log("[alarm] 自验异常: " + e);
}
audit(R.root, 0, "root");
