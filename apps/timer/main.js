// Timer - multi-countdown manager (240x240 round, all-English)
//
// Home   : numbered task list (3 per page). Tap a row to START / PAUSE /
//          RESUME / RESET (depends on state), tap "x" to delete.
// New    : three vertical wheels (HR / MIN / SEC) - scroll to pick, then OK.
//          The created task gets an id (#N) shown in a toast right away.
// Background: tasks persist in config.json ("countdown"). A Core service
//          (cos_service_countdown.c) polls the clock every 1s and relaunches
//          this app when a RUN task reaches its end timestamp, so a timer
//          still fires after the app was closed (same scheme as Alarm).
//
// Data contract (JS <-> Core service, same config.json):
//   countdown = [ { id, dur, remain, end, state } ]
//     id     : auto-increment number (shown as #N)
//     dur    : total seconds
//     remain : seconds left (last persisted)
//     end    : absolute end second (RTC-based) while RUN, else 0
//     state  : READY | RUN | PAUSE | DONE
//
// Red lines: no arc / no border / radius<54 / no flex / anim<=6.
// EVENT_CLICKED broken in this fork -> use EVENT_PRESSED.

var activity = cos.activity.current();
var view = cos.activity.getView(activity);
cos.activity.setTitle(activity, "Timer");

function pad2(n) { return (n < 10 ? "0" : "") + n; }
function hex(v) { return lv.color.hex(v); }

var BG = 0x12121A, WHITE = 0xFFFFFF, GREY = 0x9A9AA8,
    BLUE = 0x4C8DFF, ORANGE = 0xE5A34D, GREEN = 0x3FB950, RED = 0xE5484D;

/* ---------- time (days-from-civil, matches the Core service) ---------- */
function dateToSec(y, m, d, h, mi, s) {
    var yy = y - (m <= 2 ? 1 : 0);
    var era = Math.floor((yy >= 0 ? yy : yy - 399) / 400);
    var yoe = yy - era * 400;
    var mp = m + (m > 2 ? -3 : 9);
    var doy = Math.floor((153 * mp + 2) / 5) + d - 1;
    var doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
    return ((era * 146097 + doe - 719468) * 86400) + h * 3600 + mi * 60 + s;
}
function nowMs() {
    var t = cos.time.getNow();
    return dateToSec(t.year, t.month, t.day, t.hour, t.min, t.sec) * 1000 + (t.ms || 0);
}
function nowSec() {
    var t = cos.time.getNow();
    return dateToSec(t.year, t.month, t.day, t.hour, t.min, t.sec);
}
function fmt(sec) {
    var h = Math.floor(sec / 3600);
    var m = Math.floor((sec % 3600) / 60);
    var s = sec % 60;
    return pad2(h) + ":" + pad2(m) + ":" + pad2(s);
}

/* ---------- root ---------- */
var root = new lv.obj(view);
root.setSize(240, 240);
root.setPos(0, 0);
cos.roundClip(root);   /* round the background + black out the bezel corners */
root.setStyleBgOpa(255, 0);
root.setStyleBgColor(hex(BG), 0);
root.setStylePadAll(0, 0);
root.setStyleBorderWidth(0, 0);
root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
root.removeFlag(lv.OBJ_FLAG_CLICKABLE);

var homeC = new lv.obj(view);
homeC.setSize(240, 210); homeC.setPos(0, 30);
homeC.setStyleBgOpa(0, 0);
homeC.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
homeC.removeFlag(lv.OBJ_FLAG_CLICKABLE);

var editC = new lv.obj(view);
editC.setSize(240, 210); editC.setPos(0, 30);
editC.setStyleBgOpa(0, 0);
editC.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
editC.removeFlag(lv.OBJ_FLAG_CLICKABLE);
editC.addFlag(lv.OBJ_FLAG_HIDDEN);

/* toast drawn last -> sits on top of both pages */
var toastLbl = new lv.label(view);
toastLbl.setSize(240, 24); toastLbl.setPos(0, 96);
toastLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
toastLbl.setFontSize(13);
toastLbl.setStyleTextColor(hex(GREEN), 0);
toastLbl.setStyleTextOpa(240, 0);
toastLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
toastLbl.addFlag(lv.OBJ_FLAG_HIDDEN);

/* ---------- small button ---------- */
function smallBtn(parent, x, y, w, h, label, opa, color, cb) {
    var b = new lv.button(parent);
    b.setSize(w, h); b.setPos(x, y);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(opa, 0);
    b.setStyleBgColor(hex(color), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(8);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    var l = new lv.label(b);
    l.setText(label);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(11);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setStyleTextOpa(230, 0);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(cb, lv.EVENT_PRESSED, null);
    return { btn: b, lbl: l };
}

/* ---------- data ---------- */
var tasks = [];
var nextId = 1;

function loadTasks() {
    try {
        var s = cos.config.getStr("countdown");
        if (s) { tasks = JSON.parse(s); if (!Array.isArray(tasks)) tasks = []; }
    } catch (e) { tasks = []; }
    nextId = 1;
    var dirty = false;
    for (var i = 0; i < tasks.length; i++) {
        var t = tasks[i];
        if (t.id >= nextId) nextId = t.id + 1;
        if (!t.dur || t.dur <= 0) t.dur = 1;
        if (t.state === "RUN") {
            var r = Math.ceil((t.end * 1000 - nowMs()) / 1000);   /* ms-accurate resume */
            if (r <= 0) { t.state = "DONE"; t.end = 0; t.remain = 0; dirty = true; }
            else t.remain = r;
        }
        if (typeof t.remain !== "number" || isNaN(t.remain)) t.remain = t.dur;
    }
    if (dirty) saveTasks();
}
function saveTasks() { try { cos.config.setStr("countdown", JSON.stringify(tasks)); } catch (e) {} }

function stateCol(st) {
    return st === "RUN" ? BLUE : st === "PAUSE" ? ORANGE : st === "DONE" ? RED : GREY;
}
function stateTxt(st) {
    return st === "RUN" ? "RUNNING" : st === "PAUSE" ? "PAUSED" : st === "DONE" ? "DONE" : "READY";
}
function taskRemain(t) {
    if (t.state === "RUN") return Math.max(0, Math.ceil((t.end * 1000 - nowMs()) / 1000));
    return t.remain;
}

/* ---------- toast ---------- */
var _toastUntil = 0;
function showToast(s, col) {
    toastLbl.setText(s);
    toastLbl.setStyleTextColor(hex(col || GREEN), 0);
    toastLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
    _toastUntil = nowSec() + 2;
}

/* ================= HOME (task list) ================= */
var page = 0;
var rowRefs = [];
var emptyTtl, emptySub, prevBtn, pageLbl, nextBtn, addBtn;

function buildHome() {
    emptyTtl = new lv.label(homeC);
    emptyTtl.setSize(240, 26); emptyTtl.setPos(0, 76);
    emptyTtl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    emptyTtl.setText("No timers");
    emptyTtl.setFontSize(20);
    emptyTtl.setStyleTextColor(hex(WHITE), 0);
    emptyTtl.setStyleTextOpa(220, 0);
    emptyTtl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    emptySub = new lv.label(homeC);
    emptySub.setSize(240, 18); emptySub.setPos(0, 110);
    emptySub.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    emptySub.setText("Tap + NEW to add");
    emptySub.setFontSize(13);
    emptySub.setStyleTextColor(hex(GREY), 0);
    emptySub.setStyleTextOpa(150, 0);
    emptySub.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    // 按钮组屏坐标 y190..214(圆内 x46.6..193.4);激进内移:组 x48..184 居中于圆心 x120
    prevBtn = smallBtn(homeC, 48, 160, 22, 22, "<", 40, WHITE,
        function () { if (page > 0) { page--; paintHome(); } });
    pageLbl = new lv.label(homeC);
    pageLbl.setSize(28, 20); pageLbl.setPos(72, 162);
    pageLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    pageLbl.setFontSize(12);
    pageLbl.setStyleTextColor(hex(WHITE), 0);
    pageLbl.setStyleTextOpa(170, 0);
    pageLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    nextBtn = smallBtn(homeC, 102, 160, 22, 22, ">", 40, WHITE,
        function () { page++; paintHome(); });
    addBtn = smallBtn(homeC, 128, 156, 56, 30, "+NEW", 255, BLUE,
        function () { openNew(); });   // 原 y180 宽 98 完全出圆；x146..182 → 激进内移 x128..184
    addBtn.lbl.setFontSize(14);
}

function paintHome() {
    for (var i = 0; i < rowRefs.length; i++) {
        try { rowRefs[i].row.delete(); } catch (e) {}
        try { rowRefs[i].delBtn.delete(); } catch (e) {}
    }
    rowRefs = [];

    var n = tasks.length;
    var tp = Math.max(1, Math.ceil(n / 3));
    if (page >= tp) page = tp - 1;
    if (page < 0) page = 0;

    if (n === 0) {
        emptyTtl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        emptySub.removeFlag(lv.OBJ_FLAG_HIDDEN);
    } else {
        emptyTtl.addFlag(lv.OBJ_FLAG_HIDDEN);
        emptySub.addFlag(lv.OBJ_FLAG_HIDDEN);
    }

    var start = page * 3;
    for (var i = 0; i < 3; i++) {
        var idx = start + i;
        if (idx >= n) break;
        var t = tasks[idx];
        (function (t, i) {
            var y = 2 + i * 52;                        // 行1/2/3: y2/54/106, 底156(屏186) 与按钮组 y160 留 4px
            var row = new lv.obj(homeC);
            row.setSize(160, 50); row.setPos(40, y);   // x40..200：行1 顶部 y32 圆界 38.4..201.6，四角留 ≥1px
            row.setStyleBgOpa(0, 0);                   // 无矩形托底：文字直接显示
            row.setStyleBgColor(hex(WHITE), 0);
            row.setStyleRadius(0, 0);
            row.setStylePadAll(0, 0);
            row.setStyleBorderWidth(0, 0);
            row.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            row.addEventCb(function () { tapTask(t); }, lv.EVENT_PRESSED, null);

            var numLbl = new lv.label(row);
            numLbl.setSize(30, 14); numLbl.setPos(10, 4);
            numLbl.setText("#" + t.id);
            numLbl.setFontSize(13);
            numLbl.setStyleTextColor(hex(BLUE), 0);
            numLbl.setStyleTextOpa(240, 0);
            numLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

            var timeLbl = new lv.label(row);
            timeLbl.setSize(150, 30); timeLbl.setPos(10, 16);
            timeLbl.setFontSize(22);
            timeLbl.setStyleTextColor(hex(WHITE), 0);
            timeLbl.setStyleTextOpa(255, 0);
            timeLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

            var stateLbl = new lv.label(row);
            stateLbl.setSize(90, 14); stateLbl.setPos(76, 4);
            stateLbl.setFontSize(11);
            stateLbl.setStyleTextColor(hex(GREY), 0);
            stateLbl.setStyleTextOpa(220, 0);
            stateLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

            /* delete button lives on homeC (outside the row) so taps never bubble */
            var delBtn = new lv.button(homeC);
            delBtn.setSize(20, 20); delBtn.setPos(146, y + 14);
            delBtn.setStyleRadius(10, 0);
            delBtn.setStyleBgOpa(16, 0);
            delBtn.setStyleBgColor(hex(WHITE), 0);
            delBtn.setStylePadAll(0, 0);
            delBtn.setStyleBorderWidth(0, 0);
            delBtn.setExtClickArea(8);
            delBtn.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            var dl = new lv.label(delBtn);
            dl.setText("x");
            dl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
            dl.setFontSize(13);
            dl.setStyleTextColor(hex(RED), 0);
            dl.align(lv.ALIGN_CENTER, 0, 0);
            dl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            delBtn.addEventCb(function () { delTask(t); }, lv.EVENT_PRESSED, null);

            rowRefs.push({ row: row, delBtn: delBtn, numLbl: numLbl, timeLbl: timeLbl, stateLbl: stateLbl });
        })(t, i);
    }

    pageLbl.setText((page + 1) + "/" + tp);
    prevBtn.btn.setStyleBgOpa(page > 0 ? 40 : 12, 0);
    prevBtn.lbl.setStyleTextOpa(page > 0 ? 230 : 90, 0);
    nextBtn.btn.setStyleBgOpa(page < tp - 1 ? 40 : 12, 0);
    nextBtn.lbl.setStyleTextOpa(page < tp - 1 ? 230 : 90, 0);
    updateRows();
}

function updateRows() {
    var start = page * 3;
    for (var i = 0; i < rowRefs.length; i++) {
        var idx = start + i;
        if (idx >= tasks.length) break;
        var t = tasks[idx];
        var r = rowRefs[i];
        r.numLbl.setText("#" + t.id);
        r.timeLbl.setText(fmt(taskRemain(t)));
        r.stateLbl.setText(stateTxt(t.state));
        r.stateLbl.setStyleTextColor(hex(stateCol(t.state)), 0);
    }
}

/* ================= actions ================= */
function tapTask(t) {
    if (t.state === "RUN") {
        t.state = "PAUSE"; t.end = 0;
    } else if (t.state === "PAUSE" || t.state === "READY") {
        t.state = "RUN"; t.end = Math.ceil(nowMs() / 1000) + Math.max(1, t.remain);
    } else {                                   /* DONE -> reset */
        t.state = "READY"; t.remain = t.dur; t.end = 0;
    }
    saveTasks();
    updateRows();
}

function delTask(t) {
    for (var i = 0; i < tasks.length; i++) {
        if (tasks[i] === t) { tasks.splice(i, 1); break; }
    }
    saveTasks();
    paintHome();
}

/* ================= NEW (wheel editor) ================= */
var wheels = [];

function makeWheel(x, count) {
    var ITEM_H = 32, VIEW_H = 120, CENTER = 60, PAD = 44;
    var box = new lv.obj(editC);
    box.setSize(58, VIEW_H); box.setPos(x, 22);   // y22：屏 y52..172，圆边界 y52 处 x21..219，滚轮 x27..213 两侧各留 6px
    box.setStyleBgOpa(0, 0);
    box.setStyleRadius(12, 0);
    box.setStylePadAll(0, 0);
    box.setStyleBorderWidth(0, 0);
    box.addFlag(lv.OBJ_FLAG_SCROLLABLE);
    box.addFlag(lv.OBJ_FLAG_CLICKABLE);
    box.setScrollDir(lv.DIR_VER);
    box.setScrollbarMode(lv.SCROLLBAR_MODE_OFF);

    /* spacer defines the scrollable range (PAD top/bottom keeps ends centered) */
    var spacer = new lv.obj(box);
    spacer.setSize(58, PAD + count * ITEM_H + PAD);
    spacer.setPos(0, 0);
    spacer.setStyleBgOpa(0, 0);
    spacer.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    spacer.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    /* 7-slot moving window */
    var labels = [];
    for (var i = 0; i < 7; i++) {
        var l = new lv.label(box);
        l.setSize(58, ITEM_H);
        l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        l.setFontSize(13);
        l.setStyleTextColor(hex(WHITE), 0);
        l.setStyleTextOpa(120, 0);
        l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        l.removeFlag(lv.OBJ_FLAG_CLICKABLE);
        labels.push(l);
    }

    var maxScroll = PAD + count * ITEM_H + PAD - VIEW_H;
    var lastN = [-1, -1, -1, -1, -1, -1, -1];
    var w = { box: box, labels: labels, count: count, value: 0, maxScroll: maxScroll };

    function paint(s) {
        var v = Math.round(s / ITEM_H);
        v = Math.max(0, Math.min(count - 1, v));
        w.value = v;
        for (var i = 0; i < 7; i++) {
            var n = v - 3 + i;
            var l = labels[i];
            if (n < 0 || n >= count) {
                if (lastN[i] !== -1) { l.addFlag(lv.OBJ_FLAG_HIDDEN); lastN[i] = -1; }
                continue;
            }
            l.removeFlag(lv.OBJ_FLAG_HIDDEN);
            l.setPos(0, PAD + n * ITEM_H);
            if (lastN[i] !== n) {
                lastN[i] = n;
                l.setText(pad2(n));
                var cen = (i === 3);
                l.setFontSize(cen ? 24 : 13);
                l.setStyleTextOpa(cen ? 255 : 120, 0);
            }
        }
    }
    function snap() {
        var s = box.getScrollY();
        var v = Math.round(s / ITEM_H);
        v = Math.max(0, Math.min(count - 1, v));
        var target = Math.max(0, Math.min(maxScroll, v * ITEM_H));
        box.scrollToY(target, 0);
        paint(box.getScrollY());
    }
    box.addEventCb(function () { paint(box.getScrollY()); }, lv.EVENT_SCROLL, null);
    box.addEventCb(function () { snap(); }, lv.EVENT_SCROLL_END, null);

    w.setValue = function (v) {
        var target = Math.max(0, Math.min(maxScroll, v * ITEM_H));
        box.scrollToY(target, 0);
        paint(box.getScrollY());
    };
    paint(0);
    return w;
}

function buildEdit() {
    var cols = [["HR", 24], ["MIN", 24], ["SEC", 24]];
    var wx = [27, 91, 155];   // 整体居中于 x=120（总宽186），右缘 213 < 圆右界
    for (var i = 0; i < 3; i++) {
        var tl = new lv.label(editC);
        tl.setSize(58, 12); tl.setPos(wx[i], 0);
        tl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        tl.setText(cols[i][0]);
        tl.setFontSize(10);
        tl.setStyleTextColor(hex(GREY), 0);
        tl.setStyleTextOpa(170, 0);
        tl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

        /* center highlight bar (under the wheel) */
        var bar = new lv.obj(editC);
        bar.setSize(58, 32); bar.setPos(wx[i], 66);   // 随 VIEW_H 120 中心对齐（box.y22+CENTER60-ITEM_H/2）
        bar.setStyleBgOpa(22, 0);
        bar.setStyleBgColor(hex(WHITE), 0);
        bar.setStyleRadius(12, 0);
        bar.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        bar.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    }
    wheels.push(makeWheel(wx[0], 24));     /* hours */
    wheels.push(makeWheel(wx[1], 60));     /* minutes */
    wheels.push(makeWheel(wx[2], 60));     /* seconds */

    // 按钮屏 y174..200（滚轮底 172 下 2px）；OK 右缘 204 < 圆 y200 处右界 209，留空隙
    smallBtn(editC, 36, 144, 68, 26, "CANCEL", 20, WHITE,
        function () { paintHome(); showHome(); });
    smallBtn(editC, 136, 144, 68, 26, "OK", 255, BLUE,
        function () { okNew(); });
}

function openNew() {
    wheels[0].setValue(0);   /* 0h */
    wheels[1].setValue(5);   /* 5m */
    wheels[2].setValue(0);   /* 0s */
    showEdit();
}

function okNew() {
    var total = wheels[0].value * 3600 + wheels[1].value * 60 + wheels[2].value;
    if (total <= 0) { showToast("Set a time > 0", RED); return; }
    var t = { id: nextId++, dur: total, remain: total, end: 0, state: "READY" };
    tasks.push(t);
    saveTasks();
    page = Math.ceil(tasks.length / 3) - 1;
    paintHome();
    showHome();
    showToast("Timer #" + t.id + " created", GREEN);
}

/* ================= page switching ================= */
function setTitle(s) { try { cos.activity.setTitle(activity, s); } catch (e) {} }
function showHome() {
    homeC.removeFlag(lv.OBJ_FLAG_HIDDEN);
    editC.addFlag(lv.OBJ_FLAG_HIDDEN);
    setTitle("Timer");
    paintHome();
}
function showEdit() {
    homeC.addFlag(lv.OBJ_FLAG_HIDDEN);
    editC.removeFlag(lv.OBJ_FLAG_HIDDEN);
    setTitle("New Timer");
}

/* ================= tick (250ms: <1s keeps C ms-interp continuous) ================= */
var tickTimer = new lv.timer(function () {
    var dirty = false, doneId = 0;
    for (var i = 0; i < tasks.length; i++) {
        var t = tasks[i];
        if (t.state !== "RUN") continue;
        var r = Math.ceil((t.end * 1000 - nowMs()) / 1000);
        if (r <= 0) {
            t.state = "DONE"; t.end = 0; t.remain = 0;
            dirty = true; doneId = t.id;
        } else {
            t.remain = r;
            if (t.remain % 5 === 0) dirty = true;   /* persist every 5s */
        }
    }
    if (doneId) {
        saveTasks();
        showToast("Timer #" + doneId + " finished", RED);
        flashDone(doneId);
    } else if (dirty) {
        saveTasks();
    }
    updateRows();
    if (_toastUntil && nowSec() >= _toastUntil) { toastLbl.addFlag(lv.OBJ_FLAG_HIDDEN); _toastUntil = 0; }
}, 250, null);
tickTimer.setRepeatCount(-1);

/* brief blink of the just-finished row's time (light touch) */
function flashDone(doneId) {
    var start = page * 3;
    for (var j = 0; j < rowRefs.length; j++) {
        var idx = start + j;
        if (idx < tasks.length && tasks[idx].id === doneId) {
            (function (r) {
                var a = new lv.anim();
                a.init(); a.setVar(r.timeLbl);
                a.setValues(255, 70); a.setDuration(150); a.setPlaybackTime(150); a.setRepeatCount(5);
                a.setCustomExecCb(function (an, v) {
                    try { r.timeLbl.setStyleTextOpa(Math.round(v), 0); } catch (e) {}
                });
            })(rowRefs[j]);
            break;
        }
    }
}

/* ---------- boot ---------- */
loadTasks();
buildHome();
buildEdit();
paintHome();
showHome();
cos.console.log("[timer] multi-countdown v0.2 loaded");
