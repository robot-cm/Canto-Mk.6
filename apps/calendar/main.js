// Calendar - monthly grid + festival lookup + month/year navigation
// 240x240 round screen, all-English.
// Layout: header(y0-24) | nav row y25-47: [<] Aug 2026 [>] | Sun..Sat(y48)
//   | day grid y64-206 (7x6, colW29/gap2, rowH22/gap2) | info bar y212-234.
// Tap title "Aug 2026" -> picker: year [-]/[+] + 12 months grid; tap outside closes.
// Today = accent fill(30)+accent text; selected = accent fill(200)+white text;
//   festival = corner dot; weekends = grey text.
// Data: /sdcard/calendar/festival.json (solar/lunar/nthwd).
// Red lines: no arc / no border triple / radius<54 / no flex.
// EVENT_CLICKED broken in this fork -> use EVENT_PRESSED.

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Calendar");

var BG = 0x0E0E14;
var PANEL = 0x16161E;
var WHITE = 0xFFFFFF;
var GREY = 0x8A8F98;
var ACCENT = 0x4A90D9;

function hex(v) { return lv.color.hex(v); }

var R = {};
R.root = new lv.obj(view);
R.root.setSize(240, 240);
R.root.setPos(0, 0);
R.root.setStyleRadius(0, 0);
R.root.setStyleBgOpa(255, 0);
R.root.setStyleBgColor(hex(BG), 0);
R.root.setStylePadAll(0, 0);
R.root.setStyleBorderWidth(0, 0);
R.root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
R.root.setScrollbarMode(0);

// ===================== date math (no Date object) =====================
// 1900-01-01 was a Monday -> weekday = (daysSince1900 + 1) % 7 (0 = Sun)
var lunarInfo = [0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2, //1900-1909
0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977, //1910-1919
0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970, //1920-1929
0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950, //1930-1939
0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557, //1940-1949
0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0, //1950-1959
0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0, //1960-1969
0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6, //1970-1979
0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570, //1980-1989
0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0, //1990-1999
0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5, //2000-2009
0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930, //2010-2019
0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530, //2020-2029
0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45, //2030-2039
0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0, //2040-2049
0x14b63,0x09370,0x049f8,0x04970,0x064b0,0x168a6,0x0ea50,0x06b20,0x1a6c4,0x0aae0, //2050-2059
0x092e0,0x0d2e3,0x0c960,0x0d557,0x0d4a0,0x0da50,0x05d55,0x056a0,0x0a6d0,0x055d4, //2060-2069
0x052d0,0x0a9b8,0x0a950,0x0b4a0,0x0b6a6,0x0ad50,0x055a0,0x0aba4,0x0a5b0,0x052b0, //2070-2079
0x0b273,0x06930,0x07337,0x06aa0,0x0ad50,0x14b55,0x04b60,0x0a570,0x054e4,0x0d160, //2080-2089
0x0e968,0x0d520,0x0daa0,0x16aa6,0x056d0,0x04ae0,0x0a9d4,0x0a2d0,0x0d150,0x0f252, //2090-2099
0x0d520];                                                                              //2100

function isLeapG(y) { return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0; }
function monthDaysG(y, m) {
    if (m === 2) return isLeapG(y) ? 29 : 28;
    return (m === 4 || m === 6 || m === 9 || m === 11) ? 30 : 31;
}
function dayFrom1900(y, m, d) {   // days since 1900-01-01
    var days = 0, i;
    for (i = 1900; i < y; i++) days += isLeapG(i) ? 366 : 365;
    for (i = 1; i < m; i++) days += monthDaysG(y, i);
    return days + d - 1;
}
function firstWeekday(y, m) { return (dayFrom1900(y, m, 1) + 1) % 7; }   // 0 = Sun

function leapMonth(y) { return lunarInfo[y - 1900] & 0xf; }
function leapDays(y) { return leapMonth(y) ? ((lunarInfo[y - 1900] & 0x10000) ? 30 : 29) : 0; }
function monthDaysL(y, m) { return (lunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }
function lYearDays(y) {
    var info = lunarInfo[y - 1900], sum = 348;
    for (var i = 0x8000; i > 0x8; i >>= 1) sum += (info & i) ? 1 : 0;
    return sum + leapDays(y);
}
// solar -> lunar {m, d, leap} or null (year out of table range)
function solar2lunar(y, m, d) {
    if (y < 1900 || y > 2100) return null;
    var offset = dayFrom1900(y, m, d) - 30;   // 1900-01-31 == lunar 1900-1-1
    var i, temp = 0;
    for (i = 1900; i < 2101 && offset > 0; i++) {
        temp = lYearDays(i);
        offset -= temp;
    }
    if (offset < 0) { offset += temp; i--; }
    var year = i;
    var leap = leapMonth(year), isLeap = false;
    for (i = 1; i < 13 && offset > 0; i++) {
        if (leap > 0 && i === (leap + 1) && !isLeap) {
            --i; isLeap = true; temp = leapDays(year);
        } else {
            temp = monthDaysL(year, i);
        }
        if (isLeap && i === (leap + 1)) isLeap = false;
        offset -= temp;
    }
    if (offset === 0 && leap > 0 && i === leap + 1) {
        if (isLeap) isLeap = false; else { isLeap = true; --i; }
    }
    if (offset < 0) { offset += temp; --i; }
    return { m: i, d: offset + 1, leap: isLeap };
}
function nthWdDay(y, m, n, w) {   // day of nth weekday w (0=Sun) in month m, or -1
    var d = 1 + ((w - firstWeekday(y, m) + 7) % 7) + 7 * (n - 1);
    return d > monthDaysG(y, m) ? -1 : d;
}

// ===================== festivals data =====================
var FEST_PATH = "/sdcard/calendar/festival.json";
var festList = [];

function festNames(y, m, d, lun) {
    var names = [];
    if (!festList || !festList.length) return names;
    for (var i = 0; i < festList.length; i++) {
        var f = festList[i];
        var t = f.type || "solar";
        if (t === "lunar") {
            if (lun && f.m === lun.m && f.d === lun.d) names.push(f.name);
        } else if (t === "nthwd") {
            if (f.m === m && nthWdDay(y, m, f.n, f.w) === d) names.push(f.name);
        } else {
            if (f.m === m && f.d === d) names.push(f.name);
        }
    }
    return names;
}

// Uint8Array -> UTF-8 string (surrogate-safe, chunked)
function bytesToStr(b) {
    var s = "", n = b.length, i = 0, block = [];
    while (i < n) {
        var c = b[i++], ch;
        if (c < 0x80) {
            ch = c;
        } else if (c < 0xE0) {
            ch = ((c & 0x1F) << 6) | (b[i++] & 0x3F);
        } else if (c < 0xF0) {
            ch = ((c & 0x0F) << 12) | ((b[i++] & 0x3F) << 6) | (b[i++] & 0x3F);
        } else {
            var cp = ((c & 0x07) << 18) | ((b[i++] & 0x3F) << 12) |
                     ((b[i++] & 0x3F) << 6) | (b[i++] & 0x3F);
            cp -= 0x10000;
            block.push(String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)));
            if (block.length > 2048) { s += block.join(""); block = []; }
            continue;
        }
        block.push(String.fromCharCode(ch));
        if (block.length > 2048) { s += block.join(""); block = []; }
    }
    if (block.length) s += block.join("");
    return s;
}

function loadFest() {
    setTimeout(function () {
        var raw = undefined;
        try {
            if (eos.fs.size(FEST_PATH) > 32768) { festLbl.setText("festival.json too big"); return; }
            raw = eos.fs.read(FEST_PATH);
        } catch (e) {}
        if (!raw) { festLbl.setText("No /sdcard/calendar/festival.json"); return; }
        var data = null;
        try { data = JSON.parse(bytesToStr(raw)); } catch (e) { data = null; }
        if (!data || !data.festivals || !data.festivals.length) {
            festLbl.setText("Bad festival.json");
            return;
        }
        festList = data.festivals;
        markDots();
        paintAll();
        showInfo(selIdx);
    }, 30);
}

// ===================== month state =====================
var now = eos.time.getNow();
var Y = now.year, M = now.month;
var dim = 0, firstWd = 0;
var today = now.day, todayIdx = -1;
var selIdx = -1;

var WDS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
var MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
var COL_W = 29, COL_GAP = 2, ROW_H = 22, ROW_GAP = 2;
var GRID_X = 12, GRID_Y = 64;

// ===================== nav row =====================
function pressFx(b) {
    b.setStyleTransformScale(232, 0);
    var a = new lv.anim();
    a.init(); a.setVar(b);
    a.setValues(232, 256); a.setDuration(120);
    a.setCustomExecCb(function (an, v) { b.setStyleTransformScale(Math.round(v), 0); });
}

function navBtn(x, txt) {
    var b = new lv.button(R.root);
    b.setSize(36, 22);
    b.setPos(x, 25);
    b.setStyleRadius(10, 0);
    b.setStyleBgOpa(14, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(8);
    var l = new lv.label(b);
    l.setSize(36, 22);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(16);
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(250, 0);
    l.setText(txt);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return b;
}
var navPrev = navBtn(20, "<");
var navNext = navBtn(184, ">");
navPrev.addEventCb(function () { pressFx(navPrev); goPrev(); }, lv.EVENT_PRESSED, null);
navNext.addEventCb(function () { pressFx(navNext); goNext(); }, lv.EVENT_PRESSED, null);

var navLbl = new lv.label(R.root);
navLbl.setSize(240, 16);
navLbl.setPos(0, 28);
navLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
navLbl.setFontSize(15);
navLbl.setStyleTextColor(WHITE, 0);
navLbl.setStyleTextOpa(250, 0);
navLbl.addFlag(lv.OBJ_FLAG_CLICKABLE);
navLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
navLbl.addEventCb(function () { togglePicker(); }, lv.EVENT_PRESSED, null);

// ===================== weekday labels =====================
for (var w = 0; w < 7; w++) {
    var wl = new lv.label(R.root);
    wl.setSize(COL_W, 12);
    wl.setPos(GRID_X + w * (COL_W + COL_GAP), 48);
    wl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    wl.setFontSize(11);
    wl.setStyleTextColor(GREY, 0);
    wl.setStyleTextOpa(190, 0);
    wl.setText(WDS[w]);
    wl.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    wl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
}

// ===================== day grid (day values filled by refreshMonth) =====================
var cells = [];
for (var r = 0; r < 6; r++) {
    for (var c = 0; c < 7; c++) {
        var i = r * 7 + c;
        var b = new lv.button(R.root);
        b.setSize(COL_W, ROW_H);
        b.setPos(GRID_X + c * (COL_W + COL_GAP), GRID_Y + r * (ROW_H + ROW_GAP));
        b.setStyleRadius(8, 0);
        b.setStyleBgOpa(0, 0);
        b.setStyleBgColor(hex(ACCENT), 0);
        b.setStylePadAll(0, 0);
        b.setStyleBorderWidth(0, 0);
        var l = new lv.label(b);
        l.setSize(COL_W, ROW_H);
        l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        l.setFontSize(13);
        l.setStyleTextColor(WHITE, 0);
        l.setStyleTextOpa(240, 0);
        l.align(lv.ALIGN_CENTER, 0, 0);
        l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        var dot = new lv.obj(b);
        dot.setSize(4, 4);
        dot.setPos(COL_W - 9, 2);
        dot.setStyleRadius(2, 0);
        dot.setStyleBgColor(hex(ACCENT), 0);
        dot.setStyleBgOpa(0, 0);
        dot.removeFlag(lv.OBJ_FLAG_CLICKABLE);
        dot.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        cells.push({ btn: b, lbl: l, dot: dot, day: 0, hasFest: false });
        (function (idx) {
            b.addEventCb(function () { select(idx); }, lv.EVENT_PRESSED, null);
        })(i);
    }
}

// ===================== info bar (scroll label) =====================
var festBox = new lv.obj(R.root);
festBox.setSize(216, 22);
festBox.setPos(12, 212);
festBox.setStyleBgOpa(8, 0);
festBox.setStyleBgColor(hex(WHITE), 0);
festBox.setStyleRadius(8, 0);
festBox.setStylePadAll(0, 0);
festBox.setStyleBorderWidth(0, 0);
festBox.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var festLbl = new lv.label(festBox);
festLbl.setSize(204, 18);
festLbl.setPos(6, 2);
festLbl.setFontSize(12);
festLbl.setStyleTextColor(WHITE, 0);
festLbl.setStyleTextOpa(235, 0);
festLbl.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
festLbl.addFlag(lv.OBJ_FLAG_SCROLLABLE);
festLbl.setLongMode(lv.LABEL_LONG_SCROLL_CIRCULAR);
festLbl.setText("Tap a day\u2026");

// ===================== year/month picker (tap title) =====================
var pickerY = Y;
var pickerMask = new lv.button(R.root);
pickerMask.setSize(240, 240);
pickerMask.setPos(0, 0);
pickerMask.setStyleRadius(0, 0);
pickerMask.setStyleBgOpa(140, 0);
pickerMask.setStyleBgColor(hex(0x000000), 0);
pickerMask.setStylePadAll(0, 0);
pickerMask.setStyleBorderWidth(0, 0);
pickerMask.addEventCb(function () { closePicker(); }, lv.EVENT_PRESSED, null);

var pickerPanel = new lv.button(R.root);
pickerPanel.setSize(180, 134);
pickerPanel.setPos(30, 45);
pickerPanel.setStyleRadius(16, 0);
pickerPanel.setStyleBgOpa(255, 0);
pickerPanel.setStyleBgColor(hex(PANEL), 0);
pickerPanel.setStylePadAll(0, 0);
pickerPanel.setStyleBorderWidth(0, 0);
pickerPanel.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

function pickerBtn(x, y, txt) {
    var b = new lv.button(pickerPanel);
    b.setSize(36, 24);
    b.setPos(x, y);
    b.setStyleRadius(10, 0);
    b.setStyleBgOpa(14, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(6);
    var l = new lv.label(b);
    l.setSize(36, 24);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(16);
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(250, 0);
    l.setText(txt);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return b;
}
var pickerMinus = pickerBtn(10, 5, "-");
pickerMinus.addEventCb(function () {
    pressFx(pickerMinus);
    if (pickerY > 1900) { pickerY--; refreshPicker(); }
}, lv.EVENT_PRESSED, null);

var pickerPlus = pickerBtn(134, 5, "+");
pickerPlus.addEventCb(function () {
    pressFx(pickerPlus);
    if (pickerY < 2100) { pickerY++; refreshPicker(); }
}, lv.EVENT_PRESSED, null);

var pickerYearLbl = new lv.label(pickerPanel);
pickerYearLbl.setSize(88, 24);
pickerYearLbl.setPos(46, 5);
pickerYearLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
pickerYearLbl.setFontSize(15);
pickerYearLbl.setStyleTextColor(WHITE, 0);
pickerYearLbl.setStyleTextOpa(250, 0);
pickerYearLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var pickerCells = [];
for (var pm = 0; pm < 12; pm++) {
    (function (m0) {
        var b = new lv.button(pickerPanel);
        b.setSize(36, 24);
        b.setPos(12 + (m0 % 4) * 40, 35 + Math.floor(m0 / 4) * 28);
        b.setStyleRadius(10, 0);
        b.setStyleBgOpa(14, 0);
        b.setStyleBgColor(hex(ACCENT), 0);
        b.setStylePadAll(0, 0);
        b.setStyleBorderWidth(0, 0);
        b.setExtClickArea(4);
        var l = new lv.label(b);
        l.setSize(36, 24);
        l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        l.setFontSize(12);
        l.setStyleTextColor(WHITE, 0);
        l.setStyleTextOpa(250, 0);
        l.setText(MONTHS[m0]);
        l.align(lv.ALIGN_CENTER, 0, 0);
        l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        pickerCells.push(b);
        b.addEventCb(function () {
            pressFx(b);
            Y = pickerY; M = m0 + 1;
            closePicker();
            refreshMonth();
        }, lv.EVENT_PRESSED, null);
    })(pm);
}

pickerMask.addFlag(lv.OBJ_FLAG_HIDDEN);
pickerPanel.addFlag(lv.OBJ_FLAG_HIDDEN);

function refreshPicker() {
    pickerYearLbl.setText(String(pickerY));
    for (var m0 = 0; m0 < 12; m0++) {
        var on = (pickerY === Y && m0 + 1 === M);
        pickerCells[m0].setStyleBgOpa(on ? 200 : 14, 0);
    }
}

function openPicker() {
    pickerY = Y;
    refreshPicker();
    pickerMask.clearFlag(lv.OBJ_FLAG_HIDDEN);
    pickerPanel.clearFlag(lv.OBJ_FLAG_HIDDEN);
}

function closePicker() {
    pickerMask.addFlag(lv.OBJ_FLAG_HIDDEN);
    pickerPanel.addFlag(lv.OBJ_FLAG_HIDDEN);
}

function togglePicker() {
    if (pickerMask.hasFlag(lv.OBJ_FLAG_HIDDEN)) openPicker();
    else closePicker();
}

// ===================== drawing / selection =====================
function paintCell(idx, sel) {
    var cell = cells[idx];
    var isToday = (todayIdx >= 0 && idx === todayIdx);
    var weekend = (idx % 7 === 0 || idx % 7 === 6);
    if (sel) {
        cell.btn.setStyleBgOpa(200, 0);
        cell.lbl.setStyleTextColor(hex(WHITE), 0);
        cell.dot.setStyleBgColor(hex(WHITE), 0);
        cell.dot.setStyleBgOpa(255, 0);
    } else if (isToday) {
        cell.btn.setStyleBgOpa(30, 0);
        cell.lbl.setStyleTextColor(hex(ACCENT), 0);
        cell.dot.setStyleBgColor(hex(ACCENT), 0);
        cell.dot.setStyleBgOpa(cell.hasFest ? 255 : 0, 0);
    } else {
        cell.btn.setStyleBgOpa(0, 0);
        cell.lbl.setStyleTextColor(hex(weekend ? GREY : WHITE), 0);
        cell.dot.setStyleBgColor(hex(ACCENT), 0);
        cell.dot.setStyleBgOpa(cell.hasFest ? 255 : 0, 0);
    }
}

function paintAll() {
    for (var i = 0; i < cells.length; i++) paintCell(i, i === selIdx);
}

function select(idx) {
    var cell = cells[idx];
    if (!cell || cell.day < 1 || cell.day > dim) return;
    if (selIdx >= 0 && selIdx < cells.length && selIdx !== idx) paintCell(selIdx, false);
    selIdx = idx;
    paintCell(idx, true);
    showInfo(idx);
}

function showInfo(idx) {
    if (!cells[idx] || cells[idx].day < 1 || cells[idx].day > dim) return;
    var d = cells[idx].day;
    var lun = solar2lunar(Y, M, d);
    var names = festNames(Y, M, d, lun);
    var s = "";
    if (names.length) s = names.join(" | ");
    else if (lun) s = "Lunar " + lun.m + "/" + lun.d;
    if (idx === todayIdx) s = s ? "Today \u00b7 " + s : "Today";
    festLbl.setText(s);
    try { festLbl.scrollToX(0, 0); } catch (e) {}
}

function markDots() {
    for (var i = 0; i < cells.length; i++) {
        var cell = cells[i];
        if (cell.day < 1 || cell.day > dim) continue;
        var lun = solar2lunar(Y, M, cell.day);
        cell.hasFest = festNames(Y, M, cell.day, lun).length > 0;
    }
}

// ===================== month navigation =====================
function refreshMonth() {
    dim = monthDaysG(Y, M);
    firstWd = firstWeekday(Y, M);
    todayIdx = (Y === now.year && M === now.month) ? firstWd + today - 1 : -1;
    for (var i = 0; i < cells.length; i++) {
        var cell = cells[i];
        cell.day = i - firstWd + 1;
        if (cell.day >= 1 && cell.day <= dim) {
            cell.btn.clearFlag(lv.OBJ_FLAG_HIDDEN);
            cell.lbl.setText(String(cell.day));
        } else {
            cell.btn.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
    selIdx = (todayIdx >= 0) ? todayIdx : firstWd;   // 1st of month when browsing
    navLbl.setText(MONTHS[M - 1] + " " + Y);
    navPrev.setHidden(Y <= 1900 && M <= 1);
    navNext.setHidden(Y >= 2100 && M >= 12);
    markDots();
    paintAll();
    showInfo(selIdx);
}

function goPrev() {
    if (Y <= 1900 && M <= 1) return;
    M--; if (M < 1) { M = 12; Y--; }
    refreshMonth();
}

function goNext() {
    if (Y >= 2100 && M >= 12) return;
    M++; if (M > 12) { M = 1; Y++; }
    refreshMonth();
}

// ===================== boot =====================
loadFest();
refreshMonth();
