// ElenixOS 画图 — P1a（canvas 路线，重排）
// eos.draw.* ：create(parent,w,h) / setPen(r,g,b) / setPx(x,y,r,g,b 局部)
//            clear(r,g,b) / save(path) -> .edrw ("ELDRW1"+w+h+fmt+RGB565)
// 画笔：JS 侧 Bresenham + NxN 笔刷，坐标由 canvasBack 静态偏移换算；
//       笔画栈 strokes[] 支持撤销；clear 刷底色。
var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "画图");
eos.activity.setAppHeaderVisible(activity, false);

var BG = 0x12121A;
var WHITE = 0xFFFFFF;
var BASE = { r: 20, g: 22, b: 26 };   // 0x14161A 画布底色

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
R.root.addFlag(lv.OBJ_FLAG_CLICKABLE);   // 接收 PRESSED/PRESSING 落点

// ===================== 画布 =====================
// canvasBack 在 (45,46) 尺寸 150x150（白 Opa15 背衬），canvas 由 C 挂在它内部 (0,0)
// => canvas 绝对原点 = (45,46)，即屏幕坐标换算基准（静态布局，硬编码偏移）。
var CW = 150, CH = 150;
var CANVAS_OX = 45, CANVAS_OY = 46;

var canvasBack = new lv.obj(R.root);
canvasBack.setSize(CW, CH);
canvasBack.setPos(CANVAS_OX, CANVAS_OY);
canvasBack.setStyleRadius(0, 0);
canvasBack.setStyleBgOpa(15, 0);
canvasBack.setStyleBgColor(hex(WHITE), 0);
canvasBack.setStyleBorderWidth(0, 0);
canvasBack.setStylePadAll(0, 0);
canvasBack.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
canvasBack.setScrollbarMode(0);
canvasBack.addFlag(lv.OBJ_FLAG_CLICKABLE);

eos.draw.create(canvasBack, CW, CH);
var pen = { r: 255, g: 255, b: 255 };   // 默认白笔（底色深，白可见）
eos.draw.setPen(pen.r, pen.g, pen.b);
eos.draw.clear(BASE.r, BASE.g, BASE.b);

var brushSize = 2;                       // 粗细：2px / 4px
var strokes = [];                        // 撤销栈：每项 {r,g,b,size,segs:[[x0,y0,x1,y1]...]}
var cur = null;
var lastX = -1, lastY = -1;
var suppressDraw = false;                // 覆盖层交互时抑制落点绘制
var paletteDim = null, paletteCard = null, paletteCells = [], selIdx = -1;   // 调色板状态（提前声明，供 markQuick 默认高亮使用）

function setPxLocal(x, y, r, g, b) {
    if (x >= 0 && x < CW && y >= 0 && y < CH) eos.draw.setPx(x, y, r, g, b);
}
function brushDot(cx, cy, size, col) {
    for (var dy = 0; dy < size; dy++)
        for (var dx = 0; dx < size; dx++)
            setPxLocal(cx + dx, cy + dy, col.r, col.g, col.b);
}
function drawSeg(x0, y0, x1, y1, col, size) {
    var dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0);
    var sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    var err = dx - dy;
    while (true) {
        brushDot(x0, y0, size, col);
        if (x0 === x1 && y0 === y1) break;
        var e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}
function toLocal(sx, sy) { return { x: sx - CANVAS_OX, y: sy - CANVAS_OY }; }

function redrawAll() {
    eos.draw.clear(BASE.r, BASE.g, BASE.b);
    for (var s = 0; s < strokes.length; s++) {
        var st = strokes[s];
        var col = { r: st.r, g: st.g, b: st.b };
        for (var i = 0; i < st.segs.length; i++) {
            var sg = st.segs[i];
            drawSeg(sg[0], sg[1], sg[2], sg[3], col, st.size);
        }
    }
}
function undo() {
    if (strokes.length > 0) { strokes.pop(); redrawAll(); flashStatus("已撤销"); }
    else flashStatus("无可撤销");
}

R.root.addEventCb(function (ev) {
    if (suppressDraw) { suppressDraw = false; lastX = -1; lastY = -1; return; }
    var p = toLocal(ev.x, ev.y);
    if (p.x < 0 || p.x >= CW || p.y < 0 || p.y >= CH) { lastX = -1; lastY = -1; return; }
    cur = { r: pen.r, g: pen.g, b: pen.b, size: brushSize, segs: [] };
    drawSeg(p.x, p.y, p.x, p.y, cur, brushSize);
    cur.segs.push([p.x, p.y, p.x, p.y]);
    lastX = p.x; lastY = p.y;
}, lv.EVENT_PRESSED, null);

R.root.addEventCb(function (ev) {
    if (suppressDraw) return;
    if (lastX < 0) return;
    var p = toLocal(ev.x, ev.y);
    drawSeg(lastX, lastY, p.x, p.y, cur, brushSize);
    cur.segs.push([lastX, lastY, p.x, p.y]);
    lastX = p.x; lastY = p.y;
}, lv.EVENT_PRESSING, null);

R.root.addEventCb(function (ev) {
    if (ev.type === lv.EVENT_RELEASED) {
        if (cur) { strokes.push(cur); cur = null; }
        lastX = -1; lastY = -1;
    }
}, lv.EVENT_RELEASED, null);

// ===================== Toast（保存/撤销/清除 反馈） =====================
var toast = null, toastLab = null;
function flashStatus(msg) {
    if (!toast) {
        toast = new lv.obj(R.root);
        toast.setSize(120, 26);
        toast.setPos(60, 104);
        toast.setStyleRadius(13, 0);
        toast.setStyleBgOpa(205, 0);
        toast.setStyleBgColor(hex(BG), 0);
        toast.setStyleBorderWidth(0, 0);
        toast.setStylePadAll(0, 0);
        toast.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        toast.setScrollbarMode(0);
        toast.addFlag(lv.OBJ_FLAG_HIDDEN);
        toastLab = new lv.label(toast);
        toastLab.setSize(120, 26);
        toastLab.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        toastLab.setFontSize(12);
        toastLab.setStyleTextColor(hex(WHITE), 0);
        toastLab.setText("");
        toastLab.align(lv.ALIGN_CENTER, 0, 0);
    }
    toastLab.setText(msg);
    toast.removeFlag(lv.OBJ_FLAG_HIDDEN);
    new lv.timer(function () { toast.addFlag(lv.OBJ_FLAG_HIDDEN); }, 800, null);
}

// ===================== 工具行（y28） =====================
// 3 快捷色(白/黄/红) + 更多(空心白环) + 粗细(2/4px) + 撤销；总宽 130 ≤ 138
function onTap(obj, cb) {
    obj.addEventCb(function () { suppressDraw = true; cb(); }, lv.EVENT_PRESSED, null);
}
function quickDot(x, color, cb) {
    var d = new lv.obj(R.root);
    d.setSize(14, 14);
    d.setPos(x, 28);
    d.setStyleRadius(7, 0);
    d.setStyleBgOpa(255, 0);
    d.setStyleBgColor(hex(color), 0);
    d.setStyleBorderWidth(0, 0);
    d.setStylePadAll(0, 0);
    d.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    d.addFlag(lv.OBJ_FLAG_CLICKABLE);
    onTap(d, cb);
    return d;
}
function setPen(r, g, b) {
    pen = { r: r, g: g, b: b };
    eos.draw.setPen(r, g, b);
}
var quickDots = [];
function clearQuickHL() {
    for (var i = 0; i < quickDots.length; i++) quickDots[i].obj.setStyleBorderWidth(0, 0);
}
function markQuick(i) {
    clearQuickHL();
    if (i >= 0 && i < quickDots.length) {
        quickDots[i].obj.setStyleBorderWidth(2, 0);
        quickDots[i].obj.setStyleBorderColor(hex(WHITE), 0);
    }
    markSelected(-1);
}
function mkQuick(x, color, r, g, b, idx) {
    var d = quickDot(x, color, function () { setPen(r, g, b); markQuick(idx); });
    quickDots.push({ obj: d, r: r, g: g, b: b });
    return d;
}
mkQuick(12, 0xFFFFFF, 255, 255, 255, 0);
mkQuick(30, 0xFCEE0A, 252, 238, 10, 1);
mkQuick(48, 0xFF003C, 255, 0, 60, 2);
markQuick(0);   // 默认白笔高亮

// 更多（空心白环，附不可见探针标签 "更多"）
var moreRing = new lv.obj(R.root);
moreRing.setSize(14, 14);
moreRing.setPos(68, 28);
moreRing.setStyleRadius(7, 0);
moreRing.setStyleBgOpa(0, 0);
moreRing.setStyleBorderWidth(2, 0);
moreRing.setStyleBorderColor(hex(WHITE), 0);
moreRing.setStylePadAll(0, 0);
moreRing.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
moreRing.addFlag(lv.OBJ_FLAG_CLICKABLE);
onTap(moreRing, showPalette);
var moreLab = new lv.label(moreRing);
moreLab.setSize(14, 14);
moreLab.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
moreLab.setFontSize(10);
moreLab.setStyleTextColor(hex(WHITE), 0);
moreLab.setStyleTextOpa(0, 0);     // 不可见，仅供探针按标签查找
moreLab.setText("更多");
moreLab.align(lv.ALIGN_CENTER, 0, 0);

// 粗细（2px / 4px 切换）
var sizePill = new lv.obj(R.root);
sizePill.setSize(24, 22);
sizePill.setPos(88, 24);
sizePill.setStyleRadius(6, 0);
sizePill.setStyleBgOpa(18, 0);
sizePill.setStyleBgColor(hex(WHITE), 0);
sizePill.setStyleBorderWidth(0, 0);
sizePill.setStylePadAll(0, 0);
sizePill.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
sizePill.addFlag(lv.OBJ_FLAG_CLICKABLE);
var sizeLab = new lv.label(sizePill);
sizeLab.setSize(24, 22);
sizeLab.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
sizeLab.setFontSize(11);
sizeLab.setStyleTextColor(hex(WHITE), 0);
sizeLab.setText("2px");
sizeLab.align(lv.ALIGN_CENTER, 0, 0);
onTap(sizePill, function () {
    brushSize = (brushSize === 2) ? 4 : 2;
    sizeLab.setText(brushSize === 2 ? "2px" : "4px");
});

// 撤销
var undoPill = new lv.obj(R.root);
undoPill.setSize(26, 22);
undoPill.setPos(116, 24);
undoPill.setStyleRadius(6, 0);
undoPill.setStyleBgOpa(18, 0);
undoPill.setStyleBgColor(hex(WHITE), 0);
undoPill.setStyleBorderWidth(0, 0);
undoPill.setStylePadAll(0, 0);
undoPill.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
undoPill.addFlag(lv.OBJ_FLAG_CLICKABLE);
var undoLab = new lv.label(undoPill);
undoLab.setSize(26, 22);
undoLab.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
undoLab.setFontSize(11);
undoLab.setStyleTextColor(hex(WHITE), 0);
undoLab.setText("撤销");
undoLab.align(lv.ALIGN_CENTER, 0, 0);
onTap(undoPill, undo);

// ===================== 底行（y196）：打开 / 保存 / 清除 =====================
function bottomBtn(x, text, cb) {
    var b = new lv.obj(R.root);
    b.setSize(36, 20);
    b.setPos(x, 196);
    b.setStyleRadius(6, 0);
    b.setStyleBgOpa(16, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStyleBorderWidth(0, 0);
    b.setStylePadAll(0, 0);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.addFlag(lv.OBJ_FLAG_CLICKABLE);
    var l = new lv.label(b);
    l.setSize(36, 20);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(10);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setText(text);
    l.align(lv.ALIGN_CENTER, 0, 0);
    onTap(b, cb);
    return b;
}
function clearCanvas() {
    eos.draw.clear(BASE.r, BASE.g, BASE.b);
    strokes = []; cur = null; lastX = -1; lastY = -1;
    flashStatus("已清除");
}
bottomBtn(63, "打开", openDrawing);
bottomBtn(102, "保存", saveDrawing);
bottomBtn(141, "清除", clearCanvas);

// ===================== 调色板（更多点，构建即存；暗卡覆盖层） =====================
var PALETTE_COLS = 8, PALETTE_ROWS = 6;   // 48 色（paletteDim/paletteCells 已在顶部声明）
function hsv2rgb(h, s, v) {
    s /= 100; v /= 100;
    var c = v * s;
    var x = c * (1 - Math.abs((h / 60) % 2 - 1));
    var m = v - c;
    var r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return { r: Math.round((r + m) * 255), g: Math.round((g + m) * 255), b: Math.round((b + m) * 255) };
}
function buildPalette() {
    paletteDim = new lv.obj(R.root);
    paletteDim.setSize(240, 240);
    paletteDim.setPos(0, 0);
    paletteDim.setStyleBgOpa(120, 0);
    paletteDim.setStyleBgColor(hex(BG), 0);
    paletteDim.setStylePadAll(0, 0);
    paletteDim.setStyleBorderWidth(0, 0);
    paletteDim.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    paletteDim.setScrollbarMode(0);
    paletteDim.addFlag(lv.OBJ_FLAG_CLICKABLE);
    paletteDim.addFlag(lv.OBJ_FLAG_HIDDEN);
    paletteDim.addEventCb(function () { suppressDraw = true; hidePalette(); }, lv.EVENT_PRESSED, null);

    paletteCard = new lv.obj(paletteDim);
    paletteCard.setSize(120, 120);
    paletteCard.setPos(60, 60);
    paletteCard.setStyleRadius(10, 0);
    paletteCard.setStyleBgOpa(220, 0);
    paletteCard.setStyleBgColor(hex(BG), 0);
    paletteCard.setStyleBorderWidth(0, 0);
    paletteCard.setStylePadAll(0, 0);
    paletteCard.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    paletteCard.setScrollbarMode(0);
    // 卡本身不接收点击 -> 点卡外空白落到 dimmer 关闭

    var cardTitle = new lv.label(paletteCard);
    cardTitle.setSize(120, 18);
    cardTitle.setPos(0, 6);
    cardTitle.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    cardTitle.setFontSize(11);
    cardTitle.setStyleTextColor(hex(WHITE), 0);
    cardTitle.setText("选色");

    var cell = 10, gap = 4;
    var gw = PALETTE_COLS * cell + (PALETTE_COLS - 1) * gap;   // 108
    var gh = PALETTE_ROWS * cell + (PALETTE_ROWS - 1) * gap;   // 80
    var x0 = (120 - gw) / 2;   // 6
    var y0 = 28;
    for (var r = 0; r < PALETTE_ROWS; r++) {
        for (var c = 0; c < PALETTE_COLS; c++) {
            var hue = c * 360 / PALETTE_COLS;
            var val = 100 - r * 90 / (PALETTE_ROWS - 1);
            var rgb = hsv2rgb(hue, 100, val);
            if (c === 4 && r === 0) rgb = { r: 0, g: 229, b: 255 };   // 青 0x00E5FF 入调色板
            var idx = r * PALETTE_COLS + c;
            var o = new lv.obj(paletteCard);
            o.setSize(cell, cell);
            o.setPos(x0 + c * (cell + gap), y0 + r * (cell + gap));
            o.setStyleRadius(2, 0);
            o.setStyleBgOpa(255, 0);
            o.setStyleBgColor(hex((rgb.r << 16) | (rgb.g << 8) | rgb.b), 0);
            o.setStyleBorderWidth(0, 0);
            o.setStylePadAll(0, 0);
            o.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            o.addFlag(lv.OBJ_FLAG_CLICKABLE);
            (function (ii, rr, gg, bb) {
                o.addEventCb(function () {
                    suppressDraw = true;
                    setPen(rr, gg, bb);
                    markSelected(ii);
                    hidePalette();
                }, lv.EVENT_PRESSED, null);
            })(idx, rgb.r, rgb.g, rgb.b);
            paletteCells.push(o);
        }
    }
}
function markSelected(i) {
    for (var k = 0; k < paletteCells.length; k++) {
        if (k === i) {
            paletteCells[k].setStyleBorderWidth(2, 0);
            paletteCells[k].setStyleBorderColor(hex(WHITE), 0);
        } else {
            paletteCells[k].setStyleBorderWidth(0, 0);
        }
    }
    selIdx = i;
    if (i >= 0) clearQuickHL();
}
function showPalette() {
    if (!paletteDim) buildPalette();
    paletteDim.removeFlag(lv.OBJ_FLAG_HIDDEN);
}
function hidePalette() {
    if (paletteDim) paletteDim.addFlag(lv.OBJ_FLAG_HIDDEN);
}
buildPalette();   // 构建即存在（探针按标签 "选色" 查找）

// ===================== 保存 =====================
var saveCount = 0;
function saveDrawing() {
    var path = "/sdcard/Drawings/drawing" + saveCount + ".edrw";
    saveCount++;
    var ok = eos.draw.save(path);
    flashStatus(ok ? "已保存" : "保存失败");
}

// ===================== 打开（文件列表，全屏覆盖，默认隐藏） =====================
var fileList = null;
var fileRows = [];
var curList = [];
var FILE_ROWS = 8;
function buildFileList() {
    fileList = new lv.obj(R.root);
    fileList.setSize(240, 240);
    fileList.setPos(0, 0);
    fileList.setStyleBgOpa(255, 0);
    fileList.setStyleBgColor(hex(BG), 0);
    fileList.setStylePadAll(0, 0);
    fileList.setStyleBorderWidth(0, 0);
    fileList.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    fileList.setScrollbarMode(0);
    fileList.addFlag(lv.OBJ_FLAG_CLICKABLE);
    fileList.addFlag(lv.OBJ_FLAG_HIDDEN);
    fileList.addEventCb(function () { suppressDraw = true; hideFileList(); }, lv.EVENT_PRESSED, null);

    var title = new lv.label(fileList);
    title.setSize(240, 24);
    title.setPos(0, 6);
    title.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    title.setFontSize(12);
    title.setStyleTextColor(hex(WHITE), 0);
    title.setText("打开（点空白关闭）");

    var x0 = 12, y0 = 36, rw = 216, rh = 22, gap = 2;
    for (var i = 0; i < FILE_ROWS; i++) {
        (function (idx) {
            var row = new lv.obj(fileList);
            row.setSize(rw, rh);
            row.setPos(x0, y0 + idx * (rh + gap));
            row.setStyleRadius(4, 0);
            row.setStyleBgOpa(18, 0);
            row.setStyleBgColor(hex(WHITE), 0);
            row.setStyleBorderWidth(0, 0);
            row.setStylePadAll(0, 0);
            row.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            row.addFlag(lv.OBJ_FLAG_CLICKABLE);
            var lab = new lv.label(row);
            lab.setSize(rw, rh);
            lab.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
            lab.setFontSize(11);
            lab.setStyleTextColor(hex(WHITE), 0);
            lab.setPos(6, 4);
            lab.setText("");
            row.addEventCb(function () {
                suppressDraw = true;
                if (idx < curList.length) loadDrawing(curList[idx]);
            }, lv.EVENT_PRESSED, null);
            fileRows.push({ obj: row, label: lab });
        })(i);
    }
}
function renderFileRows(names) {
    curList = names;
    for (var i = 0; i < FILE_ROWS; i++) {
        if (i < names.length) {
            fileRows[i].label.setText(names[i]);
            fileRows[i].obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
        } else if (names.length > FILE_ROWS && i === FILE_ROWS - 1) {
            fileRows[i].label.setText("…共 " + names.length + " 个");
            fileRows[i].obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
        } else {
            fileRows[i].obj.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
}
function showFileList(names) {
    if (!fileList) buildFileList();
    renderFileRows(names);
    fileList.removeFlag(lv.OBJ_FLAG_HIDDEN);
}
function hideFileList() {
    if (fileList) fileList.addFlag(lv.OBJ_FLAG_HIDDEN);
}
function openDrawing() {
    var names;
    try { names = eos.fs.list("/sdcard/Drawings"); }
    catch (e) { flashStatus("无绘图"); return; }
    var edrw = [];
    for (var i = 0; i < names.length; i++) {
        var n = names[i];
        if (n.indexOf(".edrw") === n.length - 5) edrw.push(n);
    }
    if (edrw.length === 0) { flashStatus("无绘图"); return; }
    showFileList(edrw);
}
function loadDrawing(name) {
    var ok = eos.draw.load("/sdcard/Drawings/" + name);
    flashStatus(ok ? ("已打开 " + name) : ("打开失败 " + name));
    hideFileList();
}

// ===================== 弦宽自验 + 对象账本 =====================
function chordSelfCheck() {
    var C = 120, RAD = 120, cw = CW;
    var rows = [46, 70, 96, 120, 146, 170, 196];
    var minUp = 1e9, minLo = 1e9, all = 1;
    eos.console.log("[draw-chord] canvas " + CW + "x" + CH +
        " @(" + CANVAS_OX + "," + CANVAS_OY + ")..(" + (CANVAS_OX + CW) + "," + (CANVAS_OY + CH) + ")");
    for (var i = 0; i < rows.length; i++) {
        var dy = rows[i] - C;
        var ch = 2 * Math.sqrt(RAD * RAD - dy * dy);
        var ok = (cw <= ch) ? 1 : 0;
        if (!ok) all = 0;
        if (i === 0) minUp = ch;
        if (i === rows.length - 1) minLo = ch;
        eos.console.log("[draw-chord] row y=" + rows[i] + " chord=" + ch.toFixed(1) +
            " canvasW=" + cw + " " + (ok ? "PASS" : "FAIL"));
    }
    eos.console.log("[draw-chord] MIN_UPPER=" + minUp.toFixed(1) +
        " MIN_LOWER=" + minLo.toFixed(1) + " ALL_PASS=" + all);
}
function audit() {
    var rc = R.root.getChildCount();
    var pc = paletteCard ? paletteCard.getChildCount() : 0;   // card 的子：title + 48 = 49
    var total = rc + pc + 1;   // +1 = paletteCard 本身（挂在 paletteDim 下，不计入 rc）
    eos.console.log("[draw-audit] root children=" + rc + " paletteCard children=" + pc +
        " (title+48) 总对象=" + total + " (+canvas C)");
    eos.console.log("[draw-audit] 按钮: 快捷色3 + 更多1 + 粗细1 + 撤销1 + 底行3 = 9 ; 调色板48格");
}
chordSelfCheck();
audit();
eos.console.log("[draw] P1a ready (canvas 150x150, palette 48, .edrw 150x150)");
