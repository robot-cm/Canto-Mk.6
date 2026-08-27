/**
 * Alarm - multi-alarm manager (240x240 round, all-English)
 *
 * Features:
 *  - Multiple alarms (list + add/edit/delete), repeat count (INF=-1 or xN),
 *    weekday multi-select (MON..SUN), on/off per alarm.
 *  - Ring: white-screen flash in grouped intervals 0.8s/0.9s/0.6s/0.4s
 *    (on/off alternation), black OFF button + purple SNOOZE (5 min) button.
 *  - No buzzer on this hardware -> visual ring only.
 *  - Background trigger: a Core service (eos_service_alarm.c) polls the
 *    system clock every 1s, reads this app's config.json ("alarms" field)
 *    and relaunches this app when an alarm is due, so it fires even after
 *    the app was closed. Ringing UI itself lives here.
 *
 * Data contract (JS <-> Core service, same config.json file):
 *   alarms = [ { id, h, m, days, rep, on, lf, note?, tmp? } ]
 *     days : bitmask bit0=MON ... bit6=SUN ; 0 = daily
 *     rep  : -1 = infinite ; N>0 = remaining rings (decremented here)
 *     lf   : last ring minute key YYYYMMDDHHMM (written here on ring start)
 *     note : optional text note (entered via the system round keyboard)
 *     tmp  : temporary snooze alarm (auto-removed after it rings)
 *
 * Add/Edit is a 4-step wizard (TIME -> WEEKDAYS -> REPEAT -> NOTE) so each
 * field gets roomy spacing; NOTE uses eos.ime.open (the same round keyboard
 * as the Wi-Fi password page).
 *
 * Red lines: no arc / no border / radius<54 / no flex / anim<=6.
 * EVENT_CLICKED broken in this fork -> use EVENT_PRESSED.
 */
var activity = eos.activity.current();
var view = eos.activity.getView(activity);

/* ---------- palette ---------- */
var COL_BG = 0x12121A, COL_WHITE = 0xFFFFFF, COL_GRAY = 0x9A9AA8,
    COL_BLUE = 0x4C8DFF, COL_PURPLE = 0x9C27B0, COL_RED = 0xE5484D;

function hex(v) { return lv.color.hex(v); }
function pad2(n) { return (n < 10 ? "0" : "") + n; }

var WDAYS = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"];
var RING_SEQ = [ { w: true, ms: 800 }, { w: false, ms: 900 },
                 { w: true, ms: 600 }, { w: false, ms: 400 } ];

/* ---------- data ---------- */
var alarms = [];
var nextId = 0;

function loadAlarms() {
    try {
        var s = eos.config.getStr("alarms");
        if (s) { alarms = JSON.parse(s); if (!Array.isArray(alarms)) alarms = []; }
    } catch (e) { alarms = []; }
    nextId = 0;
    for (var i = 0; i < alarms.length; i++) {
        if (alarms[i].id >= nextId) nextId = alarms[i].id + 1;
    }
}
function saveAlarms() {
    try { eos.config.setStr("alarms", JSON.stringify(alarms)); } catch (e) {}
}
function dayMatch(d, dow) { if (!d) return true; return (d & (1 << ((dow + 6) % 7))) !== 0; }
function daysLabel(d) {
    if (!d) return "Daily";
    if (d === 127) return "All days";
    var s = "";
    for (var i = 0; i < 7; i++) {
        if (d & (1 << i)) { if (s) s += " "; s += WDAYS[i]; }
    }
    return s;
}
function repLabel(r) { return r < 0 ? "INF" : "x" + r; }
function newAlarm() {
    var t = eos.time.getNow();
    var nm = (t.min + 10) % 60;
    var nh = (t.hour + Math.floor((t.min + 10) / 60)) % 24;
    return { id: nextId++, h: nh, m: nm, days: 0, rep: -1, on: true, lf: 0 };
}

/* ---------- state ---------- */
var page = 0;
var editing = null;                 /* { a, isNew } */
var editH = 7, editM = 0, editDays = 0, editRep = -1;
var editStep = 0, editNote = "";
var ringAlarm = null;
var ringTicks = 0, ringSeqIdx = 0, ringSeqElapsed = 0;
var tickCount = 0;

/* ---------- root ---------- */
/* Note: the activity view wrapper has no removeFlag() — do not touch it.
 * Children below manage their own flags. */
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

var ringC = new lv.obj(view);
ringC.setSize(240, 240); ringC.setPos(0, 0);
ringC.setStyleBgOpa(0, 0);
ringC.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
ringC.removeFlag(lv.OBJ_FLAG_CLICKABLE);
ringC.addFlag(lv.OBJ_FLAG_HIDDEN);

function setTitle(s) { try { eos.activity.setTitle(activity, s); } catch (e) {} }

/* ---------- small button helper ---------- */
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
    l.setFontSize(12);
    l.setStyleTextColor(hex(COL_WHITE), 0);
    l.setStyleTextOpa(230, 0);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(cb, lv.EVENT_PRESSED, null);
    return { btn: b, lbl: l };
}

/* ================= HOME (list) ================= */
var emptyTtl, emptySub, prevBtn, pageLbl, nextBtn, addBtn;
var rowBoxes = [];

function buildHome() {
    emptyTtl = new lv.label(homeC);
    emptyTtl.setSize(240, 24); emptyTtl.setPos(0, 78);
    emptyTtl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    emptyTtl.setText("No alarms");
    emptyTtl.setFontSize(20);
    emptyTtl.setStyleTextColor(hex(COL_WHITE), 0);
    emptyTtl.setStyleTextOpa(220, 0);
    emptyTtl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    emptySub = new lv.label(homeC);
    emptySub.setSize(240, 16); emptySub.setPos(0, 108);
    emptySub.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    emptySub.setText("Tap + to add");
    emptySub.setFontSize(12);
    emptySub.setStyleTextColor(hex(COL_GRAY), 0);
    emptySub.setStyleTextOpa(150, 0);
    emptySub.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    // 按钮组屏坐标 y190..214：圆内可用 x46.6..193.4；激进内移：组 x52..180 居中于圆心 x120
    prevBtn = smallBtn(homeC, 52, 160, 22, 22, "<", 40, COL_WHITE,
        function () { if (page > 0) { page--; paintHome(); } });
    pageLbl = new lv.label(homeC);
    pageLbl.setSize(24, 20); pageLbl.setPos(76, 162);
    pageLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    pageLbl.setFontSize(12);
    pageLbl.setStyleTextColor(hex(COL_WHITE), 0);
    pageLbl.setStyleTextOpa(170, 0);
    pageLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    nextBtn = smallBtn(homeC, 102, 160, 22, 22, ">", 40, COL_WHITE,
        function () { paintHome(); });
    addBtn = smallBtn(homeC, 128, 156, 52, 30, "+", 255, COL_BLUE,
        function () { openEdit(newAlarm(), true); });   // 原 x210 完全在圆外；x141..175 → 激进内移 x128..180
}

function paintHome() {
    for (var i = 0; i < rowBoxes.length; i++) {
        if (rowBoxes[i]) { try { rowBoxes[i].delete(); } catch (e) {} }
    }
    rowBoxes = [];

    var n = alarms.length;
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
        var a = alarms[idx];
        (function (a, i) {
            var y = 2 + i * 52;                        // 行1/2/3: y2/54/106, 底156(屏186) 与按钮组 y160 留 4px
            var row = new lv.obj(homeC);
            row.setSize(146, 50); row.setPos(40, y);   // x40..186：行1 顶部 y32 圆界 38.4..201.6，四角留 ≥1px（原 x0 行左缘被圆裁 38px）
            row.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            row.addEventCb(function () { openEdit(a, false); }, lv.EVENT_PRESSED, null);

            var tl = new lv.label(row);
            var note = a.note || "";
            if (note.length > 14) note = note.slice(0, 14) + "\u2026";
            if (note) {
                /* 有备注:备注 22px(上限),时间 20px */
                var nl = new lv.label(row);
                nl.setText(note);
                nl.setFontSize(22);
                nl.setStyleTextColor(hex(COL_WHITE), 0);
                nl.setStyleTextOpa(255, 0);
                nl.setPos(22, 0);
                nl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
                tl.setText(pad2(a.h) + ":" + pad2(a.m));
                tl.setFontSize(20);
                tl.setStyleTextColor(hex(COL_WHITE), 0);
                tl.setStyleTextOpa(255, 0);
                tl.setPos(22, 26);
                tl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            } else {
                tl.setText(pad2(a.h) + ":" + pad2(a.m));
                tl.setFontSize(26);   // jbm_26:无备注时主时间取最大档
                tl.setStyleTextColor(hex(COL_WHITE), 0);
                tl.setStyleTextOpa(255, 0);
                tl.setPos(22, 2);
                tl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            }

            var sl = new lv.label(row);
            sl.setText(daysLabel(a.days) + "  " + repLabel(a.rep));
            sl.setFontSize(10);
            sl.setStyleTextColor(hex(COL_GRAY), 0);
            sl.setStyleTextOpa(170, 0);
            sl.setPos(note ? 88 : 22, note ? 30 : 36);
            sl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

            var sep = new lv.obj(row);
            sep.setSize(146, 1); sep.setPos(0, 49);
            sep.setStyleBgOpa(15, 0);
            sep.setStyleBgColor(hex(COL_WHITE), 0);
            sep.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

            /* on/off switch: outside the row so it never bubbles to row press */
            var tg = new lv.button(homeC);
            tg.setSize(34, 24); tg.setPos(136, y + 16);   // 内移：右缘 170 与行对齐；原 x174..208 第三行(y 屏 152..176)出圆 24px
            tg.setStyleRadius(10, 0);
            tg.setStylePadAll(0, 0);
            tg.setStyleBorderWidth(0, 0);
            tg.setExtClickArea(8);
            tg.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            var tl2 = new lv.label(tg);
            tl2.setText(a.on ? "ON" : "OFF");
            tl2.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
            tl2.setFontSize(12);
            tl2.align(lv.ALIGN_CENTER, 0, 0);
            tl2.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            if (a.on) {
                tg.setStyleBgOpa(255, 0); tg.setStyleBgColor(hex(COL_BLUE), 0);
                tl2.setStyleTextColor(hex(COL_WHITE), 0); tl2.setStyleTextOpa(255, 0);
            } else {
                tg.setStyleBgOpa(18, 0); tg.setStyleBgColor(hex(COL_WHITE), 0);
                tl2.setStyleTextColor(hex(COL_GRAY), 0); tl2.setStyleTextOpa(230, 0);
            }
            tg.addEventCb(function () { a.on = !a.on; saveAlarms(); paintHome(); }, lv.EVENT_PRESSED, null);

            rowBoxes.push(row);
            rowBoxes.push(tg);
        })(a, i);
    }

    pageLbl.setText((page + 1) + "/" + tp);
    prevBtn.btn.setStyleBgOpa(page > 0 ? 40 : 12, 0);
    prevBtn.lbl.setStyleTextOpa(page > 0 ? 230 : 90, 0);
    nextBtn.btn.setStyleBgOpa(page < tp - 1 ? 40 : 12, 0);
    nextBtn.lbl.setStyleTextOpa(page < tp - 1 ? 230 : 90, 0);
}

/* ================= EDIT (4-step wizard) =================
 * TIME -> WEEKDAYS -> REPEAT -> NOTE, one page at a time so each field
 * gets roomy spacing on the round screen. NOTE opens the system round
 * keyboard (eos.ime.open, same as the Wi-Fi password page).
 * Bottom nav: BACK / NEXT; step0 BACK=CANCEL; step3 NEXT=SAVE;
 * DEL (edit mode only, on step 0) sits top-right. */
var stepTtl;
var hVal, colonL, mVal, hUp, hDn, mUp, mDn;
var dayChips = [], allBtn, noneBtn;
var rptSub, rptVal, rptDn, rptUp;
var noteHint, noteBox, noteLbl, noteSum;
var navBackBtn, navNextBtn, navDelBtn;

function buildEdit() {
    stepTtl = new lv.label(editC);
    stepTtl.setSize(240, 14); stepTtl.setPos(0, 0);
    stepTtl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    stepTtl.setFontSize(9);
    stepTtl.setStyleTextColor(hex(COL_GRAY), 0);
    stepTtl.setStyleTextOpa(170, 0);
    stepTtl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    /* ---- step 1: TIME (HH:MM)（整体下移 10px 向圆心靠拢，圆屏 240x240 圆心 120,120） ---- */
    hVal = new lv.label(editC);
    hVal.setSize(56, 44); hVal.setPos(48, 34);
    hVal.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    hVal.setFontSize(24);
    hVal.setStyleTextColor(hex(COL_WHITE), 0);
    hVal.setStyleTextOpa(255, 0);
    hVal.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    colonL = new lv.label(editC);
    colonL.setSize(12, 44); colonL.setPos(114, 34);
    colonL.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    colonL.setText(":");
    colonL.setFontSize(24);
    colonL.setStyleTextColor(hex(COL_WHITE), 0);
    colonL.setStyleTextOpa(255, 0);
    colonL.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    mVal = new lv.label(editC);
    mVal.setSize(56, 44); mVal.setPos(136, 34);
    mVal.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    mVal.setFontSize(24);
    mVal.setStyleTextColor(hex(COL_WHITE), 0);
    mVal.setStyleTextOpa(255, 0);
    mVal.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    hUp = smallBtn(editC, 56, 82, 40, 24, "+", 20, COL_WHITE,
        function () { editH = (editH + 1) % 24; paintEdit(); });
    hDn = smallBtn(editC, 56, 110, 40, 24, "-", 20, COL_WHITE,
        function () { editH = (editH + 23) % 24; paintEdit(); });
    mUp = smallBtn(editC, 144, 82, 40, 24, "+", 20, COL_WHITE,
        function () { editM = (editM + 1) % 60; paintEdit(); });
    mDn = smallBtn(editC, 144, 110, 40, 24, "-", 20, COL_WHITE,
        function () { editM = (editM + 59) % 60; paintEdit(); });

    /* ---- step 2: WEEKDAYS（48 宽 chips；两行各自居中，右缘 219 进圆内；原行0 右缘 226 圆外） ---- */
    for (var i = 0; i < 7; i++) {
        (function (i) {
            var row = i < 4 ? 0 : 1;
            var b = new lv.button(editC);
            b.setSize(48, 38);
            // 行0 四枚：总宽 198 → x0 21；行1 三枚：总宽 148 → x0 46；步长 50（48 宽 + 2 间距）
            // 行0 y30..68 / 行1 y68..106（垂直紧凑，为底部 ALL/NONE 与导航按钮让位）
            b.setPos(row === 0 ? 21 + i * 50 : 46 + (i - 4) * 50, row === 0 ? 30 : 68);
            b.setStyleRadius(13, 0);
            b.setStyleBgOpa(18, 0);
            b.setStyleBgColor(hex(COL_WHITE), 0);
            b.setStylePadAll(0, 0);
            b.setStyleBorderWidth(0, 0);
            b.setExtClickArea(4);
            b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            var l = new lv.label(b);
            l.setText(WDAYS[i]);
            l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
            l.setFontSize(11);
            l.setStyleTextColor(hex(COL_GRAY), 0);
            l.setStyleTextOpa(200, 0);
            l.align(lv.ALIGN_CENTER, 0, 0);
            l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            b.addEventCb(function () { editDays ^= (1 << i); paintEdit(); }, lv.EVENT_PRESSED, null);
            dayChips.push({ btn: b, lbl: l });
        })(i);
    }
    allBtn = smallBtn(editC, 76, 110, 36, 24, "ALL", 18, COL_WHITE,
        function () { editDays = 127; paintEdit(); });
    noneBtn = smallBtn(editC, 128, 110, 44, 24, "NONE", 18, COL_WHITE,
        function () { editDays = 0; paintEdit(); });

    /* ---- step 3: REPEAT（整体下移 10px 向圆心靠拢） ---- */
    rptSub = new lv.label(editC);
    rptSub.setSize(240, 14); rptSub.setPos(0, 26);
    rptSub.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    rptSub.setText("INF = infinite");
    rptSub.setFontSize(10);
    rptSub.setStyleTextColor(hex(COL_GRAY), 0);
    rptSub.setStyleTextOpa(170, 0);
    rptSub.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    rptVal = new lv.label(editC);
    rptVal.setSize(56, 44); rptVal.setPos(92, 58);
    rptVal.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    rptVal.setFontSize(28);
    rptVal.setStyleTextColor(hex(COL_WHITE), 0);
    rptVal.setStyleTextOpa(255, 0);
    rptVal.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    rptDn = smallBtn(editC, 44, 106, 40, 30, "-", 20, COL_WHITE,
        function () {
            if (editRep <= 1) editRep = -1;
            else editRep--;
            paintEdit();
        });
    rptUp = smallBtn(editC, 156, 106, 40, 30, "+", 20, COL_WHITE,
        function () { editRep = (editRep < 0 ? 1 : Math.min(999, editRep + 1)); paintEdit(); });

    /* ---- step 4: NOTE (system round keyboard；整体下移 10px 向圆心靠拢) ---- */
    noteHint = new lv.label(editC);
    noteHint.setSize(240, 14); noteHint.setPos(0, 26);
    noteHint.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    noteHint.setText("TAP TO EDIT NOTE");
    noteHint.setFontSize(10);
    noteHint.setStyleTextColor(hex(COL_GRAY), 0);
    noteHint.setStyleTextOpa(170, 0);
    noteHint.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    noteBox = new lv.button(editC);
    noteBox.setSize(168, 56); noteBox.setPos(36, 52);   // 居中 36..204，圆内留空隙
    noteBox.setStyleRadius(12, 0);
    noteBox.setStyleBgOpa(10, 0);
    noteBox.setStyleBgColor(hex(COL_WHITE), 0);
    noteBox.setStylePadAll(0, 0);
    noteBox.setStyleBorderWidth(0, 0);
    noteBox.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    noteLbl = new lv.label(noteBox);
    noteLbl.setSize(152, 40); noteLbl.setPos(8, 8);
    noteLbl.setFontSize(11);
    noteLbl.setStyleTextColor(hex(COL_GRAY), 0);
    noteLbl.setStyleTextOpa(180, 0);
    noteLbl.addFlag(lv.OBJ_FLAG_SCROLLABLE);
    noteLbl.setLongMode(lv.LABEL_LONG_SCROLL_CIRCULAR);
    noteBox.addEventCb(function () {
        eos.ime.open(function (text) {
            if (text !== undefined) { editNote = text; paintEdit(); }
        });
    }, lv.EVENT_PRESSED, null);

    noteSum = new lv.label(editC);
    noteSum.setSize(240, 16); noteSum.setPos(0, 112);   // 屏 y142..158，不与导航按钮 y170..198 重叠
    noteSum.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    noteSum.setFontSize(11);
    noteSum.setStyleTextColor(hex(COL_WHITE), 0);
    noteSum.setStyleTextOpa(220, 0);
    noteSum.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    /* ---- bottom nav（屏 y170..198，左右各留 ≥8px 圆内空隙；70x28 可容纳 CANCEL 六字符 20px 字） ---- */
    navBackBtn = smallBtn(editC, 40, 140, 70, 28, "CANCEL", 20, COL_WHITE, backStep);
    navNextBtn = smallBtn(editC, 130, 140, 70, 28, "NEXT", 255, COL_BLUE, nextStep);
    navDelBtn = smallBtn(editC, 158, 4, 36, 22, "DEL", 255, COL_RED,
        function () { delEdit(); });
}

function paintEdit() {
    stepTtl.setText(["TIME", "WEEKDAYS", "REPEAT", "NOTE"][editStep]);

    /* step 1 */
    hVal.setText(pad2(editH));
    mVal.setText(pad2(editM));

    /* step 2 */
    for (var i = 0; i < 7; i++) {
        var on = (editDays & (1 << i)) !== 0;
        var c = dayChips[i];
        if (on) {
            c.btn.setStyleBgOpa(200, 0); c.btn.setStyleBgColor(hex(COL_BLUE), 0);
            c.lbl.setStyleTextColor(hex(COL_WHITE), 0); c.lbl.setStyleTextOpa(255, 0);
        } else {
            c.btn.setStyleBgOpa(18, 0); c.btn.setStyleBgColor(hex(COL_WHITE), 0);
            c.lbl.setStyleTextColor(hex(COL_GRAY), 0); c.lbl.setStyleTextOpa(200, 0);
        }
    }
    allBtn.btn.setStyleBgOpa(editDays === 127 ? 200 : 18, 0);
    allBtn.btn.setStyleBgColor(hex(editDays === 127 ? COL_BLUE : COL_WHITE), 0);
    allBtn.lbl.setStyleTextColor(hex(editDays === 127 ? COL_WHITE : COL_GRAY), 0);
    noneBtn.btn.setStyleBgOpa(editDays === 0 ? 200 : 18, 0);
    noneBtn.btn.setStyleBgColor(hex(editDays === 0 ? COL_BLUE : COL_WHITE), 0);
    noneBtn.lbl.setStyleTextColor(hex(editDays === 0 ? COL_WHITE : COL_GRAY), 0);

    /* step 3 */
    rptVal.setText(repLabel(editRep));

    /* step 4 */
    if (editNote) {
        noteLbl.setText(editNote);
        noteLbl.setStyleTextColor(hex(COL_WHITE), 0);
        noteLbl.setStyleTextOpa(255, 0);
    } else {
        noteLbl.setText("No note");
        noteLbl.setStyleTextColor(hex(COL_GRAY), 0);
        noteLbl.setStyleTextOpa(160, 0);
    }
    try { noteLbl.scrollToX(0, 0); } catch (e) {}
    noteSum.setText(pad2(editH) + ":" + pad2(editM) + "  \u00b7  "
        + daysLabel(editDays) + "  \u00b7  " + repLabel(editRep));

    /* visibility per step */
    function show(objs, v) {
        for (var k = 0; k < objs.length; k++) {
            if (v) objs[k].removeFlag(lv.OBJ_FLAG_HIDDEN);
            else objs[k].addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
    var s2 = [];
    for (i = 0; i < 7; i++) s2.push(dayChips[i].btn);
    s2.push(allBtn.btn); s2.push(noneBtn.btn);
    show([hVal, colonL, mVal, hUp.btn, hDn.btn, mUp.btn, mDn.btn], editStep === 0);
    show(s2, editStep === 1);
    show([rptSub, rptVal, rptDn.btn, rptUp.btn], editStep === 2);
    show([noteHint, noteBox, noteLbl, noteSum], editStep === 3);

    /* bottom nav */
    navDelBtn.btn.addFlag(lv.OBJ_FLAG_HIDDEN);
    if (editStep === 0) {
        navBackBtn.lbl.setText("CANCEL");
        navNextBtn.lbl.setText("NEXT");
        if (editing && !editing.isNew) navDelBtn.btn.removeFlag(lv.OBJ_FLAG_HIDDEN);
    } else {
        navBackBtn.lbl.setText("BACK");
        navNextBtn.lbl.setText(editStep === 3 ? "SAVE" : "NEXT");
    }
}

function backStep() {
    if (editStep === 0) { editing = null; paintHome(); showHome(); }
    else { editStep--; paintEdit(); }
}
function nextStep() {
    if (editStep === 3) { saveEdit(); return; }
    editStep++;
    paintEdit();
}

function openEdit(a, isNew) {
    editing = { a: a, isNew: isNew };
    editH = a.h; editM = a.m; editDays = a.days; editRep = a.rep;
    editStep = 0; editNote = a.note || "";
    paintEdit();
    showEdit();
}

function saveEdit() {
    var a = editing.a;
    a.h = editH; a.m = editM; a.days = editDays; a.rep = editRep;
    a.note = editNote; a.lf = 0;
    if (editing.isNew) alarms.push(a);
    editing = null;
    saveAlarms();
    page = 0;
    paintHome();
    showHome();
}

function delEdit() {
    if (editing && !editing.isNew) {
        for (var i = 0; i < alarms.length; i++) {
            if (alarms[i] === editing.a) { alarms.splice(i, 1); break; }
        }
        saveAlarms();
    }
    editing = null;
    page = 0;
    paintHome();
    showHome();
}

/* ================= RING (white flash) ================= */
var flashBg, alarmTtl, ringTime, bar, offBtn, snoozeBtn;

function buildRing() {
    flashBg = new lv.obj(ringC);
    flashBg.setSize(240, 180); flashBg.setPos(0, 0);
    flashBg.setStyleBgOpa(255, 0);
    flashBg.setStyleBgColor(hex(0xFFFFFF), 0);
    flashBg.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    alarmTtl = new lv.label(ringC);
    alarmTtl.setSize(240, 14); alarmTtl.setPos(0, 38);
    alarmTtl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    alarmTtl.setText("ALARM");
    alarmTtl.setFontSize(10);
    alarmTtl.setStyleTextColor(hex(0x000000), 0);
    alarmTtl.setStyleTextOpa(200, 0);
    alarmTtl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    ringTime = new lv.label(ringC);
    ringTime.setSize(240, 40); ringTime.setPos(0, 62);
    ringTime.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    ringTime.setFontSize(36);
    ringTime.setStyleTextColor(hex(0x000000), 0);
    ringTime.setStyleTextOpa(255, 0);
    ringTime.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    /* fixed white bar below the flashing area (buttons stay visible) */
    bar = new lv.obj(ringC);
    bar.setSize(240, 60); bar.setPos(0, 180);
    bar.setStyleBgOpa(255, 0);
    bar.setStyleBgColor(hex(COL_WHITE), 0);
    bar.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

    offBtn = smallBtn(ringC, 44, 192, 62, 26, "OFF", 255, 0x000000,
        function () { stopRing(); });
    snoozeBtn = smallBtn(ringC, 134, 192, 62, 26, "SNOOZE", 255, COL_PURPLE,
        function () { snoozeRing(); });
}

function applyFlash(white) {
    if (white) {
        flashBg.setStyleBgColor(hex(0xFFFFFF), 0);
        ringTime.setStyleTextColor(hex(0x000000), 0);
        alarmTtl.setStyleTextColor(hex(0x000000), 0);
    } else {
        flashBg.setStyleBgColor(hex(0x000000), 0);
        ringTime.setStyleTextColor(hex(0xFFFFFF), 0);
        alarmTtl.setStyleTextColor(hex(0xFFFFFF), 0);
    }
}

function startRing(a) {
    ringAlarm = a;
    ringTicks = 0; ringSeqIdx = 0; ringSeqElapsed = 0;
    ringTime.setText(pad2(a.h) + ":" + pad2(a.m));
    applyFlash(true);
    showRing();
}

function stopRing() {
    if (ringAlarm) {
        if (ringAlarm.tmp) {          /* temporary snooze alarm: remove after ringing */
            for (var i = 0; i < alarms.length; i++) {
                if (alarms[i] === ringAlarm) { alarms.splice(i, 1); break; }
            }
            saveAlarms();
        }
        ringAlarm = null;
    }
    if (editing) { paintEdit(); showEdit(); } else { page = 0; paintHome(); showHome(); }
}

function snoozeRing() {
    var t = eos.time.getNow();
    var nm = (t.min + 5) % 60;
    var nh = (t.hour + Math.floor((t.min + 5) / 60)) % 24;
    alarms.push({
        id: nextId++, h: nh, m: nm,
        days: (1 << ((t.day_of_week + 6) % 7)),   /* today only */
        rep: 1, on: true, lf: 0, tmp: true
    });
    saveAlarms();
    stopRing();
}

/* ================= page switching ================= */
function showHome() {
    homeC.removeFlag(lv.OBJ_FLAG_HIDDEN);
    editC.addFlag(lv.OBJ_FLAG_HIDDEN);
    ringC.addFlag(lv.OBJ_FLAG_HIDDEN);
    setTitle("Alarm");
}
function showEdit() {
    homeC.addFlag(lv.OBJ_FLAG_HIDDEN);
    editC.removeFlag(lv.OBJ_FLAG_HIDDEN);
    ringC.addFlag(lv.OBJ_FLAG_HIDDEN);
    setTitle(editing && editing.isNew ? "Add" : "Edit");
}
function showRing() {
    homeC.addFlag(lv.OBJ_FLAG_HIDDEN);
    editC.addFlag(lv.OBJ_FLAG_HIDDEN);
    ringC.removeFlag(lv.OBJ_FLAG_HIDDEN);
    setTitle("Alarm!");
}

/* ================= due check + ring flash driver ================= */
function checkDue() {
    if (ringAlarm) return;
    var t = eos.time.getNow();
    var key = t.year * 1000000 + t.month * 10000 + t.day * 100 + t.hour * 100 + t.min;
    var hit = null, dirty = false;
    for (var i = 0; i < alarms.length; i++) {
        var a = alarms[i];
        if (!a.on) continue;
        if (a.h !== t.hour || a.m !== t.min) continue;
        if (a.lf === key) continue;
        if (!dayMatch(a.days, t.day_of_week)) continue;
        a.lf = key;                    /* mark fired this minute (Core skips relaunch) */
        if (a.rep > 0) { a.rep--; if (a.rep === 0) a.on = false; }
        dirty = true;
        if (!hit) hit = a;
    }
    if (hit) {
        if (dirty) saveAlarms();
        startRing(hit);
    }
}

var tickTimer = new lv.timer(function () {
    tickCount++;
    if (ringAlarm) {
        ringTicks++;
        if (ringTicks >= 600) { stopRing(); return; }   /* auto stop after 60s */
        ringSeqElapsed += 100;
        var seq = RING_SEQ[ringSeqIdx];
        if (ringSeqElapsed >= seq.ms) {
            ringSeqElapsed = 0;
            ringSeqIdx = (ringSeqIdx + 1) % 4;
            applyFlash(RING_SEQ[ringSeqIdx].w);
        }
    }
    if (tickCount % 10 === 0) checkDue();
}, 100, null);
tickTimer.setRepeatCount(-1);

/* ---------- boot ---------- */
loadAlarms();
buildHome();
buildEdit();
buildRing();
paintHome();
showHome();
checkDue();   /* fire immediately if an alarm is due right now */
