// Texthub — light-weight read-only .txt/.md reader for /sdcard/texthub/
// Layout (240x240 round): system header (y0-24) | app bar y34-58
//   (< prev | scrolling filename | next >) | text card y64-230 (drag to scroll)
// Rules: read-only (no fs.write). Recursive scan depth<=8, *.txt/*.md case-insens.
//   64KB per-file cap. Long filenames marquee via LONG_SCROLL_CIRCULAR.
//   MD: light sanitize (strip links/bold/italic/inline-code marks) - single-label
//   rendering can't mix styles; ask if a real block renderer is wanted.
//   EVENT_CLICKED is broken in this fork -> use EVENT_PRESSED (same as timer).

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Texthub");

var BG = 0x0E0E14;
var WHITE = 0xFFFFFF;
var GREY = 0x8A8F98;
var ARROW_OPA = 26;        // idle arrow bg
var ARROW_OFF_OPA = 8;     // disabled (edge) arrow bg

function hex(v) { return lv.color.hex(v); }

var ROOT = "/sdcard/texthub";
var MAX_BYTES = 64 * 1024;
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

// ===================== helpers =====================
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

function makeBarBtn(x, label, cb) {
    var b = new lv.button(R.root);
    b.setSize(32, 24);
    b.setPos(x, 34);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(ARROW_OPA, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(6);
    var l = new lv.label(b);
    l.setSize(32, 24);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(15);
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(240, 0);
    l.setText(label);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(function () { cb(); }, lv.EVENT_PRESSED, null);
    return b;
}

// ===================== app bar (y34-58) =====================
var btnPrev = makeBarBtn(44, "<", function () { go(cur - 1); });
var btnNext = makeBarBtn(164, ">", function () { go(cur + 1); });

var fileLabel = makeLabel(R.root, 80, 36, 108, 20, 13, WHITE, 230, lv.TEXT_ALIGN_LEFT);
fileLabel.addFlag(lv.OBJ_FLAG_SCROLLABLE);
fileLabel.setLongMode(lv.LABEL_LONG_SCROLL_CIRCULAR);   // marquee long names

// ===================== text card (y64-230) =====================
var card = new lv.obj(R.root);
card.setSize(216, 166);
card.setPos(12, 64);
card.setStyleBgOpa(8, 0);
card.setStyleBgColor(hex(WHITE), 0);
card.setStyleRadius(12, 0);
card.setStylePadAll(0, 0);
card.setStyleBorderWidth(0, 0);
card.addFlag(lv.OBJ_FLAG_SCROLLABLE);
card.addFlag(lv.OBJ_FLAG_CLICKABLE);
card.setScrollbarMode(lv.SCROLLBAR_MODE_AUTO);

var body = makeLabel(card, 14, 10, 188, 40, 14, WHITE, 235, lv.TEXT_ALIGN_LEFT);
body.setLongMode(lv.LABEL_LONG_WRAP);   // auto height -> card drag scrolls

// ===================== fs scan (read-only) =====================
function isTextFile(name) {
    var low = name.toLowerCase();
    return low.lastIndexOf(".txt") === low.length - 4 ||
           low.lastIndexOf(".md") === low.length - 3;
}

var files = [];
function scan(dir, depth) {
    if (depth > MAX_DEPTH) return;
    var names = [];
    try { names = eos.fs.list(dir); } catch (e) { return; }   // missing dir
    for (var i = 0; i < names.length; i++) {
        var p = dir + "/" + names[i];
        if (isTextFile(names[i])) { files.push(p); continue; }
        try {
            var sub = eos.fs.list(p);        // dir -> array, file -> throw
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
            if (block.length > 2048) { s += block.join(""); block = []; }
            continue;
        }
        block.push(String.fromCharCode(ch));
        if (block.length > 2048) { s += block.join(""); block = []; }
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

// ===================== load =====================
var cur = 0;

function updateBar() {
    fileLabel.setText(files.length ? basename(files[cur]) : "No files");
    btnPrev.setStyleBgOpa(cur > 0 ? ARROW_OPA : ARROW_OFF_OPA, 0);
    btnNext.setStyleBgOpa(cur < files.length - 1 ? ARROW_OPA : ARROW_OFF_OPA, 0);
}

function fadeIn() {   // 3-step text fade (light touch, non-blocking)
    for (var i = 0; i < 4; i++) {
        (function (a) {
            setTimeout(function () { try { body.setStyleTextOpa(a, 0); } catch (e) {} }, 30 * i);
        })(150 + 35 * i);
    }
}

function load(i) {
    cur = i;
    updateBar();
    if (!files.length) {
        body.setText("Empty: no .txt/.md\nunder /sdcard/texthub/");
        fadeIn();
        return;
    }
    body.setText("Loading\u2026");
    var idx = i;
    setTimeout(function () {          // async read -> UI shows loading first
        var p = files[idx];
        var sz = 0;
        try { sz = eos.fs.size(p); } catch (e) {}
        if (sz > MAX_BYTES) {
            body.setText("File too large: " + Math.floor(sz / 1024) + "KB\n(cap " + (MAX_BYTES / 1024) + "KB)");
            fadeIn();
            return;
        }
        var raw = undefined;
        try { raw = eos.fs.read(p); } catch (e) {}
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
    }, 30);
}

function go(i) {
    if (i < 0 || i >= files.length || i === cur) return;
    load(i);
}

// ===================== boot =====================
scan(ROOT, 0);
files.sort();
load(0);
