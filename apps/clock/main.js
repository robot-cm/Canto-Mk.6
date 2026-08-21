// ElenixOS 时光集 (Chrono) — 纯时钟页（用户精简指令 2026-08-16）
//
// 只做时钟：dial 深色盘 + 点阵刻度 + 数字1-12 + 时分秒三指针（实时动）+
// 中心 + 数字时间 HH:MM:SS。单主题（monet），无切换；无日期；无 tab 栏。
//
// 红线：lv.arc 禁 / transformRotation 仅3指针 / border三件套禁 /
//   radius≥54 禁 / flex 禁 / opa 裸数字 / color.hex 数字 / setFontSize。

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Chrono");

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hex(v) { return lv.color.hex(v); }

// 单主题（monet）：dial 深色 + 强调色
var DIAL_COLOR = 0x141216;
var ACCENT = 0xB07A8E;
var WHITE = 0xFFFFFF;

var DISP_W = 240, DISP_H = 240;
var CX = 120, CY = 120;

// ===================== dial（全屏深色盘，radius 0） =====================
var dial = new lv.obj(view);
dial.setSize(DISP_W, DISP_H);
dial.setPos(0, 0);
dial.setStyleRadius(0, 0);
dial.setStyleBgOpa(255, 0);
dial.setStyleBgColor(hex(DIAL_COLOR), 0);
dial.setStylePadAll(0, 0);
dial.setStyleBorderWidth(0, 0);
dial.removeFlag(lv.OBJ_FLAG_CLICKABLE);
dial.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// 极坐标：deg 从 12 点顺时针
function pt(r, deg) {
    var rad = deg * Math.PI / 180;
    return [CX + r * Math.sin(rad), CY - r * Math.cos(rad)];
}

// ===================== 刻度（点阵，禁 transform/arc） =====================
function mark(w, h, x, y, opa) {
    var m = new lv.obj(dial);
    m.setSize(w, h);
    m.setPos(Math.round(x - w / 2), Math.round(y - h / 2));
    m.setStyleRadius(Math.floor(Math.min(w, h) / 2), 0);
    m.setStyleBgColor(WHITE, 0);
    m.setStyleBgOpa(opa, 0);
    m.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    m.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return m;
}
// 分钟刻度：m%5!=0 → 2×2 白 Opa120 @r=108
for (var mi = 1; mi < 60; mi++) {
    if (mi % 5 !== 0) {
        var pm = pt(108, mi * 6);
        mark(2, 2, pm[0], pm[1], 120);
    }
}
// 小时长刻度：12/3/9 → 2×8 / 8×2 白 Opa230
var h12 = pt(108, 0), h3 = pt(108, 90), h9 = pt(108, 270);
mark(2, 8, h12[0], h12[1], 230);
mark(8, 2, h3[0], h3[1], 230);
mark(8, 2, h9[0], h9[1], 230);
// 其余 8 向（1,2,4,5,7,8,10,11）→ 4×4 白 Opa230（6 让位文字列）
for (var hi = 1; hi < 12; hi++) {
    if (hi === 3 || hi === 6 || hi === 9) continue;
    var ph = pt(108, hi * 30);
    mark(4, 4, ph[0], ph[1], 230);
}

// ===================== 数字 1-12（跳过 6）@r=88 =====================
var NUMS = [12, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11];
var numDefs = [];
for (var ni = 0; ni < NUMS.length; ni++) {
    var lbl = new lv.label(dial);
    lbl.setText(String(NUMS[ni]));
    lbl.setFontSize(16);                       // 16 → 22px（三档不可精确）
    lbl.setStyleTextColor(WHITE, 0);
    lbl.setStyleTextOpa(230, 0);
    lbl.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    lbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    numDefs.push({ lbl: lbl, deg: NUMS[ni] * 30 });
}
try { dial.updateLayout(); } catch (e) {}
for (var nk = 0; nk < numDefs.length; nk++) {
    var pn = pt(88, numDefs[nk].deg);
    var tw = numDefs[nk].lbl.getWidth();
    var th = numDefs[nk].lbl.getHeight();
    numDefs[nk].lbl.setPos(Math.round(pn[0] - tw / 2), Math.round(pn[1] - th / 2));
}

// ===================== 指针（3 个 transformRotation，唯一允许） =====================
function makeHand(w, h, colorKey, opa) {
    var hand = new lv.obj(dial);
    hand.setStyleRadius(Math.floor(w / 2), 0);  // LineRounded 等价
    hand.setStyleBgColor(hex(colorKey), 0);
    hand.setStyleBgOpa(opa, 0);
    hand.setStyleTransformPivotX(Math.floor(w / 2), 0);
    hand.setStyleTransformPivotY(h, 0);         // 根部 pivot → 无尾翼
    hand.setSize(w, h);
    hand.setPos(CX - Math.floor(w / 2), CY - h);
    hand.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    hand.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return hand;
}
var hourHand = makeHand(4, 52, WHITE, 230);
var minuteHand = makeHand(3, 80, WHITE, 230);
var secondHand = makeHand(2, 96, ACCENT, 255);

// ===================== 中心（白 10×10 + 强调 4×4 叠放） =====================
var cWhite = new lv.obj(dial);
cWhite.setSize(10, 10);
cWhite.setPos(CX - 5, CY - 5);
cWhite.setStyleRadius(5, 0);
cWhite.setStyleBgColor(WHITE, 0);
cWhite.setStyleBgOpa(255, 0);
cWhite.removeFlag(lv.OBJ_FLAG_CLICKABLE);
cWhite.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var cDot = new lv.obj(dial);
cDot.setSize(4, 4);
cDot.setPos(CX - 2, CY - 2);
cDot.setStyleRadius(2, 0);
cDot.setStyleBgColor(hex(ACCENT), 0);
cDot.setStyleBgOpa(255, 0);
cDot.removeFlag(lv.OBJ_FLAG_CLICKABLE);
cDot.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== 数字时间（HH:MM:SS，@y146） =====================
var timeTxt = new lv.label(dial);
timeTxt.setSize(240, 18);
timeTxt.setPos(0, 146);
timeTxt.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
timeTxt.setFontSize(13);                       // 13 → 22px
timeTxt.setStyleTextColor(WHITE, 0);
timeTxt.setStyleTextOpa(250, 0);

// ===================== 渲染（时分秒实时动，每 500ms；秒针机械 tick anim） =====================
var lastSecAngle = 0;
function render() {
    var t = eos.time.getNow();
    var s = t.hour * 3600 + t.min * 60 + t.sec;
    hourHand.setStyleTransformRotation(Math.floor((s % 43200) / 43200 * 3600), 0);
    minuteHand.setStyleTransformRotation(Math.floor((s % 3600) / 3600 * 3600), 0);
    // 动效6：秒针跳秒用 300ms ease-out 角度动画（机械 tick 感）
    var a2 = Math.floor((s % 60) / 60 * 3600);
    if (a2 !== lastSecAngle) {
        var aa = new lv.anim();
        aa.init(); aa.setVar(secondHand);
        aa.setValues(lastSecAngle, a2); aa.setDuration(300); aa.setPathCb(3); // EASE_OUT
        aa.setCustomExecCb(function (an, v) {
            secondHand.setStyleTransformRotation(Math.round(v), 0);
        });
        lastSecAngle = a2;
    }
    timeTxt.setText(pad2(t.hour) + ":" + pad2(t.min) + ":" + pad2(t.sec));
}

var tick = new lv.timer(function () { render(); }, 500, null);
tick.setRepeatCount(-1);   // 关键：默认 repeat_count=0 → timer 只触发一次，指针不动！
render();

// ===== audit（协议第六节）=====
function audit(o, d, name) {
    var tag = name || "obj";
    if (typeof o.getText === "function") { try { tag += "<" + o.getText() + ">"; } catch (e) {} }
    var c = o.getCoords();
    var indent = "";
    for (var k = 0; k < d; k++) { indent += "  "; }
    var r = "?", bo = "?", bw = "?", hid = "?";
    try { r = o.getStyleRadius(0); } catch (e) {}
    try { bo = o.getStyleBgOpa(0); } catch (e) {}
    try { bw = o.getStyleBorderWidth(0); } catch (e) {}
    try { hid = o.hasFlag(lv.OBJ_FLAG_HIDDEN); } catch (e) {}
    eos.console.log(indent + tag +
        " xywh=" + c.x1 + "," + c.y1 + "," + (c.x2 - c.x1) + "," + (c.y2 - c.y1) +
        " r=" + r + " bgOpa=" + bo + " bw=" + bw + " hid=" + hid);
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

eos.console.log("[chrono] 纯时钟页完成（单主题 monet）");
