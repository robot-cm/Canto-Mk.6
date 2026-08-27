// Canto Mk.6 环球时间 — 精简版
// 背景：地球点阵（400 点池 + DOTS 数据 + 每 tick 全量球面投影）导致 DRAM 紧张、
//       UI 任务单次执行过久被 task_wdt 重置（fatal code=10）。
// 处理：按需求移除地球点阵，仅保留五个地区时间（按钮切换 + 聚焦城市时间 HUD）。

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "环球时间");

// ===================== 根容器 =====================
var root = new lv.obj(view);
root.setSize(eos.DISPLAY_WIDTH, eos.DISPLAY_HEIGHT);
root.setPos(0, 0);
root.setStyleRadius(0, 0);
root.setStyleBgOpa(255, 0);
root.setStyleBgColor(lv.color.hex(0x050810), 0);
root.setStylePadAll(0, 0);
root.setStyleBorderWidth(0, 0);
root.setScrollbarMode(0);
root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
root.removeFlag(lv.OBJ_FLAG_CLICKABLE);

// ===================== 城市表 =====================
var CITIES = {
    "香港": { code: "HK", off: 8 },
    "东京": { code: "TYO", off: 9 },
    "北京": { code: "BJS", off: 8 },
    "斯德哥尔摩": { code: "STO", off: 2 },
    "纽约": { code: "NYC", off: -4 },
    "洛杉矶": { code: "LAX", off: -8 }
};
var CITY_NAMES = ["香港", "东京", "北京", "斯德哥尔摩", "纽约", "洛杉矶"];

// ===================== 城市按钮（两行 3+3 放大，胶囊 y170/198） =====================
var BTN_W = 34, BTN_H = 26;
var BTN_XS = [61, 103, 145];   // 每行 3 键：3*34+2*8=118，居中 x0=61
var BTN_YS = [170, 198];       // 行 y：170..196 / 198..224（圆内 y224 界 x≤179.9）
var BTN_SHORT = ["HK", "TYO", "BJS", "STO", "NYC", "LAX"];
var btns = [];
var focusCity = "香港";

var pressTimer = new lv.timer(function () {
    for (var i = 0; i < btns.length; i++) {
        var sel = (btns[i].name === focusCity);
        btns[i].btn.setStyleBgOpa(sel ? 255 : 22, 0);
    }
    pressTimer.pause();
}, 120, null);
pressTimer.pause();

function pressFx(b) {
    b.setStyleBgOpa(255, 0);
    pressTimer.reset();
    pressTimer.resume();
}

function makeBtn(idx, name) {
    var b = new lv.button(root);
    b.setSize(BTN_W, BTN_H);
    b.setPos(BTN_XS[idx % 3], BTN_YS[Math.floor(idx / 3)]);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(22, 0);
    b.setStyleBgColor(0xFFFFFF, 0);
    b.setStylePadAll(0, 0);
    b.setExtClickArea(4);
    var l = new lv.label(b);
    l.setSize(BTN_W, BTN_H);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(12);
    l.setStyleTextColor(0xFFFFFF, 0);
    l.setStyleTextOpa(200, 0);
    l.setText(BTN_SHORT[idx]);
    l.align(lv.ALIGN_CENTER, 0, 0);
    b.addEventCb(function (nm) {
        return function () {
            pressFx(b);
            tapCity(nm);
        };
    }(name), lv.EVENT_PRESSED, null);
    btns.push({ btn: b, lbl: l, name: name });
}

function updateBtns() {
    for (var i = 0; i < btns.length; i++) {
        var sel = btns[i].name === focusCity;
        btns[i].btn.setStyleBgOpa(sel ? 255 : 22, 0);
        btns[i].btn.setStyleBgColor(sel ? 0x4A90D9 : 0xFFFFFF, 0);
        btns[i].lbl.setStyleTextOpa(sel ? 255 : 200, 0);
        btns[i].lbl.setStyleTextColor(0xFFFFFF, 0);
    }
}

function tapCity(name) {
    if (name === focusCity) { return; }
    focusCity = name;
    updateBtns();
    updateHud();
}
for (var bi = 0; bi < CITY_NAMES.length; bi++) { makeBtn(bi, CITY_NAMES[bi]); }

// ===================== 聚焦城市时间 HUD（居中） =====================
var hudMain = new lv.label(root);           // "HK 09:00" 大字号
hudMain.setStyleTextColor(0xFFFFFF, 0);
hudMain.setStyleTextOpa(250, 0);
hudMain.setFontSize(30);

var hudZone = new lv.label(root);           // "UTC+8" 小字
hudZone.setStyleTextColor(0xFFFFFF, 0);
hudZone.setStyleTextOpa(120, 0);
hudZone.setFontSize(14);

function pad2(n) { return (n < 10 ? "0" : "") + n; }

function layoutHud() {
    try { root.updateLayout(); } catch (e) {}
    var w1 = hudMain.getWidth();
    var w2 = hudZone.getWidth();
    var x0 = Math.floor(120 - (w1 + w2 + 6) / 2);
    hudMain.setPos(x0, 92);
    hudZone.setPos(x0 + w1 + 6, 98);
}

function updateHud() {
    var t = eos.time.getNow();
    var c = CITIES[focusCity];
    var h = (t.hour + c.off - 8 + 24) % 24;    // 设备时区 +8 → 城市 = 本地 + off - 8
    hudMain.setText(c.code + " " + pad2(h) + ":" + pad2(t.min));
    hudZone.setText("UTC" + (c.off >= 0 ? "+" : "") + c.off);
    layoutHud();
}

// ===================== tick（100ms，仅 HUD 每秒刷新） =====================
var _tickCnt = 0;
var tick = new lv.timer(function () {
    _tickCnt++;
    if (_tickCnt % 10 === 0) { updateHud(); }
}, 100, null);
tick.setRepeatCount(-1);

// ===================== 启动 =====================
focusCity = "香港";
updateBtns();
updateHud();

eos.console.log("[globaltime] lite: earth dots removed, 5 city times only");
