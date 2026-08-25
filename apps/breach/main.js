// 入侵协议 — P0：三页骨架 + Boot 动画（Cyberpunk v1）
// 规则以 github.com/yet3/cyberpunk2077-breach-protocol 为准（fetch 到）：
//   码池 55/BD/1C/E9/ 7A（5）；5x5；缓冲 5；序列 3（长 2-4）；初始方向 ROW/0（首选第0行）。
// 红线：opa 裸数字；hex 数字；setFontSize；R.root 全屏 r0 禁滚；label 进容器 align CENTER；
//       装饰容器禁滚；timer≥30ms；anim 全池化（LVGL timer/overlay 步进）；事件 PRESSED；
//       行宽 ≤ min(chord顶,chord底)−16。

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "入侵协议");
eos.activity.setAppHeaderVisible(activity, false);

var BG     = 0x050508;   // 底
var YELLOW = 0xFCEE0A;   // 主黄
var RED    = 0xFF003C;   // 警红
var GREEN  = 0x00FF41;   // 成绿
var GRAY   = 0x8A8F98;   // 灰
var PANEL  = 0x111115;   // 矩阵/面板底（仓库矩阵底）

function hex(v) { return lv.color.hex(v); }

// 捕获 eos 日志句柄（加载期 eos 可解析；计时器/事件回调上下文里全局 eos 可能不可解析，
// 故在加载期绑定闭包变量 LOG，避免回调里 "eos is not defined" 级联）。
var LOG = function (m) {};
try { var _eos = eos; LOG = function (m) { try { _eos.console.log(m); } catch (e2) {} }; } catch (e) {}

// ===================== 红线：所有 lv.timer 回调首行包 try/catch =====================
// 单点缺符号不再脑死亡全引擎（state=3 全拒收 → 卡死）。
// catch 里 log + pause（delete）该 timer，问题隔离在单回调。
function safeTimer(cb, period, data) {
    return new lv.timer(function () {
        try { cb(); }
        catch (e) {
            if (typeof R !== "undefined" && R) R.jerryErrors++;
            // 计时器回调上下文里全局 eos 可能不可解析；用加载期捕获的 LOG（闭包绑定），绝不依赖回调内全局 eos
            LOG("[breach-err] timer cb error: " + (e && e.message ? e.message : String(e)));
        }
    }, period, data);
}
// 注：safeTimer 内部必须用原生 new lv.timer（绝不可改名 safeTimer，否则自递归）。

// ===================== 纯逻辑核心（与 core.js 同源，ES5 for-loop） =====================
var CODES = ["55", "BD", "1C", "E9", "7A"];
function mulberry32(seed) {
    var s = seed >>> 0;
    return function () {
        s = (s + 0x6D2B79F5) | 0;
        var t = Math.imul(s ^ (s >>> 15), 1 | s);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}
function randInt(rng, a, b) { return Math.floor(rng() * (b - a + 1)) + a; }
function randCode(rng) { return CODES[randInt(rng, 0, CODES.length - 1)]; }
function idxToRC(idx, cols) { return { col: idx % cols, row: Math.floor(idx / cols) }; }
function defaultConfig() {
    return { matrixCols: 5, matrixRows: 5, bufferSize: 5, solutionSize: 5,
             numberOfSequences: 3, minSequenceSize: 2, maxSequenceSize: 4,
             time: 60, availableCodes: CODES.slice() };
}
// ===================== 等级系统 =====================
// 参数参照仓库 generateBreachConfig 的难度映射（time=timeIdx*15；buffer/序列随矩阵递增）。
// C = 原固定配置（5x5/缓冲5/序列3/60s），保持默认体验不变。
var LEVELS = [
    { id: "A", label: "3x3", matrixCols: 3, matrixRows: 3, bufferSize: 3, solutionSize: 3,
      numberOfSequences: 2, minSequenceSize: 2, maxSequenceSize: 3, time: 30 },
    { id: "B", label: "4x4", matrixCols: 4, matrixRows: 4, bufferSize: 4, solutionSize: 4,
      numberOfSequences: 3, minSequenceSize: 2, maxSequenceSize: 3, time: 45 },
    { id: "C", label: "5x5", matrixCols: 5, matrixRows: 5, bufferSize: 5, solutionSize: 5,
      numberOfSequences: 3, minSequenceSize: 2, maxSequenceSize: 4, time: 60 },
    { id: "D", label: "6x6", matrixCols: 6, matrixRows: 6, bufferSize: 6, solutionSize: 6,
      numberOfSequences: 3, minSequenceSize: 3, maxSequenceSize: 5, time: 75 }
];
var curLv = 2;   // 默认等级 C（5x5，与原固定配置一致）
function levelConfig(li) {
    var base = LEVELS[li];
    var c = { matrixCols: base.matrixCols, matrixRows: base.matrixRows,
              bufferSize: base.bufferSize, solutionSize: base.solutionSize,
              numberOfSequences: base.numberOfSequences,
              minSequenceSize: base.minSequenceSize, maxSequenceSize: base.maxSequenceSize,
              time: base.time, availableCodes: CODES.slice() };
    return c;
}
function generateMatrix(rng, cfg) {
    var cols = cfg.matrixCols, rows = cfg.matrixRows, n = cols * rows, i;
    var matrix = [];
    for (i = 0; i < n; i++) {
        var rc = idxToRC(i, cols);
        matrix.push({ idx: i, code: randCode(rng), col: rc.col, row: rc.row });
    }
    var dir = "ROW", row = 0, col = 0, used = {}, path = [], solution = [], safety = 0, ok = true;
    while (path.length < cfg.solutionSize && safety < 2000) {
        safety++;
        var free = [];
        if (dir === "COL") { for (var r = 0; r < rows; r++) { var ci = r * cols + col; if (!used[ci]) free.push(ci); } }
        else { for (var c = 0; c < cols; c++) { var ci2 = row * cols + c; if (!used[ci2]) free.push(ci2); } }
        if (free.length === 0) {
            var last = path.pop();
            if (last == null) { ok = false; break; }
            used[last] = false; dir = (dir === "ROW") ? "COL" : "ROW";
            var lc = idxToRC(last, cols); row = lc.row; col = lc.col;
            continue;
        }
        var pick = free[randInt(rng, 0, free.length - 1)];
        used[pick] = true; path.push(pick);
        var pc = idxToRC(pick, cols); row = pc.row; col = pc.col;
  if (path.length < cfg.solutionSize) dir = (dir === "ROW") ? "COL" : "ROW";
    }
    if (path.length < cfg.solutionSize) ok = false;
    for (i = 0; i < path.length; i++) {
        var cc = randCode(rng); matrix[path[i]].code = cc; solution.push(cc);
    }
    return { matrix: matrix, solution: solution, path: path, solvable: ok };
}
function generateSequences(rng, cfg, solution) {
    var possible = [];
    for (var z = 0; z < solution.length; z++)
        for (var i = cfg.minSequenceSize; i <= solution.length - z; i++)
            if (i <= cfg.maxSequenceSize) possible.push(solution.slice(z, z + i));
    var seqs = [];
    // 恒产 numberOfSequences 条：优先选首字节不同于已选者（视觉区分），无可选则退化为任意剩余。
    // 原版按首字节去重会清空 possible 提前 break → 序列数 < 3 → t2 占位符 "-- -- --" 横线残留。
    while (seqs.length < cfg.numberOfSequences && possible.length > 0) {
        var preferred = [];
        for (var p = 0; p < possible.length; p++) {
            var dup = false;
            for (var q = 0; q < seqs.length; q++) if (possible[p][0] === seqs[q].codes[0]) { dup = true; break; }
            if (!dup) preferred.push(p);
        }
        var pool = preferred.length > 0 ? preferred : null;
        var idx = pool ? pool[randInt(rng, 0, pool.length - 1)] : randInt(rng, 0, possible.length - 1);
        var seq = possible.splice(idx, 1)[0];
        seqs.push({ codes: seq, status: "IN_PROGRESS" });
    }
    return seqs;
}
function isSubsequence(buf, seq) {
    if (seq.length > buf.length) return false;
    for (var i = 0; i + seq.length <= buf.length; i++) {
        var okk = true;
        for (var j = 0; j < seq.length; j++) if (buf[i + j] !== seq[j]) { okk = false; break; }
        if (okk) return true;
    }
    return false;
}
function isSelectable(sel, code) {
    return (sel.direction === "ROW") ? (sel.value === code.row) : (sel.value === code.col);
}
function afterSelect(sel, code) {
    return { direction: (sel.direction === "ROW") ? "COL" : "ROW",
             value: (sel.direction === "ROW") ? code.col : code.row };
}

// ===================== Root =====================
var R = { jerryErrors: 0 };
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
R.root.addFlag(lv.OBJ_FLAG_CLICKABLE);

// 背景十字网格：已删除。1px 结构性不渲染，2px(Opa12) 真窗口实测仍不可见 —— 按约定不装样子，删。


// ===================== 三页容器（HIDDEN 切换 + fadeIn） =====================
function makePage() {
    var p = new lv.obj(R.root);
    p.setSize(240, 240);
    p.setPos(0, 0);
    p.setStyleRadius(0, 0);
    p.setStyleBgOpa(255, 0);
    p.setStyleBgColor(hex(BG), 0);
    p.setStylePadAll(0, 0);
    p.setStyleBorderWidth(0, 0);
    p.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    p.setScrollbarMode(0);
    return p;
}
var pageBoot = makePage();
var pageHack = makePage();
var pageResult = makePage();
pageHack.addFlag(lv.OBJ_FLAG_HIDDEN);
pageResult.addFlag(lv.OBJ_FLAG_HIDDEN);
pageResult.setStyleBgOpa(230, 0);   // translucent overlay：压在 Hack 之上作暗化遮罩（overlay Opa230）

// fade 遮罩（池化 timer 步进遮罩透明度，撤销/创建禁用）
var fadeOverlay = new lv.obj(R.root);
fadeOverlay.setSize(240, 240);
fadeOverlay.setPos(0, 0);
fadeOverlay.setStyleRadius(0, 0);
fadeOverlay.setStyleBgColor(hex(BG), 0);
fadeOverlay.setStyleBgOpa(0, 0);
fadeOverlay.setStyleBorderWidth(0, 0);
fadeOverlay.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
fadeOverlay.setScrollbarMode(0);
fadeOverlay.addFlag(lv.OBJ_FLAG_HIDDEN);

function fadeTo(target) {
    pageBoot.addFlag(lv.OBJ_FLAG_HIDDEN);
    pageResult.addFlag(lv.OBJ_FLAG_HIDDEN);
    if (target !== pageResult) pageHack.addFlag(lv.OBJ_FLAG_HIDDEN);  // Result 作 overlay：保留 Hack 作暗化底
    target.removeFlag(lv.OBJ_FLAG_HIDDEN);
    // Glitch 仅在 Result 可见时运行
    if (target === pageResult) startGlitch(); else stopGlitch();
    fadeOverlay.removeFlag(lv.OBJ_FLAG_HIDDEN);
    fadeOverlay.setStyleBgOpa(255, 0);
    var step = 0, steps = 5;   // 30ms * 5 = 150ms fadeIn（timer≥30ms）
    var t = safeTimer(function () {
        step++;
        if (step >= steps) {
            fadeOverlay.setStyleBgOpa(0, 0);
            fadeOverlay.addFlag(lv.OBJ_FLAG_HIDDEN);
            t.delete();
            // Hack 页可见 + 布局完成后，实测盒做相交断言（绕过隐藏页坐标 0 的坑）
            if (target === pageHack) {
                var at2 = safeTimer(function () {
                    try { auditLayout(); } catch (e) { R.jerryErrors++; LOG("[breach-err] auditLayout: " + (e && e.message ? e.message : String(e))); }
                    at2.delete();
                }, 60, null);
                at2.setRepeatCount(1);
                at2.setAutoDelete(true);
            }
            return;
        }
        fadeOverlay.setStyleBgOpa(255 - Math.floor(255 * step / steps), 0);
    }, 30, null);
    t.setRepeatCount(-1);
    t.setAutoDelete(true);
}

// ===================== P1 Boot 页 =====================
var bootTitle = new lv.label(pageBoot);
bootTitle.setSize(240, 36);          // 全宽 240 + 居中；两行高度 36（y56..92，文本宽≤187）
bootTitle.setPos(0, 56);
bootTitle.setFontSize(16);
bootTitle.setStyleTextColor(hex(YELLOW), 0);
bootTitle.setStyleTextLetterSpace(2, 0);
bootTitle.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);   // CENTER
bootTitle.setText("");

var bootSub = new lv.label(pageBoot);
bootSub.setSize(184, 16);
bootSub.setPos(28, 100);
bootSub.setFontSize(10);
bootSub.setStyleTextColor(hex(GRAY), 0);
bootSub.setStyleTextLetterSpace(2, 0);
bootSub.setStyleTextAlign(2, 0);
bootSub.setText("INITIATING...");

var PB_W = 12, PB_H = 12, PB_GAP = 4, PB_N = 10;
var pbTotal = PB_N * PB_W + (PB_N - 1) * PB_GAP;   // 156
var pbX0 = (240 - pbTotal) / 2;                    // 42
var progBlocks = [];
for (var i = 0; i < PB_N; i++) {
    var b = new lv.obj(pageBoot);
    b.setSize(PB_W, PB_H);
    b.setPos(pbX0 + i * (PB_W + PB_GAP), 130);
    b.setStyleRadius(0, 0);
    b.setStyleBgColor(hex(0x222228), 0);
    b.setStyleBgOpa(255, 0);
    b.setStyleBorderWidth(0, 0);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    b.setScrollbarMode(0);
    progBlocks.push(b);
}

var bootCount = new lv.label(pageBoot);
bootCount.setSize(80, 36);
bootCount.setPos(80, 170);
bootCount.setFontSize(28);
bootCount.setStyleTextColor(hex(RED), 0);
bootCount.setStyleTextLetterSpace(2, 0);
bootCount.setStyleTextAlign(2, 0);
    bootCount.setText("");

// 玩法说明（UI 全英文）：Boot 页 y210 一行，font9 灰，居中。"TAP ROW/COL TO MATCH SEQS" 21 字符 font9≈105px 居中安全（弦宽≈159px）
var bootHint = new lv.label(pageBoot);
bootHint.setSize(200, 14);
bootHint.setPos(20, 210);
bootHint.setFontSize(9);
bootHint.setStyleTextColor(hex(GRAY), 0);
bootHint.setStyleTextLetterSpace(2, 0);
bootHint.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
bootHint.setText("TAP ROW/COL TO MATCH SEQS");

function startBoot() {
    var full = "BREACH PROTOCOL";
    var ti = 0;
    var tw = safeTimer(function () {
        ti++;
        bootTitle.setText(full.substring(0, ti));
        if (ti >= full.length) { tw.delete(); startProgress(); }
    }, 50, null);
    tw.setRepeatCount(-1);
    tw.setAutoDelete(true);
}
function startProgress() {
    var lit = 0;
    var pt = safeTimer(function () {
        if (lit < PB_N) {
            progBlocks[lit].setStyleBgColor(hex(YELLOW), 0);
            lit++;
        } else {
            pt.delete();
            startCountdown();
        }
    }, 200, null);
    pt.setRepeatCount(-1);
    pt.setAutoDelete(true);
}
function startCountdown() {
    var nums = ["3", "2", "1"];
    var ci = 0;
    var ct = safeTimer(function () {
        if (ci < nums.length) { bootCount.setText(nums[ci]); ci++; }
        else { bootCount.setText(""); ct.delete(); newGame(); fadeTo(pageHack); }
    }, 500, null);
    ct.setRepeatCount(-1);
    ct.setAutoDelete(true);
}
startBoot();

// ===================== P2 Hack 页（P1 完整实现） =====================
var MAX_N = 6, MAX_BUF = 6;   // 对象池上限（等级 D 6×6；缓冲槽 6）
var CELL_W = 30, CELL_H = 20, CELL_GAP = 3, CELL_R = 2;   // 横向 gap3
var MX_X0 = 39, MX_Y0 = 70;
var COL_STEP = CELL_W + CELL_GAP;   // 33
var ROW_STEP = CELL_H + 2;          // 22（纵向 gap2 → 5 行 y70/92/114/136/158，底 178）

// 按等级动态布局：5x5（C）及以下沿用原布局；6x6（D）格子 27x16、行距 18，6 行底 176 ≤ 目标区 178
function layoutFor(c) {
    var n = c.matrixCols;
    if (n <= 5) return { cellW: 30, cellH: 20, colStep: 33, rowStep: 22, x0: 39, y0: 70 };
    var cellW = 27, cellH = 16, gap = 3;
    var total = n * cellW + (n - 1) * gap;
    return { cellW: cellW, cellH: cellH, colStep: cellW + gap, rowStep: 18,
             x0: Math.floor((240 - total) / 2), y0: 70 };
}
// 应用当前等级的矩阵/缓冲槽/目标序列布局（对象池显隐 + 定位）
function applyLayout() {
    var L = layoutFor(cfg);
    for (var r = 0; r < MAX_N; r++) for (var c = 0; c < MAX_N; c++) {
        var cell = cells[r][c];
        if (r < cfg.matrixRows && c < cfg.matrixCols) {
            cell.obj.setPos(L.x0 + c * L.colStep, L.y0 + r * L.rowStep);
            cell.obj.setSize(L.cellW, L.cellH);
            cell.lab.setSize(L.cellW, L.cellH);
            cell.lab.setFontSize(10);
            cell.obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
        } else {
            cell.obj.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
    var bufTotal = cfg.bufferSize * BUF_W + (cfg.bufferSize - 1) * BUF_GAP;
    var bx0 = (240 - bufTotal) / 2;
    for (var i = 0; i < MAX_BUF; i++) {
        if (i < cfg.bufferSize) {
            bufSlots[i].obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
            bufSlots[i].obj.setPos(bx0 + i * (BUF_W + BUF_GAP), 50);
        } else {
            bufSlots[i].obj.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
    for (var t = 0; t < tgtLabels.length; t++) {
        if (t < cfg.numberOfSequences) tgtLabels[t].removeFlag(lv.OBJ_FLAG_HIDDEN);
        else { tgtLabels[t].setText(""); tgtLabels[t].addFlag(lv.OBJ_FLAG_HIDDEN); }
    }
}

// --- HUD：仅右计时（状态栏已承载"入侵协议"；页内左标题删，避免重复+被状态栏裁）---
var hackTimer = new lv.label(pageHack);
hackTimer.setSize(30, 18);
hackTimer.setPos(160, 30);         // 计时右缘 x190（黄）；顶栏 y30 不被状态栏裁
hackTimer.setFontSize(18);
hackTimer.setStyleTextColor(hex(YELLOW), 0);
hackTimer.setStyleTextLetterSpace(2, 0);
hackTimer.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
hackTimer.setText("60");

// 等级标签（左上；当前等级 LV X，font10 灰）
var lvTag = new lv.label(pageHack);
lvTag.setSize(50, 18);
lvTag.setPos(10, 30);
lvTag.setFontSize(10);
lvTag.setStyleTextColor(hex(GRAY), 0);
lvTag.setStyleTextLetterSpace(1, 0);
lvTag.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
lvTag.setText("LV C");

// --- 缓冲槽（对象池 6，按等级显示 3..6）---
var BUF_W = 26, BUF_H = 14, BUF_GAP = 4;
var bufSlots = [];
for (var bi = 0; bi < MAX_BUF; bi++) {
    var so = new lv.obj(pageHack);
    so.setSize(BUF_W, BUF_H);
    so.setPos(0, 50);                    // 位置由 applyLayout 按等级设置
    so.setStyleRadius(CELL_R, 0);
    so.setStyleBgColor(hex(0x26262E), 0);          // 空槽填充提亮（原 0x1A1A20 近 BG 不可见）
    so.setStyleBgOpa(255, 0);
    so.setStyleBorderWidth(1, 0);                   // 槽轮廓，空态也看出是槽
    so.setStyleBorderColor(hex(0x55585F), 0);
    so.setStyleBorderOpa(180, 0);
    so.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    so.setScrollbarMode(0);
    so.addFlag(lv.OBJ_FLAG_HIDDEN);      // 池对象初始隐藏
    var sl = new lv.label(so);
    sl.setSize(BUF_W, BUF_H);
    sl.setPos(0, 0);
    sl.setFontSize(12);
    sl.setStyleTextColor(hex(GRAY), 0);
    sl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    sl.setText("-");
    bufSlots.push({ obj: so, lab: sl });
}

// --- 矩阵（对象池 6×6，按等级显示 3×3..6×6；位置/尺寸由 applyLayout 设置）---
var cells = [];
for (var r = 0; r < MAX_N; r++) {
    cells[r] = [];
    for (var c = 0; c < MAX_N; c++) {
        (function (rr, cc) {
            var o = new lv.obj(pageHack);
            o.setSize(CELL_W, CELL_H);
            o.setPos(0, 0);
            o.setStyleRadius(CELL_R, 0);
            o.setStyleBgColor(hex(PANEL), 0);
            o.setStyleBgOpa(255, 0);
            o.setStyleBorderWidth(0, 0);
            o.setStylePadAll(0, 0);   // 清默认 padding，label 子对象(0,0) 才与格盒重合（⊆ 断言 + 视觉对齐）
            o.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            o.setScrollbarMode(0);
            o.addFlag(lv.OBJ_FLAG_CLICKABLE);
            o.addFlag(lv.OBJ_FLAG_HIDDEN);   // 池对象初始隐藏
            var l = new lv.label(o);
            l.setSize(CELL_W, CELL_H);
            l.setPos(0, 0);
            l.setFontSize(10);
            l.setStyleTextColor(hex(GRAY), 0);
            l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
            l.setStyleTextLetterSpace(1, 0);   // LS1（防双字符截断）
            l.setText("??");
            o.addEventCb(function () {
                try { doSelect(rr, cc); }
                catch (e) {
                    R.jerryErrors++;
                    LOG("[breach-err] select error: " + (e && e.message ? e.message : String(e)));
                }
            }, lv.EVENT_PRESSED, null);
            cells[rr][cc] = { obj: o, lab: l };
        })(r, c);
    }
}

// --- 扫描线（池化；1px→10px，因本 fork <8px 二维对象不渲染）---
var scanLine = new lv.obj(pageHack);
scanLine.setStyleRadius(0, 0);
scanLine.setStyleBgColor(hex(YELLOW), 0);
scanLine.setStyleBgOpa(90, 0);
scanLine.setStyleBorderWidth(0, 0);
scanLine.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
scanLine.setScrollbarMode(0);
scanLine.addFlag(lv.OBJ_FLAG_HIDDEN);

// --- 目标序列（3，三态；font10 / CLIP 禁换行 / 居中盒(10..230 中心120) / y178·194·210 步距16 → 底224 ≤ chord-16≈103；三行零重叠硬验收）---
// 括号被删：纯序列 "BD 1C E9 1C" 才是换行元凶（长串 + 默认 WRAP 模式 → 换行叠字）。
// CLIP 结构性禁换行（不包即裁，绝不换行叠字）；盒 220 宽居中，文本 CENTER 对齐即居中。
var TGT_Y = [178, 194, 210];
var tgtLabels = [];
for (var ti = 0; ti < 3; ti++) {
    var tl = new lv.label(pageHack);
    tl.setLongMode(lv.LABEL_LONG_CLIP);   // 结构性禁换行（CLIP，SNI 严格 1 参）
    tl.setSize(220, 14);                      // 盒居中（10..230，中心120）
    tl.setPos(10, TGT_Y[ti]);
    tl.setFontSize(10);
    tl.setStyleTextColor(hex(GRAY), 0);
    tl.setStyleTextLetterSpace(1, 0);
    tl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    tl.setText("-- -- --");                    // 纯序列占位（刷新时去括号，见 refreshTargets）
    tgtLabels.push(tl);
}

// --- 状态 ---
var state = { active: false, buffer: [], picked: {}, pickOrder: [], sel: { direction: "ROW", value: 0 }, seqs: [], matrix: [], solution: [], path: [], timeLeft: 60, win: false, round: 0 };
var cfg = defaultConfig();

// --- 游戏计时器（池化常驻；state.active 门控）---
var gameTimer = safeTimer(function () {
    if (!state.active) return;
    state.timeLeft--;
    if (state.timeLeft < 0) state.timeLeft = 0;
    hackTimer.setText(String(state.timeLeft));
    if (state.timeLeft <= 0) endGame(false);
}, 1000, null);
gameTimer.setRepeatCount(-1);
gameTimer.setAutoDelete(false);

function newGame() {
    state.round++;
    cfg = levelConfig(curLv);            // 按当前等级取参数
    R.config = cfg;
    applyLayout();                       // 重排矩阵/缓冲槽/目标序列（等级切换时生效）
    var tt = null;
    try { tt = eos.time.getNow(); } catch (e) { tt = null; }
    var seed = tt ? ((tt.sec * 1000 + tt.ms + state.round * 2654435761) >>> 0)
                  : ((state.round * 2654435761 + 12345) >>> 0);
    var rng = mulberry32(seed);
    var gm = generateMatrix(rng, cfg);
    var seqs = generateSequences(rng, cfg, gm.solution);
    state.matrix = gm.matrix;
    state.seqs = seqs;
    state.solution = gm.solution;
    state.path = gm.path;
    state.buffer = [];
    state.picked = {};
    state.pickOrder = [];
    state.sel = { direction: "ROW", value: 0 };
    state.timeLeft = cfg.time;
    state.win = false;
    state.active = true;
    hackTimer.setText(String(state.timeLeft));
    hackTimer.removeFlag(lv.OBJ_FLAG_HIDDEN);   // Fix1：retry/重开恢复计时可见
    refreshAll();
    if (!R._matrixAudited) { R._matrixAudited = true; auditMatrix(); }
}

function doSelect(r, c) {
    if (!state.active) return;
    var idx = r * cfg.matrixCols + c;
    if (state.picked[idx]) return;
    if (!isSelectable(state.sel, { row: r, col: c })) return;
    state.picked[idx] = true;
    state.pickOrder.push(idx);
    state.buffer.push(state.matrix[idx].code);
    state.sel = afterSelect(state.sel, { row: r, col: c });
    refreshAll();
    flashCell(cells[r][c]);
    var allSolved = true;
    for (var s = 0; s < state.seqs.length; s++) if (!isSubsequence(state.buffer, state.seqs[s].codes)) { allSolved = false; break; }
    if (allSolved) { endGame(true); return; }
    if (state.buffer.length >= cfg.bufferSize) { endGame(false); return; }
}

function flashCell(cell) {
    cell.obj.setStyleBgColor(hex(YELLOW), 0);
    var ft = safeTimer(function () { cell.obj.setStyleBgColor(hex(0x2A2A12), 0); ft.delete(); }, 80, null);
    ft.setRepeatCount(-1);
    ft.setAutoDelete(true);
}

function solvedCellSet() {
    var set = {};
    for (var s = 0; s < state.seqs.length; s++) {
        var seq = state.seqs[s].codes, L = seq.length;
        for (var i = 0; i + L <= state.buffer.length; i++) {
            var ok = true;
            for (var j = 0; j < L; j++) if (state.buffer[i + j] !== seq[j]) { ok = false; break; }
            if (ok) for (var j = 0; j < L; j++) set[state.pickOrder[i + j]] = true;
        }
    }
    return set;
}

function seqState(buf, seq) {
    if (isSubsequence(buf, seq)) return 2;
    var L = buf.length;
    for (var k = 1; k < seq.length; k++) {
        if (L < k) break;
        var tail = buf.slice(L - k), pre = seq.slice(0, k), same = true;
        for (var i = 0; i < k; i++) if (tail[i] !== pre[i]) { same = false; break; }
        if (same) return 1;
    }
    return 0;
}

function refreshCells() {
    var green = solvedCellSet();
    for (var r = 0; r < MAX_N; r++) for (var c = 0; c < MAX_N; c++) {
        if (r >= cfg.matrixRows || c >= cfg.matrixCols) continue;   // 池中隐藏格跳过
        var idx = r * cfg.matrixCols + c, cell = cells[r][c];
        cell.lab.setText(state.matrix[idx].code);
        if (state.picked[idx]) {
            cell.obj.setStyleBgColor(hex(0x2A2A12), 0);
            cell.lab.setStyleTextColor(hex(green[idx] ? GREEN : YELLOW), 0);
        } else if (isSelectable(state.sel, { row: r, col: c })) {
            cell.obj.setStyleBgColor(hex(PANEL), 0);
            cell.lab.setStyleTextColor(hex(YELLOW), 0);
        } else {
            cell.obj.setStyleBgColor(hex(PANEL), 0);
            cell.lab.setStyleTextColor(hex(0xB4B9C2), 0);   // 不可选灰字提亮（原 GRAY 太暗读不出）
            cell.lab.setStyleTextOpa(255, 0);
        }
    }
}
function refreshBuffer() {
    for (var i = 0; i < MAX_BUF; i++) {
        if (i >= cfg.bufferSize) continue;   // 池中隐藏槽跳过
        if (i < state.buffer.length) {
            var isGreen = false;
            for (var s = 0; s < state.seqs.length; s++) {
                var seq = state.seqs[s].codes, L = seq.length;
                for (var st = 0; st + L <= state.buffer.length; st++) {
                    var ok = true; for (var j = 0; j < L; j++) if (state.buffer[st + j] !== seq[j]) { ok = false; break; }
                    if (ok && i >= st && i < st + L) isGreen = true;
                }
            }
            bufSlots[i].lab.setText(state.buffer[i]);
            bufSlots[i].lab.setStyleTextColor(hex(isGreen ? GREEN : YELLOW), 0);
        } else {
            bufSlots[i].lab.setText("-");
            bufSlots[i].lab.setStyleTextColor(hex(GRAY), 0);
        }
    }
}
function refreshTargets() {
    for (var s = 0; s < tgtLabels.length; s++) {
        var tl = tgtLabels[s];
        if (s < cfg.numberOfSequences && s < state.seqs.length) {
            var seq = state.seqs[s].codes;
            var st = seqState(state.buffer, seq);
            tl.setText(seq.join(" "));   // 纯序列（去括号，CLIP 禁换行，避免叠字）
            tl.setStyleTextColor(hex(st === 2 ? GREEN : (st === 1 ? YELLOW : GRAY)), 0);
            tl.removeFlag(lv.OBJ_FLAG_HIDDEN);   // 注：本 fork SNI 未注册 clearFlag，用 removeFlag
        } else {
            // 防御：序列数 < 3 时清空多余标签（避免占位符 "-- -- --" 横线残留）—— generateSequences 已修，此为双保险
            tl.setText("");
            tl.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
}
function refreshScan() {
    var L = layoutFor(cfg);
    if (state.sel.direction === "ROW") {
        scanLine.setSize(L.colStep * cfg.matrixCols - CELL_GAP, 10);
        scanLine.setPos(L.x0, L.y0 + state.sel.value * L.rowStep + (L.cellH - 10) / 2);
    } else {
        scanLine.setSize(10, L.rowStep * cfg.matrixRows - CELL_GAP);
        scanLine.setPos(L.x0 + state.sel.value * L.colStep + (L.cellW - 10) / 2, L.y0);
    }
    scanLine.removeFlag(lv.OBJ_FLAG_HIDDEN);
}
function refreshAll() { refreshCells(); refreshBuffer(); refreshTargets(); refreshScan(); }

function endGame(win) {
    if (!state.active) return;
    state.active = false;
    state.win = win;
    var solved = 0;
    for (var s = 0; s < state.seqs.length; s++) if (isSubsequence(state.buffer, state.seqs[s].codes)) solved++;
    resStatus.setText(win ? "BREACH SUCCESS" : "BREACH FAILED");
    resStatus.setStyleTextColor(hex(win ? GREEN : RED), 0);
    resProgress.setText("SOLVED " + solved + " / " + state.seqs.length);
    hackTimer.addFlag(lv.OBJ_FLAG_HIDDEN);   // Fix1：失败/胜利页不留残亮倒计时（overlay Opa230 仍透出"0"）
    fadeTo(pageResult);   // overlay over Hack（不隐藏 Hack，作暗化底）
}

// ===================== P3 Result 内容（overlay Opa230 + success/fail + n/3 + RETRY + Glitch） =====================
var resStatus = new lv.label(pageResult);
resStatus.setSize(220, 30);
resStatus.setPos(10, 72);
resStatus.setFontSize(20);
resStatus.setStyleTextColor(hex(GREEN), 0);
resStatus.setStyleTextLetterSpace(2, 0);
resStatus.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
resStatus.setText("BREACH SUCCESS");

var resProgress = new lv.label(pageResult);
resProgress.setSize(220, 20);
resProgress.setPos(10, 112);
resProgress.setFontSize(16);
resProgress.setStyleTextColor(hex(YELLOW), 0);
resProgress.setStyleTextLetterSpace(2, 0);
resProgress.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
resProgress.setText("SOLVED 0 / 3");

// RETRY 按钮（点击重开；池化对象，EVENT_PRESSED）
var resRetry = new lv.obj(pageResult);
resRetry.setSize(120, 34);
resRetry.setPos(60, 152);
resRetry.setStyleRadius(2, 0);
resRetry.setStyleBgColor(hex(0x1A1A20), 0);
resRetry.setStyleBgOpa(255, 0);
resRetry.setStyleBorderWidth(1, 0);
resRetry.setStyleBorderColor(hex(YELLOW), 0);
resRetry.setStyleBorderOpa(255, 0);
resRetry.setStylePadAll(0, 0);
resRetry.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
resRetry.setScrollbarMode(0);
resRetry.addFlag(lv.OBJ_FLAG_CLICKABLE);
var resRetryLab = new lv.label(resRetry);
resRetryLab.setSize(120, 34);
resRetryLab.setPos(0, 0);
resRetryLab.setFontSize(14);
resRetryLab.setStyleTextColor(hex(YELLOW), 0);
resRetryLab.setStyleTextLetterSpace(2, 0);
resRetryLab.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
resRetryLab.setText("RETRY");
resRetry.addEventCb(function () {
    try { retry(); }
    catch (e) { R.jerryErrors++; LOG("[breach-err] retry: " + (e && e.message ? e.message : String(e))); }
}, lv.EVENT_PRESSED, null);

// --- 等级选择（DIFF，y196；点击即切换难度并立即重开；英文缩写 3x3/4x4/5x5/6x6）---
var lvBtns = [];
var lvBtnW = 48, lvBtnH = 24, lvBtnGap = 6;
var lvTotal = LEVELS.length * lvBtnW + (LEVELS.length - 1) * lvBtnGap;   // 210
var lvX0 = (240 - lvTotal) / 2;                                          // 15
for (var li = 0; li < LEVELS.length; li++) {
    (function (i) {
        var lb = new lv.obj(pageResult);
        lb.setSize(lvBtnW, lvBtnH);
        lb.setPos(lvX0 + i * (lvBtnW + lvBtnGap), 196);
        lb.setStyleRadius(2, 0);
        lb.setStyleBgColor(hex(0x1A1A20), 0);
        lb.setStyleBgOpa(255, 0);
        lb.setStyleBorderWidth(1, 0);
        lb.setStyleBorderColor(hex(0x55585F), 0);
        lb.setStyleBorderOpa(255, 0);
        lb.setStylePadAll(0, 0);
        lb.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        lb.setScrollbarMode(0);
        lb.addFlag(lv.OBJ_FLAG_CLICKABLE);
        var ll = new lv.label(lb);
        ll.setSize(lvBtnW, lvBtnH);
        ll.setPos(0, 0);
        ll.setFontSize(10);
        ll.setStyleTextColor(hex(GRAY), 0);
        ll.setStyleTextLetterSpace(1, 0);
        ll.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
        ll.setText(LEVELS[i].label);
        lb.addEventCb(function () {
            try { pickLevel(i); }
            catch (e) { R.jerryErrors++; LOG("[breach-err] pickLevel: " + (e && e.message ? e.message : String(e))); }
        }, lv.EVENT_PRESSED, null);
        lvBtns.push({ obj: lb, lab: ll });
    })(li);
}
function refreshLevelBtns() {
    lvTag.setText("LV " + LEVELS[curLv].id);
    for (var i = 0; i < lvBtns.length; i++) {
        var on = (i === curLv);
        lvBtns[i].obj.setStyleBorderColor(hex(on ? YELLOW : 0x55585F), 0);
        lvBtns[i].lab.setStyleTextColor(hex(on ? YELLOW : GRAY), 0);
    }
}
function pickLevel(li) {
    if (li < 0 || li >= LEVELS.length) return;
    curLv = li;
    refreshLevelBtns();
    retry();   // 以新等级立即开新局
}
refreshLevelBtns();

// Glitch 故障特效（池化 timer；timer≥30ms；仅 Result 可见时运行）
var glitchTimer = null;
function startGlitch() {
    if (glitchTimer) return;
    glitchTimer = safeTimer(function () {
        try {
            if (pageResult.hasFlag(lv.OBJ_FLAG_HIDDEN)) { stopGlitch(); return; }
            var jx = (Math.floor(Math.random() * 7) - 3);   // -3..3 水平抖动
            resStatus.setPos(10 + jx, 72);
            // 偶发错位色（RGB 撕裂感）：失败时闪青、成功时闪黄。Glitch 占空比≈20% ≤30%，基色恒读红/绿
            if (Math.random() < 0.20) resStatus.setStyleTextColor(hex(state.win ? YELLOW : 0x00FFFF), 0);
            else resStatus.setStyleTextColor(hex(state.win ? GREEN : RED), 0);
        } catch (e) { R.jerryErrors++; stopGlitch(); }
    }, 40, null);
    glitchTimer.setRepeatCount(-1);
    glitchTimer.setAutoDelete(false);
}
function stopGlitch() {
    if (glitchTimer) { glitchTimer.delete(); glitchTimer = null; }
    resStatus.setPos(10, 72);
    resStatus.setStyleTextColor(hex(state.win ? GREEN : RED), 0);
}

// ===================== audit（对象账本，供探针用） =====================
function countDesc(o) {
    if (!o || !o.getChildCount) return 0;
    var n = o.getChildCount(), t = n;
    for (var i = 0; i < n; i++) t += countDesc(o.getChild(i));
    return t;
}
function audit() {
    LOG("[breach-audit] root.direct=" + R.root.getChildCount()
        + " pageBoot.desc=" + countDesc(pageBoot)
        + " pageHack.desc=" + countDesc(pageHack)
        + " pageResult.desc=" + countDesc(pageResult)
        + " cells=" + (MAX_N * MAX_N) + " buf=" + MAX_BUF + " tgt=" + tgtLabels.length
        + " lv=" + LEVELS.length);
}

// 矩阵 getText 审计：每格文本应为 55/BD/1C/E9/7A 之一（验证生成 + 显示无截断）
function auditMatrix() {
    var bad = 0, rows = [];
    for (var r = 0; r < cfg.matrixRows; r++) {
        var rc = [];
        for (var c = 0; c < cfg.matrixCols; c++) {
            var t = cells[r][c].lab.getText();
            rc.push(t);
            if (CODES.indexOf(t) < 0) { bad++; LOG("[breach-matrix-ERR] cell " + r + "," + c + "='" + t + "'"); }
        }
        rows.push(rc.join(" "));
    }
    LOG("[breach-matrix] " + rows.join(" | ") + "  (bad=" + bad + ")");
}

// 布局盒实测（getCoords 真实绝对坐标 {x1,y1,x2,y2}）+ 相交断言：
// 凡多行堆叠必须实测盒 + 两两相交=fail；矩阵 25 label 盒必须 ⊆ 各自格盒（宽≤30）。
function boxOf(o) {
    var a = o.getCoords();
    return { x1: a.x1, y1: a.y1, x2: a.x2, y2: a.y2, w: (a.x2 - a.x1 + 1), h: (a.y2 - a.y1 + 1) };
}
function intersect(b1, b2) {
    return !(b1.x1 > b2.x2 || b2.x1 > b1.x2 || b1.y1 > b2.y2 || b2.y1 > b1.y2);
}
function auditLayout() {
    var ok = true, why = "";
    var tb = [];
    // 目标文本原样打印（诊断 t2 横线：占位符 "-- -- --" vs 真实 hex）
    for (var ti = 0; ti < tgtLabels.length; ti++) {
        var tx = "?";
        try { tx = tgtLabels[ti].getText(); } catch (e2) {}
        LOG("[breach-target-text] t" + ti + "=\"" + tx + "\"");
    }
    for (var i = 0; i < cfg.numberOfSequences; i++) {
        var b = boxOf(tgtLabels[i]);
        tb.push(b);
        LOG("[breach-overlap] t" + i + "=(" + b.x1 + "," + b.y1 + ")-(" + b.x2 + "," + b.y2 + ") w" + b.w + " h" + b.h);
        if (b.h > 14) { ok = false; why += " t" + i + ".h=" + b.h + ">14"; }
    }
    for (var a = 0; a < tb.length; a++)
        for (var c = a + 1; c < tb.length; c++)
            if (intersect(tb[a], tb[c])) { ok = false; why += " t" + a + "X" + c; }
    var minW = 999, maxW = 0, allWithin = true;
    for (var r = 0; r < cfg.matrixRows; r++) for (var cc = 0; cc < cfg.matrixCols; cc++) {
        var cell = cells[r][cc];
        var cb = boxOf(cell.obj), lb = boxOf(cell.lab);
        if (lb.w < minW) minW = lb.w;
        if (lb.w > maxW) maxW = lb.w;
        var within = (lb.x1 >= cb.x1 && lb.y1 >= cb.y1 && lb.x2 <= cb.x2 && lb.y2 <= cb.y2 && lb.w <= cb.w && lb.h <= cb.h);
        if (!within) { allWithin = false; ok = false; why += " cell" + r + "," + cc + " overflow"; }
    }
    LOG("[breach-overlap] matrix: minLabW=" + minW + " maxLabW=" + maxW + " allWithinCell=" + (allWithin ? 1 : 0));
    R.overlapOk = ok;
    LOG("[breach-overlap] " + (ok ? "OK" : ("FAIL:" + why)));
}

// 暴露给探针 / 后续阶段使用
R.config = cfg;
R.pickLevel = pickLevel;
R.curLevel = function () { return LEVELS[curLv].id; };
R.debugSeqCount = function () { return state.seqs ? state.seqs.length : 0; };
R.startHack = function () { newGame(); fadeTo(pageHack); };
R.startResult = function (win) {
    if (typeof win === "boolean") {
        state.win = win;
        var solved = 0;
        for (var s = 0; s < state.seqs.length; s++) if (isSubsequence(state.buffer, state.seqs[s].codes)) solved++;
        resStatus.setText(win ? "BREACH SUCCESS" : "BREACH FAILED");
        resStatus.setStyleTextColor(hex(win ? GREEN : RED), 0);
        resProgress.setText("SOLVED " + solved + " / " + state.seqs.length);
    }
    fadeTo(pageResult);
};

// Result → Retry（第三转场点，统一走 fadeTo）
function retry() {
    try { fadeTo(pageHack); newGame(); }
    catch (e) { R.jerryErrors++; LOG("[breach-err] retry: " + (e && e.message ? e.message : String(e))); }
}
R.retry = retry;

// ===================== 门禁：自动完整玩 N 局（含三转场 + retry + 超时路径） =====================
// 任何 Jerry error = 本局失败（try/catch 已隔离，R.jerryErrors 累加）。
// 仅逻辑层驱动，不依赖真实计时器；转场仍走真实 fadeTo/endGame，故缺符号必现形。
R.autotest = function (n) {
    var res = { games: n, errors: 0, wins: 0, loses: 0, timeouts: 0, retries: 0, jerryErrors: 0 };
    function pathSet() { var s = {}; for (var i = 0; i < state.path.length; i++) s[state.path[i]] = true; return s; }
    function firstSelectable(excludePath) {
        var ps = excludePath ? pathSet() : {};
        for (var r = 0; r < cfg.matrixRows; r++) for (var c = 0; c < cfg.matrixCols; c++) {
            var idx = r * cfg.matrixCols + c;
            if (state.picked[idx]) continue;
            if (excludePath && ps[idx]) continue;
            if (isSelectable(state.sel, { row: r, col: c })) return { r: r, c: c, idx: idx };
        }
        // 退路：任意可选
        for (var r2 = 0; r2 < cfg.matrixRows; r2++) for (var c2 = 0; c2 < cfg.matrixCols; c2++) {
            var i2 = r2 * cfg.matrixCols + c2;
            if (state.picked[i2]) continue;
            if (isSelectable(state.sel, { row: r2, col: c2 })) return { r: r2, c: c2, idx: i2 };
        }
        return null;
    }
    for (var g = 0; g < n; g++) {
        try {
            newGame();
            if (g < Math.floor(n * 0.5)) {
                // 胜利路径：沿 solution.path 顺序选（构造式可解 → 必全解）
                for (var p = 0; p < state.path.length; p++) {
                    if (!state.active) break;
                    var idx = state.path[p];
                    doSelect(Math.floor(idx / cfg.matrixCols), idx % cfg.matrixCols);
                }
                if (state.win) res.wins++; else res.loses++;
            } else if (g < Math.floor(n * 0.75)) {
                // 失败路径：缓冲填满（避开 path，尽量不误胜）
                var guard = 0;
                while (state.active && guard < 50) {
                    var cell = firstSelectable(true);
                    if (!cell) break;
                    doSelect(cell.r, cell.c);
                    guard++;
                }
                if (!state.win) res.loses++; else res.wins++;
            } else {
                // 超时路径：直接 endGame(false)（模拟 60s 耗尽）
                endGame(false);
                res.timeouts++;
            }
            // 第三转场：Result → Retry
            retry();
            res.retries++;
        } catch (e) {
            res.errors++;
            R.jerryErrors++;
            LOG("[breach-err] autotest game " + g + ": " + (e && e.message ? e.message : String(e)));
        }
    }
    res.jerryErrors = R.jerryErrors;
    R.autotestResult = res;
    LOG("[breach-autotest] " + JSON.stringify(res));
    return res;
};

audit();   // 对象账本快照（加载即固定；Hack 仅改状态不增删对象）
