// Timer - countdown timer (240x240 round, all-English)
// Presets 1M/5M/10M/30M/1H chips + fine-tune -30s/-10s/+10s/+30s (tap, hold to repeat)
// + START/PAUSE/RESET, HH:MM:SS display.
// Background resume via eos.config persistence + timestamp diff (same scheme as Stopwatch).
// Red lines: no arc / no border triple / radius<54 / no flex / anim<=6.
// EVENT_CLICKED broken in this fork -> use EVENT_PRESSED.

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Timer");

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hex(v) { return lv.color.hex(v); }

var BG = 0x12121A;
var ACCENT = 0x4A90D9;
var PAUSE_C = 0xE5A34D;
var DONE_C = 0x3FB950;
var IDLE_C = 0x8A8F98;
var WHITE = 0xFFFFFF;

var root = new lv.obj(view);
root.setSize(240, 240);
root.setPos(0, 0);
root.setStyleRadius(0, 0);
root.setStyleBgOpa(255, 0);
root.setStyleBgColor(hex(BG), 0);
root.setStylePadAll(0, 0);
root.setStyleBorderWidth(0, 0);
root.removeFlag(lv.OBJ_FLAG_CLICKABLE);
root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== status row (dot + text, centered) =====================
var statusDot = new lv.obj(root);
statusDot.setSize(8, 8);
statusDot.setStyleRadius(4, 0);
statusDot.setStyleBgColor(hex(IDLE_C), 0);
statusDot.setStyleBgOpa(255, 0);
statusDot.removeFlag(lv.OBJ_FLAG_CLICKABLE);
statusDot.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var statusTxt = new lv.label(root);
statusTxt.setStyleTextColor(hex(WHITE), 0);
statusTxt.setStyleTextOpa(160, 0);

function layoutStatusRow() {
    try { root.updateLayout(); } catch (e) {}
    var tw = statusTxt.getWidth();
    var x0 = Math.floor((240 - (8 + 6 + tw)) / 2);
    statusDot.setPos(x0, 46);
    statusTxt.setPos(x0 + 14, 36);
}

// ===================== time display =====================
var timeTxt = new lv.label(root);
timeTxt.setStyleTextColor(hex(WHITE), 0);
timeTxt.setStyleTextOpa(250, 0);
timeTxt.setFontSize(26);
timeTxt.setStyleTextLetterSpace(2, 0);

function layoutTime() {
    try { root.updateLayout(); } catch (e) {}
    var w1 = timeTxt.getWidth();
    timeTxt.setPos(Math.floor((240 - w1) / 2), 60);
}

// ===================== preset chips (1M 5M 10M 30M 1H) =====================
var PRESETS = [60, 300, 600, 1800, 3600];
var PRESET_TXT = ["1M", "5M", "10M", "30M", "1H"];
var chips = [];
for (var ci = 0; ci < 5; ci++) {
    var b = new lv.button(root);
    b.setSize(42, 26);
    b.setPos(7 + ci * 46, 100);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(36, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(6);
    var l = new lv.label(b);
    l.setSize(42, 26);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(12);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setStyleTextOpa(250, 0);
    l.setText(PRESET_TXT[ci]);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    chips.push({ btn: b, lbl: l });
    (function (idx) {
        b.addEventCb(function () { chipPress(idx); }, lv.EVENT_PRESSED, null);
    })(ci);
}

// ===================== fine-tune row (-30s -10s +10s +30s) =====================
var MICRO = [[-30, "-30s"], [-10, "-10s"], [10, "+10s"], [30, "+30s"]];
var microBtns = [];
for (var mi = 0; mi < 4; mi++) {
    (function (m0, step) {
        var b = new lv.button(root);
        b.setSize(44, 26);
        b.setPos(26 + m0 * 48, 140);
        b.setStyleRadius(999, 0);
        b.setStyleBgOpa(20, 0);
        b.setStyleBgColor(hex(WHITE), 0);
        b.setStylePadAll(0, 0);
        b.setStyleBorderWidth(0, 0);
        b.setExtClickArea(6);
        var l = new lv.label(b);
        l.setSize(44, 26);
        l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        l.setFontSize(12);
        l.setStyleTextColor(hex(WHITE), 0);
        l.setStyleTextOpa(250, 0);
        l.setText(MICRO[m0][1]);
        l.align(lv.ALIGN_CENTER, 0, 0);
        l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        microBtns.push({ btn: b, lbl: l });
        var pressed = false;
        b.addEventCb(function () { pressFx(b); microStep(step); pressed = true; }, lv.EVENT_PRESSED, null);
        b.addEventCb(function () { pressed = false; }, lv.EVENT_RELEASED, null);
        b.addEventCb(function () { pressed = false; }, lv.EVENT_PRESS_LOST, null);
        var ht = new lv.timer(function () { if (pressed) microStep(step); }, 280, null);
        ht.setRepeatCount(-1);
    })(mi, MICRO[mi][0]);
}

// ===================== buttons =====================
var btnRefs = {};
function makeBtn(x, label, key) {
    var b = new lv.button(root);
    b.setSize(96, 28);
    b.setPos(x, 180);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(36, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(6);
    var l = new lv.label(b);
    l.setSize(96, 28);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(12);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setStyleTextOpa(250, 0);
    l.setText(label);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(function () { pressFx(b); btnHandler(key); }, lv.EVENT_PRESSED, null);
    btnRefs[key] = { btn: b, lbl: l };
    return b;
}
makeBtn(20, "START", "start");
makeBtn(124, "RESET", "reset");

// ===================== state (background resume: eos.config + timestamp diff) =====================
function nowSec() {
    var t = eos.time.getNow();
    return ((t.year * 372 + t.month * 31 + t.day) * 86400) + (t.hour * 3600 + t.min * 60 + t.sec);
}

var _svRun = false, _svRemain = 0, _svSec = 0, _svDur = 300, _svIdx = 2, _svMode = "READY";
try { _svRun = eos.config.getBool("sw.running") === true; } catch (e) {}
try { _svRemain = eos.config.getNumber("sw.remaining") || 0; } catch (e) {}
try { _svSec = eos.config.getNumber("sw.sec") || 0; } catch (e) {}
try { _svDur = eos.config.getNumber("sw.duration") || 300; if (isNaN(_svDur) || _svDur < 1) { _svDur = 300; } } catch (e) {}
try { _svIdx = eos.config.getNumber("sw.idx"); if (isNaN(_svIdx)) { _svIdx = 2; } else if (_svIdx < -1) { _svIdx = -1; } else if (_svIdx > 4) { _svIdx = 4; } } catch (e) {}
try { var _m = eos.config.getStr("sw.mode"); if (_m === "RUN" || _m === "PAUSE") { _svMode = _m; } } catch (e) {}

var selIdx = _svIdx;
var duration = (selIdx >= 0 && selIdx <= 4) ? PRESETS[selIdx] : _svDur;
var remaining = 0;
var running = false;
var mode = "READY";

if (_svMode === "RUN") {
    running = true; mode = "RUN";
    remaining = _svRemain - (nowSec() - _svSec);
    if (remaining <= 0) { remaining = 0; running = false; mode = "DONE"; }
} else if (_svMode === "PAUSE") {
    remaining = _svRemain;
    mode = "PAUSE";
} else {
    remaining = duration;
    mode = "READY";
}

function saveState() {
    try { eos.config.setBool("app.background", running); } catch (e) {}
    try {
        eos.config.setNumber("sw.duration", duration);
        eos.config.setNumber("sw.remaining", remaining);
        eos.config.setBool("sw.running", running);
        eos.config.setNumber("sw.sec", nowSec());
        eos.config.setNumber("sw.idx", selIdx);
        eos.config.setStr("sw.mode", mode);
    } catch (e) {}
}

// ===================== UI update =====================
function fmt(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    return pad2(h) + ":" + pad2(m) + ":" + pad2(s);
}

function update() {
    timeTxt.setText(fmt(remaining));
    layoutTime();
}

function paintChips() {
    for (var i = 0; i < 5; i++) {
        var on = (i === selIdx);
        chips[i].btn.setStyleBgOpa(on ? 200 : 36, 0);
        chips[i].lbl.setStyleTextOpa(mode === "RUN" ? 90 : 250, 0);
    }
}

function setMode() {
    var m = mode;
    var txt = m === "RUN" ? "RUNNING" : m === "PAUSE" ? "PAUSED" : m === "DONE" ? "DONE" : "READY";
    statusTxt.setText(txt);
    layoutStatusRow();
    var col = m === "RUN" ? ACCENT : m === "PAUSE" ? PAUSE_C : m === "DONE" ? DONE_C : IDLE_C;
    statusDot.setStyleBgColor(hex(col), 0);
    statusDot.setStyleBgOpa(255, 0);
    if (m === "RUN" || m === "DONE") setBreath(true);
    else setBreath(false);
    btnRefs.start.lbl.setText(m === "RUN" ? "PAUSE" : "START");
    paintChips();
    paintMicro();
}

function paintMicro() {
    for (var i = 0; i < microBtns.length; i++) {
        microBtns[i].lbl.setStyleTextOpa(mode === "RUN" ? 90 : 250, 0);
    }
}

function chipPress(idx) {
    if (mode === "RUN") return;                 // running: presets locked
    pressFx(chips[idx].btn);
    selIdx = idx;
    duration = PRESETS[idx];
    remaining = duration;
    running = false;
    mode = "READY";
    setMode();
    update();
    saveState();
}

function microStep(step) {
    if (mode === "RUN") return;                 // running: fine-tune locked
    if (mode === "PAUSE") {
        remaining = Math.min(86400, Math.max(1, remaining + step));
        duration = remaining;                   // keep RESET in sync
    } else {
        duration = Math.min(86400, Math.max(1, duration + step));
        remaining = duration;
        running = false;
        mode = "READY";
    }
    selIdx = -1;                                // custom unless matching a preset
    for (var i = 0; i < PRESETS.length; i++) {
        if (duration === PRESETS[i]) { selIdx = i; break; }
    }
    setMode();
    update();
    saveState();
}

function btnHandler(key) {
    if (key === "start") {
        if (mode === "RUN") { mode = "PAUSE"; running = false; }
        else if (mode === "DONE") { mode = "RUN"; running = true; remaining = duration; }
        else { mode = "RUN"; running = true; }
    } else if (key === "reset") {
        running = false; mode = "READY"; remaining = duration;
    }
    saveState();
    setMode();
    update();
}

// ===================== effects =====================
var breathAnim = null;
function setBreath(on) {
    if (on && !breathAnim) {
        var a = new lv.anim();
        a.init(); a.setVar(statusDot);
        var dur = (mode === "DONE") ? 250 : 400;
        a.setValues(80, 255); a.setDuration(dur); a.setPlaybackTime(dur); a.setRepeatCount(65535);
        a.setPathCb(4);
        a.setCustomExecCb(function (an, v) {
            if (running || mode === "DONE") statusDot.setStyleBgOpa(Math.round(v), 0);
            else statusDot.setStyleBgOpa(255, 0);
        });
        breathAnim = a;
    } else if (!on) {
        breathAnim = null;
        statusDot.setStyleBgOpa(255, 0);
    }
}

function pressFx(b) {
    b.setStyleTransformScale(232, 0);
    var a = new lv.anim();
    a.init(); a.setVar(b);
    a.setValues(232, 256); a.setDuration(120);
    a.setCustomExecCb(function (an, v) { b.setStyleTransformScale(Math.round(v), 0); });
}

function doneFx() {
    var a = new lv.anim();
    a.init(); a.setVar(timeTxt);
    a.setValues(250, 80); a.setDuration(120); a.setPlaybackTime(120); a.setRepeatCount(5);
    a.setCustomExecCb(function (an, v) { timeTxt.setStyleTextOpa(Math.round(v), 0); });
}

// ===================== tick (250ms; decrement every 4th -> 1s) =====================
var _cnt = 0;
var tick = new lv.timer(function () {
    if (running) {
        _cnt++;
        if (_cnt % 4 === 0) {
            remaining--;
            if (remaining <= 0) {
                remaining = 0; running = false; mode = "DONE";
                setMode(); update(); doneFx(); saveState();
            } else {
                update();
                if (remaining % 30 === 0) saveState();   // persist every 30s
            }
        }
    }
}, 250, null);
tick.setRepeatCount(-1);

// ===================== boot =====================
setMode();
update();
if (mode === "DONE") { doneFx(); }          // finished while in background
saveState();
eos.console.log("[stopwatch] countdown v0.1 loaded");
