// Texthub — read-only .txt/.md reader for /sdcard/texthub/
// 240x240 round. System header y0-24. App bar y32-54:
//   [< prev] [ filename (marquee when too long) ] [next >]
// Text card y62-230, drag to scroll. Read-only (no fs.write anywhere).
// Recursive scan: *.txt / *.md, depth<=8, 16KB/file cap.
// Long filenames: manual ping-pong marquee timer. LVGL LONG_SCROLL_CIRCULAR
// crashed on-device (jerry assert code=120), so we roll our own.
// EVENT_CLICKED broken in this fork -> use EVENT_PRESSED.

var activity = cos.activity.current();
var view = cos.activity.getView(activity);
cos.activity.setTitle(activity, "Texthub");

var BG = 0x0E0E14;
var WHITE = 0xFFFFFF;
var GREY = 0x8A8F98;
var ARROW_OPA = 26;        // idle arrow bg
var ARROW_OFF_OPA = 8;     // disabled (edge) arrow bg

function hex(v) { return lv.color.hex(v); }

var ROOT = "/sdcard/texthub";
var MAX_BYTES = 16 * 1024;   // keep peak memory low on the JS heap
var MAX_DEPTH = 8;

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

function makeLabel(parent, x, y, w, h, size, color, opa, align) {
    var l = new lv.label(parent);
    l.setSize(w, h);
    l.setPos(x, y);
    l.setFontSize(size);
    l.setStyleTextColor(color, 0);
    l.setStyleTextOpa(opa, 0);
    if (align) l.setStyleTextAlign(align, 0);
    l.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return l;
}

/* ===================== app bar (y32-54) ===================== */
var BAR_Y = 32, BAR_H = 22;
var FILE_CLIP_X = 70, FILE_CLIP_W = 96;

function makeBarBtn(x, label, cb) {
    var b = new lv.button(R.root);
    b.setSize(28, BAR_H);
    b.setPos(x, BAR_Y);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(ARROW_OPA, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(8);
    var l = new lv.label(b);
    l.setSize(28, BAR_H);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(14);
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(240, 0);
    l.setText(label);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(function () { cb(); }, lv.EVENT_PRESSED, null);
    return b;
}

var btnPrev = makeBarBtn(38, "<", function () { go(cur - 1); });
var btnNext = makeBarBtn(170, ">", function () { go(cur + 1); });

/* filename window: clip obj + wide label -> manual marquee */
var fileClip = new lv.obj(R.root);
fileClip.setSize(FILE_CLIP_W, BAR_H);
fileClip.setPos(FILE_CLIP_X, BAR_Y);
fileClip.setStyleBgOpa(8, 0);
fileClip.setStyleBgColor(hex(WHITE), 0);
fileClip.setStyleRadius(999, 0);
fileClip.setStylePadAll(0, 0);
fileClip.setStyleBorderWidth(0, 0);
fileClip.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
fileClip.removeFlag(lv.OBJ_FLAG_CLICKABLE);
fileClip.setScrollbarMode(0);

var fileLabel = new lv.label(fileClip);
fileLabel.setSize(300, BAR_H);   // wide: single line, never wraps
fileLabel.setPos(0, 0);
fileLabel.setFontSize(12);
fileLabel.setStyleTextColor(WHITE, 0);
fileLabel.setStyleTextOpa(235, 0);
fileLabel.setLongMode(lv.LABEL_LONG_CLIP);
fileLabel.removeFlag(lv.OBJ_FLAG_CLICKABLE);
fileLabel.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

/* ===================== text card (y62-230) ===================== */
var card = new lv.obj(R.root);
card.setSize(208, 168);
card.setPos(16, 62);
card.setStyleBgOpa(8, 0);
card.setStyleBgColor(hex(WHITE), 0);
card.setStyleRadius(12, 0);
card.setStylePadAll(0, 0);
card.setStyleBorderWidth(0, 0);
card.addFlag(lv.OBJ_FLAG_SCROLLABLE);
card.addFlag(lv.OBJ_FLAG_CLICKABLE);
card.setScrollbarMode(lv.SCROLLBAR_MODE_OFF);

var body = makeLabel(card, 14, 10, 180, 40, 14, WHITE, 235, lv.TEXT_ALIGN_LEFT);
body.setLongMode(lv.LABEL_LONG_WRAP);   // auto height -> card drag scrolls

/* index "n/total" pinned at card bottom (topmost -> stays readable) */
var pageLbl = makeLabel(R.root, 0, 228, 240, 12, 9, GREY, 200, lv.TEXT_ALIGN_CENTER);
pageLbl.setText("0/0");

/* ===================== filename marquee (ping-pong) ===================== */
var marqueeTimer = null;
function setFileName(name) {
    fileLabel.setText(name);
    fileLabel.setPos(0, 0);
    if (marqueeTimer) { try { marqueeTimer.delete(); } catch (e) {} marqueeTimer = null; }
    var tw = name.length * 8 + 8;                 // rough text width (font 12)
    if (tw <= FILE_CLIP_W) return;                // fits -> no scroll
    fileLabel.setWidth(tw + 4);
    var max = tw + 4 - FILE_CLIP_W;
    var x = 0, dir = 1, wait = 16;
    marqueeTimer = new lv.timer(function () {
        if (wait > 0) { wait--; return; }
        x += dir;
        if (x >= max) { dir = -1; wait = 16; }
        else if (x <= 0) { dir = 1; wait = 16; }
        fileLabel.setPos(-x, 0);
    }, 40, null);
    marqueeTimer.setRepeatCount(-1);
}

/* ===================== fs scan (read-only) ===================== */
function isTextFile(name) {
    var low = name.toLowerCase();
    return low.lastIndexOf(".txt") === low.length - 4 ||
           low.lastIndexOf(".md") === low.length - 3;
}

var files = [];
function scan(dir, depth) {
    if (depth > MAX_DEPTH) return;
    var names = [];
    try { names = cos.fs.list(dir); } catch (e) { return; }   // missing dir
    for (var i = 0; i < names.length; i++) {
        var p = dir + "/" + names[i];
        if (isTextFile(names[i])) { files.push(p); continue; }
        try {
            var sub = cos.fs.list(p);        // dir -> array, file -> throw
            if (sub) scan(p, depth + 1);
        } catch (e2) { /* regular file */ }
    }
}

// Uint8Array -> UTF-8 string (chunked String.fromCharCode, correct surrogates)
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
            if (block.length > 512) { s += block.join(""); block = []; }
            continue;
        }
        block.push(String.fromCharCode(ch));
        if (block.length > 512) { s += block.join(""); block = []; }
    }
    if (block.length) s += block.join("");
    return s;
}

// Light MD sanitize: strip image/link/bold/italic/inline-code marks, keep text
function sanitizeMd(s) {
    s = s.replace(/!\[([^\]]*)\]\([^)]*\)/g, "[$1]");
    s = s.replace(/\[([^\]]*)\]\([^)]*\)/g, "$1");
    s = s.replace(/[*_`~]+([^*_`~\n]+)[*_`~]+/g, "$1");
    return s;
}

function basename(p) {
    var k = p.lastIndexOf("/");
    return k >= 0 ? p.substr(k + 1) : p;
}

/* ===================== load ===================== */
var cur = 0;

function updateBar() {
    var name = files.length ? basename(files[cur]) : "No files";
    setFileName(name);
    btnPrev.setStyleBgOpa(cur > 0 ? ARROW_OPA : ARROW_OFF_OPA, 0);
    btnNext.setStyleBgOpa(cur < files.length - 1 ? ARROW_OPA : ARROW_OFF_OPA, 0);
    if (files.length) pageLbl.setText((cur + 1) + "/" + files.length);
    else pageLbl.setText("0/0");
}

function fadeIn() {   // 3-step text fade (light touch, non-blocking)
    for (var i = 0; i < 4; i++) {
        (function (a, d) {
            var t = new lv.timer(function () { try { body.setStyleTextOpa(a, 0); } catch (e) {} }, d, null);
            t.setRepeatCount(1);   // run once (setTimeout replacement)
        })(150 + 35 * i, 30 * i);
    }
}

function load(i) {
    cur = i;
    updateBar();
    if (!files.length) {
        body.setText("No .txt/.md files\nunder /sdcard/texthub/");
        fadeIn();
        return;
    }
    body.setText("Loading\u2026");
    var idx = i;
    var t = new lv.timer(function () {          // async read -> UI shows loading first
        var p = files[idx];
        var sz = 0;
        try { sz = cos.fs.size(p); } catch (e) {}
        if (sz > MAX_BYTES) {
            body.setText("File too large: " + Math.floor(sz / 1024) + "KB\n(cap " + (MAX_BYTES / 1024) + "KB)");
            fadeIn();
            return;
        }
        var raw = undefined;
        try { raw = cos.fs.read(p); } catch (e) {}
        if (!raw) {
            body.setText(sz === 0 ? "Empty file: " + basename(p) : "Read error: " + basename(p));
            fadeIn();
            return;
        }
        var txt = bytesToStr(raw);
        var low = p.toLowerCase();
        if (low.lastIndexOf(".md") === low.length - 3) txt = sanitizeMd(txt);
        body.setText(txt);
        try { R.root.updateLayout(); } catch (e) {}
        try { card.scrollToY(0, 0); } catch (e) {}
        fadeIn();
    }, 30, null);
    t.setRepeatCount(1);   // run once (setTimeout replacement)
}

function go(i) {
    if (i < 0 || i >= files.length || i === cur) return;
    load(i);
}

/* ===================== boot ===================== */
scan(ROOT, 0);
files.sort();
load(0);
