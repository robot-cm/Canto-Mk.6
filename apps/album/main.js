// Canto Mk.6 Album - recursive browser of /sdcard/album (240x240 round, English)
//
// - Recursive scan (depth <= 4, max 200 files), filter png/jpg/jpeg/bmp,
//   sorted by full path (i.e. by name within the album tree).
// - First image shown on open. Top marquee shows the file name (scrolls when
//   long, SCROLL_CIRCULAR like notes). Left/right arrows wrap around.
// - Bottom trash deletes the current image via eos.fs.remove(path) (returns
//   bool; needs the SNI added in sni_api_eos.c).
// - Memory guard: files > 300KB are skipped (same guard as the P1 probe kept
//   the LVGL decoders safe), a single lv.image object is reused so the old
//   decoded buffer is freed on setSrc, and a post-decode size cap is checked.
// Red lines: no arc / no border / radius<54 / no flex / anim<=6 / scroll is
// used only on the marquee label / events are EVENT_PRESSED.

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Album");

var BG = 0x12121A, WHITE = 0xFFFFFF, GREY = 0x9A9AA8, RED = 0xE5484D;
function hex(v) { return lv.color.hex(v); }

var root = new lv.obj(view);
root.setSize(240, 240); root.setPos(0, 0);
root.setStyleRadius(0, 0);
root.setStyleBgOpa(255, 0);
root.setStyleBgColor(hex(BG), 0);
root.setStylePadAll(0, 0);
root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
root.setScrollbarMode(0);

/* ---------------- top: file name marquee (y36-56) ---------------- */
var nameLbl = new lv.label(root);
nameLbl.setSize(240, 20); nameLbl.setPos(0, 36);
nameLbl.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
nameLbl.setStyleTextColor(hex(WHITE), 0);
nameLbl.setStyleTextOpa(230, 0);
nameLbl.setFontSize(12);
nameLbl.addFlag(lv.OBJ_FLAG_SCROLLABLE);
nameLbl.setLongMode(lv.LABEL_LONG_SCROLL_CIRCULAR);   // marquee long names
nameLbl.setScrollbarMode(0);

/* -------- center: image (adaptive fit to free area, centered 120,124) -------- */
var img = new lv.image(root);
img.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
img.setScrollbarMode(0);
img.addFlag(lv.OBJ_FLAG_HIDDEN);

/* ---------------- center overlay message ---------------- */
var msgLbl = new lv.label(root);
msgLbl.setSize(220, 60); msgLbl.setPos(10, 106);
msgLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
msgLbl.setStyleTextColor(hex(GREY), 0);
msgLbl.setStyleTextOpa(220, 0);
msgLbl.setFontSize(12);
msgLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
msgLbl.setText("Loading...");

/* ---------------- side arrows ---------------- */
function arrow(x, glyph, cb) {
    var b = new lv.button(root);
    b.setSize(24, 64); b.setPos(x, 104);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(16, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStyleBorderWidth(0, 0);
    b.setStylePadAll(0, 0);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    var l = new lv.label(b);
    l.setSize(24, 64);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(18);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setStyleTextOpa(230, 0);
    l.setText(glyph);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addEventCb(cb, lv.EVENT_PRESSED, null);
    return { btn: b, lbl: l };
}
var btnPrev = arrow(6, "<", function () { if (items.length) show(cur - 1); });
var btnNext = arrow(210, ">", function () { if (items.length) show(cur + 1); });

/* ---------------- bottom: trash button (comb-drawn icon) ---------------- */
var trashBtn = new lv.button(root);
trashBtn.setSize(44, 26); trashBtn.setPos(98, 212);
trashBtn.setStyleRadius(13, 0);
trashBtn.setStyleBgOpa(255, 0);
trashBtn.setStyleBgColor(hex(RED), 0);
trashBtn.setStyleBorderWidth(0, 0);
trashBtn.setStylePadAll(0, 0);
trashBtn.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

function iconPart(parent, x, y, w, h, r) {
    var o = new lv.obj(parent);
    o.setSize(w, h); o.setPos(x, y);
    o.setStyleBgOpa(230, 0);
    o.setStyleBgColor(hex(WHITE), 0);
    o.setStyleRadius(r, 0);
    o.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return o;
}
iconPart(trashBtn, 18, 4, 8, 2, 1);     /* handle */
iconPart(trashBtn, 12, 7, 20, 2, 1);    /* lid */
iconPart(trashBtn, 14, 9, 16, 12, 2);   /* body */
iconPart(trashBtn, 18, 11, 2, 9, 1);    /* slat 1 */
iconPart(trashBtn, 23, 11, 2, 9, 1);    /* slat 2 */
trashBtn.addEventCb(delCurrent, lv.EVENT_PRESSED, null);

var idxLbl = new lv.label(root);
idxLbl.setSize(40, 16); idxLbl.setPos(8, 219);
idxLbl.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
idxLbl.setStyleTextColor(hex(GREY), 0);
idxLbl.setStyleTextOpa(200, 0);
idxLbl.setFontSize(10);
idxLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
idxLbl.setText("0/0");

/* ---------------- scan / browse ---------------- */
var ALBUM_DIR = "/sdcard/album";
var MAX_FILE = 300 * 1024;   /* LVGL decoder headroom (probe-proven) */
var MAX_IMAGES = 200;
var MAX_DEPTH = 4;
var EXTS = [".png", ".jpg", ".jpeg", ".bmp"];

var items = [];   /* { path, name, size } */
var cur = -1;

function isImg(name) {
    var low = name.toLowerCase();
    for (var i = 0; i < EXTS.length; i++) {
        if (low.length > EXTS[i].length &&
            low.lastIndexOf(EXTS[i]) === low.length - EXTS[i].length) {
            return true;
        }
    }
    return false;
}

function scanDir(dir, depth, out) {
    if (depth > MAX_DEPTH || out.length >= MAX_IMAGES) return;
    var names = [];
    try { names = eos.fs.list(dir); } catch (e) { return; }   /* missing dir */
    for (var i = 0; i < names.length; i++) {
        if (out.length >= MAX_IMAGES) break;
        var n = names[i];
        var p = dir + "/" + n;
        if (isImg(n)) {
            var sz = 0;
            try { sz = eos.fs.size(p); } catch (e2) {}
            out.push({ path: p, name: n, size: sz });
        } else {
            try {
                var sub = eos.fs.list(p);      /* dir -> array, file -> throw */
                if (sub) scanDir(p, depth + 1, out);
            } catch (e3) { /* regular file */ }
        }
    }
}

/* ---------------- image probing (magic + JPEG SOF) ---------------- */
/* Reads only the file head via fs.peek (never the whole file) and classifies:
   real format by magic number, and baseline vs progressive JPEG by SOF. */
function probeImage(path) {
    var b;
    try { b = eos.fs.peek(path, 0, 8192); } catch (e) { return { ok: false, reason: "Probe error" }; }
    if (!b || b.length < 4) return { ok: false, reason: "Unreadable" };
    if (b[0] === 0xFF && b[1] === 0xD8 && b[2] === 0xFF) {
        /* JPEG: walk the segment table to find SOF0/1 (baseline) or SOF2 (progressive) */
        var prog = false, sof = false, i = 2;
        while (i + 3 < b.length) {
            if (b[i] !== 0xFF) { i++; continue; }
            var m = b[i + 1];
            if (m === 0xFF) { i += 2; continue; }            /* fill byte */
            if (m === 0xD8 || m === 0x01) { i += 2; continue; }  /* SOI / TEM */
            if (m >= 0xD0 && m <= 0xD7) { i += 2; continue; }    /* RSTn */
            if (m === 0xD9 || m === 0xDA) break;                 /* EOI / SOS */
            if (m === 0xC0 || m === 0xC1 || m === 0xC2) { sof = true; if (m === 0xC2) prog = true; break; }
            var sl = (b[i + 2] << 8) | b[i + 3];
            if (sl < 2) break;
            i += 2 + sl;
        }
        return { ok: true, jpeg: true, progressive: prog, hasSOF: sof };
    }
    if (b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4E && b[3] === 0x47) return { ok: true, real: "PNG" };
    if (b[0] === 0x42 && b[1] === 0x4D) return { ok: true, real: "BMP" };
    if (b[0] === 0x47 && b[1] === 0x49 && b[2] === 0x46) return { ok: true, real: "GIF" };
    if (b[0] === 0x52 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x46) return { ok: true, real: "WebP" };
    return { ok: false, reason: "Unknown format" };
}

function hasExt(name, ext) {
    var l = name.toLowerCase();
    return l.length > ext.length && l.lastIndexOf(ext) === l.length - ext.length;
}

function show(i) {
    var n = items.length;
    if (n === 0) {
        cur = -1;
        nameLbl.setText("");
        idxLbl.setText("0/0");
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("No images\nin /sdcard/album/");
        btnPrev.lbl.setStyleTextOpa(60, 0);
        btnNext.lbl.setStyleTextOpa(60, 0);
        return;
    }
    cur = ((i % n) + n) % n;   /* wrap */
    var it = items[cur];
    nameLbl.setText(it.name);
    idxLbl.setText((cur + 1) + "/" + n);
    btnPrev.lbl.setStyleTextOpa(230, 0);
    btnNext.lbl.setStyleTextOpa(230, 0);

    if (it.size > MAX_FILE) {   /* skip huge files to protect the decoder */
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Too large\n(" + Math.floor(it.size / 1024) + "KB)");
        return;
    }

    /* Probe the real format before decoding: a .jpg whose bytes are actually
       PNG/WebP, or a progressive JPEG, can never be shown by the built-in
       decoders - report it clearly instead of a bare "Decode failed". */
    var p = probeImage(it.path);
    var isJpgName = hasExt(it.name, ".jpg") || hasExt(it.name, ".jpeg");
    if (p.real && isJpgName) {
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Real " + p.real + "\nrename to ." + p.real.toLowerCase());
        return;
    }
    if (p.jpeg && !isJpgName) {
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Real JPEG\nrename to .jpg");
        return;
    }
    if (p.jpeg && p.progressive) {
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Progressive JPEG\nnot supported");
        return;
    }

    msgLbl.addFlag(lv.OBJ_FLAG_HIDDEN);
    img.removeFlag(lv.OBJ_FLAG_HIDDEN);
    img.setSrc(it.path);   /* decode png/jpg/bmp (old buffer freed on setSrc) */
    var w = img.getWidth(), h = img.getHeight();
    if (w <= 0 || h <= 0) {
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Decode failed");
        return;
    }
    if (w > 2048 || h > 2048) {   /* sanity cap even after decode */
        img.addFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Too large");
        return;
    }
    /* Adaptive fit: scale the decoded image into the largest rect that fits
       the free space (below the title marquee, above the trash bar, clear of
       the side arrows), then center it. No hard 150x150 cap. */
    var FREE_W = 180, FREE_H = 176;      /* free rect x:30..210, y:36..212 */
    var CX = 120, CY = 124;              /* center of the free rect */
    var scale = Math.min(1000, Math.floor(Math.min(FREE_W * 1000 / w, FREE_H * 1000 / h)));
    scale = Math.max(100, scale);        /* never smaller than 10% */
    img.setPivot(Math.floor(w / 2), Math.floor(h / 2));
    img.setScale(scale);
    img.setPos(CX, CY);
}

function delCurrent() {
    if (cur < 0 || cur >= items.length) return;
    var it = items[cur];
    var ok = false;
    try { ok = eos.fs.remove(it.path); } catch (e) {}
    if (!ok) {
        msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
        msgLbl.setText("Delete failed");
        return;
    }
    items.splice(cur, 1);
    show(cur);   /* next item slides into place; wraps naturally */
}

/* ---------------- boot ---------------- */
msgLbl.removeFlag(lv.OBJ_FLAG_HIDDEN);
msgLbl.setText("Loading...");
scanDir(ALBUM_DIR, 0, items);
items.sort(function (a, b) { return a.path < b.path ? -1 : (a.path > b.path ? 1 : 0); });
show(0);
