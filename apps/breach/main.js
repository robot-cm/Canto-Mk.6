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
var curLv = 1;   // 默认等级 B（4x4，矩阵底 y156 与目标序列 y178 拉开间距，避免贴边/重叠）
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
// 参考仓库 Sequence.tsx 的连续匹配算法:
// 从 seqIdx=0 起遍历玩家点击 buffer;匹配成功则 seqIdx++(picked 置位),匹配到末尾=solved;
// 失配则重置 seqIdx/picked,并从当前点击位置(i)重新对齐(若当前码==序列首码则 seqIdx=1),
// offset=序列在 buffer 中的对齐起点(首格缩进单位),随点击窗口滑动。
function evalSeq(seq, buf) {
    var seqIdx = 0, picked = [], offset = 0, i;
    for (i = 0; i < buf.length; i++) {
        var code = buf[i];
        if (code === seq[seqIdx]) {
            picked[seqIdx] = true;
            seqIdx++;
            if (seqIdx === seq.length)
                return { solved: true, offset: offset, picked: picked, seqIdx: seqIdx };
        } else {
            seqIdx = 0;
            picked = [];
            var offsetIdx = i + 1;
            if (code === seq[0]) { seqIdx = 1; picked[0] = true; offsetIdx = i; }
            offset = Math.min(offsetIdx, cfg.bufferSize - seq.length);
        }
    }
    return { solved: false, offset: offset, picked: picked, seqIdx: seqIdx };
}
// 逐序列状态更新:IN_PROGRESS → SOLVED / FAILED(参考:剩余未匹配码数 > buffer 剩余槽位 → 不可能完成)
function updateSeqStatus() {
    for (var s = 0; s < state.seqs.length; s++) {
        if (state.seqs[s].status !== "IN_PROGRESS") continue;
        var ev = evalSeq(state.seqs[s].codes, state.buffer);
        if (ev.solved) { state.seqs[s].status = "SOLVED"; continue; }
        if (state.seqs[s].codes.length - ev.seqIdx > cfg.bufferSize - state.buffer.length)
            state.seqs[s].status = "FAILED";
    }
}
function countInProgress() {
    var n = 0;
    for (var s = 0; s < state.seqs.length; s++)
        if (state.seqs[s].status === "IN_PROGRESS") n++;
    return n;
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
bootTitle.setFontSize(14);
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
        if (ti >= full.length) { tw.delete(); newGame(); fadeTo(pageHack); }   // 打字结束直接开局（无逐格进度条）
    }, 50, null);
    tw.setRepeatCount(-1);
    tw.setAutoDelete(true);
}
startBoot();

// ===================== P2 Hack 页（P1 完整实现） =====================
var MAX_N = 6, MAX_BUF = 6;   // 对象池上限（等级 D 6×6；缓冲槽 6）
var CELL_W = 30, CELL_H = 20, CELL_GAP = 3, CELL_R = 2;   // 横向 gap3
var MX_X0 = 39, MX_Y0 = 70;
var COL_STEP = CELL_W + CELL_GAP;   // 33
var ROW_STEP = CELL_H + 2;          // 22（纵向 gap2 → 5 行 y70/92/114/136/158，底 178）

// 按等级动态布局：5x5（C）及以下 cell30x18 行距19；6x6（D）cell27x16 行距17。
// 矩阵垂直居中于缓冲槽底(60)与目标序列顶(164)之间（中心 y112）；矩阵与圆边空隙：5x5 底159、6x6 底162。
function layoutFor(c) {
    var n = c.matrixCols;
    var cellW, cellH, colStep, rowStep, x0;
    if (n <= 5) {
        cellW = 30; cellH = 18; colStep = 33; rowStep = 19; x0 = 39;
    } else {
        cellW = 27; cellH = 16; colStep = 30; rowStep = 17;
        var total = n * cellW + (n - 1) * (colStep - cellW);
        x0 = Math.floor((240 - total) / 2);
    }
    var h = n * cellH + (n - 1) * (rowStep - cellH);
    var y0 = 60 + Math.floor((104 - h) / 2);
    return { cellW: cellW, cellH: cellH, colStep: colStep, rowStep: rowStep, x0: x0, y0: y0 };
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
            cell.lab.setFontSize(10);   // jbm_10（系统新小字号档）
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
            bufSlots[i].obj.setPos(bx0 + i * (BUF_W + BUF_GAP), 44);
        } else {
            bufSlots[i].obj.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
    for (var t = 0; t < tgtBgs.length; t++) {
        if (t < cfg.numberOfSequences) {
            tgtBgs[t].removeFlag(lv.OBJ_FLAG_HIDDEN);
        } else {
            tgtBgs[t].addFlag(lv.OBJ_FLAG_HIDDEN);
            for (var tc = 0; tc < MAX_TGT_COL; tc++) tgtCells[t][tc].obj.addFlag(lv.OBJ_FLAG_HIDDEN);
        }
    }
}

// --- HUD：仅右计时（状态栏已承载"入侵协议"；页内左标题删，避免重复+被状态栏裁）---
var hackTimer = new lv.label(pageHack);
hackTimer.setSize(30, 18);
hackTimer.setPos(160, 24);         // 计时 y24..42；缓冲槽 y44..60 不重叠
hackTimer.setFontSize(18);
hackTimer.setStyleTextColor(hex(YELLOW), 0);
hackTimer.setStyleTextLetterSpace(2, 0);
hackTimer.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
hackTimer.setText("60");

// 等级标签（左上；当前等级 LV X，font10 灰）
var lvTag = new lv.label(pageHack);
lvTag.setSize(50, 18);
lvTag.setPos(10, 24);
lvTag.setFontSize(10);
lvTag.setStyleTextColor(hex(GRAY), 0);
lvTag.setStyleTextLetterSpace(1, 0);
lvTag.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
lvTag.setText("LV C");

// --- 缓冲槽（对象池 6，按等级显示 3..6）---
var BUF_W = 26, BUF_H = 16, BUF_GAP = 4;
var bufSlots = [];
for (var bi = 0; bi < MAX_BUF; bi++) {
    var so = new lv.obj(pageHack);
    so.setSize(BUF_W, BUF_H);
    so.setPos(0, 44);                    // 位置由 applyLayout 按等级设置
    so.setStyleRadius(CELL_R, 0);
    so.setStyleBgColor(hex(0x26262E), 0);          // 空槽填充提亮（原 0x1A1A20 近 BG 不可见）
    so.setStyleBgOpa(255, 0);
    so.setStyleBorderWidth(1, 0);                   // 槽轮廓，空态也看出是槽
    so.setStyleBorderColor(hex(0x55585F), 0);
    so.setStyleBorderOpa(180, 0);
    so.setStylePadAll(0, 0);   // 清默认 padding（对齐格子已验证模式），否则 26×16 槽内容区被挤压，label 不显示
    so.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    so.setScrollbarMode(0);
    so.addFlag(lv.OBJ_FLAG_HIDDEN);      // 池对象初始隐藏
    var sl = new lv.label(so);
    sl.setSize(BUF_W, BUF_H);
    sl.setPos(0, 0);
    sl.setLongMode(lv.LABEL_LONG_CLIP);             // 禁换行（对齐格子）
    sl.setFontSize(12);   // jbm_13（12px 请求落 13px 档）
    sl.setStyleTextColor(hex(GRAY), 0);
    sl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    sl.setStyleTextLetterSpace(1, 0);               // LS1 防双字符截断（对齐格子）
    sl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
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
            l.setFontSize(10);   // jbm_10（系统新小字号档）
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

// --- 目标序列（3 行 × 5 列格子池,复刻仓库 Sequence 行:左对齐 + offset 缩进 + 逐序列锁定）---
// 参考 Sequence.tsx:格子从容器左侧开始(offset=0 左对齐),offset=序列在玩家点击 buffer 中的对齐起点,
// 首格 marginLeft=offset*(格宽+间距);solved 行全绿锁定(INSTALLED),failed 行全红锁定(FAILED)。
// 圆屏:y206 行圆界 x36..204;序列左端右移 8px 避免贴边,胶囊条 160 宽 x44..204;
// 格子右缘=44+offset*22+len*22 ≤ 44+(bufSize-len)*22+len*22 = 154。
var TGT_Y = [164, 185, 206];
var MAX_TGT_COL = 5;                 // 等级 D 最长 5 列
var TGT_W = 20, TGT_H = 19, TGT_GAP = 2, TGT_STEP = TGT_W + TGT_GAP;   // 22
var TGT_X0 = 44;                     // 序列行左对齐起点(圆界右移 8px)
var tgtBgs = [];
var tgtCells = [];
var tgtSts = [];                     // 锁定状态 label(INSTALLED/FAILED,覆盖整行)
for (var ti = 0; ti < 3; ti++) {
    var tb = new lv.obj(pageHack);              // 序列深色胶囊背景条（先创建 → z 序在格子下方）
    tb.setSize(160, 19);
    tb.setPos(TGT_X0, TGT_Y[ti]);
    tb.setStyleRadius(2, 0);
    tb.setStyleBgColor(hex(0x15151B), 0);
    tb.setStyleBgOpa(255, 0);
    tb.setStyleBorderWidth(0, 0);
    tb.setStylePadAll(0, 0);
    tb.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    tb.setScrollbarMode(0);
    tb.addFlag(lv.OBJ_FLAG_HIDDEN);
    tgtBgs.push(tb);
    tgtCells[ti] = [];
    for (var tc = 0; tc < MAX_TGT_COL; tc++) {
        (function (rr, cc) {
            var g = new lv.obj(pageHack);       // 每码一格（同矩阵格风）
            g.setSize(TGT_W, TGT_H);
            g.setPos(0, 0);
            g.setStyleRadius(2, 0);
            g.setStyleBgColor(hex(0x0D0D12), 0);
            g.setStyleBgOpa(255, 0);
            g.setStyleBorderWidth(0, 0);
            g.setStylePadAll(0, 0);
            g.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
            g.setScrollbarMode(0);
            g.addFlag(lv.OBJ_FLAG_HIDDEN);      // 池对象初始隐藏
            var gl = new lv.label(g);
            gl.setSize(TGT_W, TGT_H);
            gl.setPos(0, 0);
            gl.setLongMode(lv.LABEL_LONG_CLIP); // 禁换行
            gl.setFontSize(7);                  // 7px 请求落 jbm_10 小字号档
            gl.setStyleTextColor(hex(0xC8CCD4), 0);
            gl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
            gl.setStyleTextLetterSpace(1, 0);
            gl.setText("");
            tgtCells[rr][cc] = { obj: g, lab: gl };
        })(ti, tc);
    }
    // 锁定状态文本:必须最后创建且挂 pageHack(在格子上层),锁定后盖整行显示 INSTALLED/FAILED
    var ts = new lv.label(pageHack);
    ts.setSize(160, 19);
    ts.setPos(TGT_X0, TGT_Y[ti]);
    ts.setFontSize(11);
    ts.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    ts.setStyleTextLetterSpace(1, 0);
    ts.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    ts.removeFlag(lv.OBJ_FLAG_CLICKABLE);
    ts.setText("");
    ts.addFlag(lv.OBJ_FLAG_HIDDEN);
    tgtSts.push(ts);
}

// --- 状态 ---
var state = { active: false, buffer: [], picked: {}, pickOrder: [], sel: { direction: "ROW", value: 0 }, seqs: [], matrix: [], solution: [], path: [], timeLeft: 60, win: false, round: 0, countdownStarted: false };
var cfg = defaultConfig();

// --- 游戏计时器（池化常驻；state.active 门控）---
var gameTimer = safeTimer(function () {
    if (!state.active || !state.countdownStarted) return;   // 计时从第一个号码按下才开始
    state.timeLeft--;
    if (state.timeLeft < 0) state.timeLeft = 0;
    hackTimer.setText(String(state.timeLeft));
    if (state.timeLeft <= 0) endGame(true);   // 超时:status=TIMED OUT,部分 solved 算成功
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
    state.countdownStarted = false;   // 每次新局：等待第一次点击才启动计时
    hackTimer.setText(String(state.timeLeft));
    hackTimer.removeFlag(lv.OBJ_FLAG_HIDDEN);   // Fix1：retry/重开恢复计时可见
    refreshAll();
    if (!R._matrixAudited) { R._matrixAudited = true; auditMatrix(); }
}

function doSelect(r, c) {
    if (!state.active) return;
    // 点击落点不在高亮行/列时，自动投影到当前高亮线（保留另一维坐标，换掉当前维度坐标）。
    // 仅为了方便点到，不改变玩法规则（isSelectable / afterSelect / picked 语义均不变）。
    if (!isSelectable(state.sel, { row: r, col: c })) {
        if (state.sel.direction === "ROW") { r = state.sel.value; }   // 强制行=高亮行，保留所点列
        else { c = state.sel.value; }                                  // 强制列=高亮列，保留所点行
    }
    var idx = r * cfg.matrixCols + c;
    if (state.picked[idx]) return;
    if (!isSelectable(state.sel, { row: r, col: c })) return;
    state.picked[idx] = true;
    state.pickOrder.push(idx);
    state.buffer.push(state.matrix[idx].code);
    state.sel = afterSelect(state.sel, { row: r, col: c });
    if (!state.countdownStarted) state.countdownStarted = true;   // 第一个号码按下 → 开始倒计时
    refreshAll();
    flashCell(cells[r][c]);
    // 逐序列判定(参考):全非 in-progress(全部 solved/failed)或缓冲满 → 终局
    updateSeqStatus();
    if (countInProgress() === 0) { endGame(false); return; }
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
    for (var s = 0; s < tgtCells.length; s++) {
        var rowCells = tgtCells[s];
        var show = (s < cfg.numberOfSequences && s < state.seqs.length);
        if (!show) {
            for (var tc = 0; tc < MAX_TGT_COL; tc++) rowCells[tc].obj.addFlag(lv.OBJ_FLAG_HIDDEN);
            tgtBgs[s].addFlag(lv.OBJ_FLAG_HIDDEN);
            tgtSts[s].addFlag(lv.OBJ_FLAG_HIDDEN);
            continue;
        }
        tgtBgs[s].removeFlag(lv.OBJ_FLAG_HIDDEN);
        var seq = state.seqs[s].codes;
        var status = state.seqs[s].status;
        var y = TGT_Y[s];
        // 锁定:SOLVED 全绿(INSTALLED) / FAILED 全红(FAILED),格子不再变化
        if (status === "SOLVED" || status === "FAILED") {
            var isOk = (status === "SOLVED");
            for (var t2 = 0; t2 < MAX_TGT_COL; t2++) {
                var c2 = rowCells[t2];
                if (t2 < seq.length) {
                    c2.lab.setText(seq[t2]);
                    c2.lab.setStyleTextColor(hex(isOk ? GREEN : RED), 0);
                    c2.obj.setStyleBgColor(hex(isOk ? 0x0A2A12 : 0x2A0A0F), 0);
                    c2.obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
                    c2.obj.setPos(TGT_X0 + t2 * TGT_STEP, y);
                } else {
                    c2.obj.addFlag(lv.OBJ_FLAG_HIDDEN);
                }
            }
            tgtSts[s].setText(isOk ? "INSTALLED" : "FAILED");
            tgtSts[s].setStyleTextColor(hex(isOk ? GREEN : RED), 0);
            tgtSts[s].removeFlag(lv.OBJ_FLAG_HIDDEN);
            continue;
        }
        tgtSts[s].addFlag(lv.OBJ_FLAG_HIDDEN);
        // in-progress:左对齐 + offset 缩进(序列在点击 buffer 中的对齐起点),已匹配码黄,其余灰
        var ev = evalSeq(seq, state.buffer);
        var x0 = TGT_X0 + ev.offset * TGT_STEP;
        for (var t3 = 0; t3 < MAX_TGT_COL; t3++) {
            var c3 = rowCells[t3];
            if (t3 < seq.length) {
                c3.lab.setText(seq[t3]);
                c3.lab.setStyleTextColor(hex(ev.picked[t3] ? YELLOW : 0xC8CCD4), 0);
                c3.obj.setStyleBgColor(hex(0x0D0D12), 0);
                c3.obj.removeFlag(lv.OBJ_FLAG_HIDDEN);
                c3.obj.setPos(x0 + t3 * TGT_STEP, y);
            } else {
                c3.obj.addFlag(lv.OBJ_FLAG_HIDDEN);
            }
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

// 终局判定(参考 selectBreachFinishDetails):逐序列统计 solved/done
// all solved → ALL DAEMONS UPLOADED;无 in-progress 且部分 solved → "x/y DAEMONS UPLOADED"(成功);
// buffer 满/全灭 → BUFFER FULL(失败);超时 → solved>0 算成功
function computeFinish(timeout) {
    var total = state.seqs.length, solved = 0, done = 0;
    for (var s = 0; s < total; s++) {
        if (state.seqs[s].status !== "IN_PROGRESS") {
            done++;
            if (state.seqs[s].status === "SOLVED") solved++;
        }
    }
    if (solved >= total)
        return { isSuccess: true, solved: solved, total: total, status: "ALL DAEMONS UPLOADED" };
    if (done === total && solved > 0)
        return { isSuccess: true, solved: solved, total: total, status: solved + "/" + total + " DAEMONS UPLOADED" };
    if (timeout)
        return { isSuccess: solved > 0, solved: solved, total: total, status: "TIMED OUT" };
    return { isSuccess: false, solved: solved, total: total, status: "BUFFER FULL" };
}

function endGame(timeout) {
    if (!state.active) return;
    state.active = false;
    var fin = computeFinish(timeout);
    state.win = fin.isSuccess;
    resStatus.setText(fin.isSuccess ? "BREACH SUCCESS" : "BREACH FAILED");
    resStatus.setStyleTextColor(hex(fin.isSuccess ? GREEN : RED), 0);
    hackTimer.addFlag(lv.OBJ_FLAG_HIDDEN);   // Fix1：失败/胜利页不留残亮倒计时（overlay Opa230 仍透出"0"）
    fadeTo(pageResult);   // overlay over Hack（不隐藏 Hack，作暗化底）
    startTerminal(fin);   // 结束动画:终端逐序列混合打印(绿=成功/红=失败)
}

// --- 结束动画:终端打字机(逐序列混合打印,复刻仓库 MatrixFinish 语义)---
// 每条序列一行独立 label:成功行绿色 "//DAEMON_x INSTALLED"、失败行红色 "//DAEMON_x FAILED",
// 逐行逐字符打字;全部行完成后状态行出现(ALL DAEMONS UPLOADED / "x/y DAEMONS UPLOADED" / BUFFER FULL / TIMED OUT)。
// LVGL label 单色限制 → 每行一个 label,不追求参考 Code.tsx 的整块 SUCCESS/FAILURE_CODE 二选一。
var CARET_CHARS = "|<>/[]{}* #";
var typeTimer = null;

function startTerminal(fin) {
    var rows = [];
    for (var s = 0; s < state.seqs.length; s++) {
        var ok = (state.seqs[s].status === "SOLVED");
        rows.push({ text: (ok ? "//DAEMON_" + (s + 1) + " INSTALLED" : "//DAEMON_" + (s + 1) + " FAILED"), ok: ok });
    }
    var anyOk = fin.solved > 0;
    termBox.setStyleBorderColor(hex(anyOk ? GREEN : RED), 0);
    termBox.setStyleBgColor(hex(anyOk ? 0x07140B : 0x140708), 0);
    resProgress.setStyleTextColor(hex(anyOk ? GREEN : RED), 0);
    resProgress.setText(fin.status);
    resProgress.addFlag(lv.OBJ_FLAG_HIDDEN);   // 打字机结束才出现
    for (var i = 0; i < termLbls.length; i++) {
        termLbls[i].setText("");
        if (i < rows.length) termLbls[i].setStyleTextColor(hex(rows[i].ok ? GREEN : RED), 0);
    }
    if (typeTimer) { try { typeTimer.delete(); } catch (e2) {} typeTimer = null; }
    var ri = 0, ci = 0;
    typeTimer = safeTimer(function () {
        if (ri >= rows.length) {
            typeTimer.delete();
            typeTimer = null;
            // 状态行出现 + 闪烁 4 次
            resProgress.removeFlag(lv.OBJ_FLAG_HIDDEN);
            var flick = 0;
            var ft = safeTimer(function () {
                flick++;
                if (flick % 2 === 1) resProgress.addFlag(lv.OBJ_FLAG_HIDDEN);
                else resProgress.removeFlag(lv.OBJ_FLAG_HIDDEN);
                if (flick >= 8) { try { ft.delete(); } catch (e3) {} }
            }, 100, null);
            ft.setRepeatCount(-1);
            ft.setAutoDelete(true);
            return;
        }
        var row = rows[ri];
        ci += 3;   // 每帧 3 字符
        if (ci >= row.text.length) {
            termLbls[ri].setText(row.text);
            ri++;
            ci = 0;
        } else {
            var caret = CARET_CHARS.charAt(Math.floor(Math.random() * CARET_CHARS.length));
            termLbls[ri].setText(row.text.substring(0, ci) + caret);
        }
    }, 30, null);
    typeTimer.setRepeatCount(-1);
    typeTimer.setAutoDelete(true);
}

// ===================== P3 Result 内容（overlay Opa230 + 标题 + 终端打字机 + INSTALLED/FAILED + RETRY + Glitch） =====================
// 复刻仓库 MatrixFinish：结束（无论成败）在终端窗口内逐字符打字输出代码块（小字号 font7 适配 240 屏）。
var resStatus = new lv.label(pageResult);
resStatus.setSize(220, 18);
resStatus.setPos(10, 28);
resStatus.setFontSize(12);
resStatus.setStyleTextColor(hex(GREEN), 0);
resStatus.setStyleTextLetterSpace(2, 0);
resStatus.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
resStatus.setText("BREACH SUCCESS");

// 终端窗口（打字机容器；圆屏约束：(30,50)-(214,182) 四角距圆心 ≥117 留空隙；点击终端=重开，替代独立 RETRY 按钮）
var termBox = new lv.obj(pageResult);
termBox.setSize(184, 132);
termBox.setPos(30, 50);
termBox.setStyleRadius(2, 0);
termBox.setStyleBgColor(hex(0x0A0A0F), 0);
termBox.setStyleBgOpa(255, 0);
termBox.setStyleBorderWidth(1, 0);
termBox.setStyleBorderColor(hex(GREEN), 0);
termBox.setStyleBorderOpa(255, 0);
termBox.setStylePadAll(0, 0);
termBox.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
termBox.setScrollbarMode(0);
termBox.addFlag(lv.OBJ_FLAG_CLICKABLE);
termBox.addEventCb(function () {
    try { retry(); }
    catch (e) { R.jerryErrors++; LOG("[breach-err] retry: " + (e && e.message ? e.message : String(e))); }
}, lv.EVENT_PRESSED, null);
// 终端行 label(最多 3 行,对应 3 条序列;每行独立颜色:绿=INSTALLED 红=FAILED)
// 字号 jbm_10(11px 档);"//DAEMON_1 INSTALLED" 20 字符 ≈120px ≤ 172 宽
var termLbls = [];
for (var tli = 0; tli < 3; tli++) {
    (function (i) {
        var tl = new lv.label(termBox);
        tl.setSize(172, 15);
        tl.setPos(6, 6 + i * 17);   // 行 y6/23/40,底 55;状态行 y102 不重叠
        tl.setLongMode(lv.LABEL_LONG_CLIP);
        tl.setFontSize(11);
        tl.setStyleTextColor(hex(GREEN), 0);
        tl.setStyleTextLetterSpace(0, 0);
        tl.setStyleTextAlign(lv.TEXT_ALIGN_LEFT, 0);
        tl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
        tl.setText("");
        termLbls.push(tl);
    })(tli);
}

// 状态行（终端窗口内,打字机结束后出现）：INSTALLED / FAILED / 终局统计
// 13px+字距1 下 "ALL DAEMONS UPLOADED" ≈170px ≤184,setLongMode(CLIP) 禁止换行,否则第二行被 19px 高裁掉
var resProgress = new lv.label(termBox);
resProgress.setSize(184, 19);
resProgress.setPos(0, 96);
resProgress.setFontSize(13);   // jbm_13
resProgress.setStyleTextColor(hex(GREEN), 0);
resProgress.setStyleTextLetterSpace(1, 0);
resProgress.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
resProgress.setLongMode(lv.LABEL_LONG_CLIP);
resProgress.setText("INSTALLED");
resProgress.addFlag(lv.OBJ_FLAG_HIDDEN);

// TAP TO RETRY 提示（终端内底部,常显灰字）
var tapHint = new lv.label(termBox);
tapHint.setSize(184, 15);
tapHint.setPos(0, 116);
tapHint.setFontSize(13);   // jbm_13（行高 16，y123..139 超出终端 132，裁 1px 可忽略）
tapHint.setStyleTextColor(hex(0x9AA0A6), 0);
tapHint.setStyleTextLetterSpace(1, 0);
tapHint.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
tapHint.setText("TAP TO RETRY");
tapHint.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// --- 等级选择（DIFF，y196；点击即切换难度并立即重开；英文缩写 3x3/4x4/5x5/6x6）---
var lvBtns = [];
// 34x20 gap3 → 总宽 145，居中 x47..192；y188..208 圆界 x38..202，四周留 ≥9px 空隙（原 48x24 y196 右端出圆 25px）
var lvBtnW = 34, lvBtnH = 20, lvBtnGap = 3;
var lvTotal = LEVELS.length * lvBtnW + (LEVELS.length - 1) * lvBtnGap;   // 145
var lvX0 = (240 - lvTotal) / 2;                                          // 47.5
for (var li = 0; li < LEVELS.length; li++) {
    (function (i) {
        var lb = new lv.obj(pageResult);
        lb.setSize(lvBtnW, lvBtnH);
        lb.setPos(Math.floor(lvX0 + i * (lvBtnW + lvBtnGap)), 188);
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
        ll.setFontSize(14);   // 14px 请求落 jbm_13（"6X6" 23.4px ≤ 34 宽，行高16 ≤ 20）
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
            resStatus.setPos(10 + jx, 28);
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
    resStatus.setPos(10, 28);
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
        + " cells=" + (MAX_N * MAX_N) + " buf=" + MAX_BUF + " tgtRows=" + tgtCells.length
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
    // 目标序列格子审计：每行格子须 ⊆ 各自胶囊条（行内不重叠、不越界）
    for (var ti = 0; ti < tgtCells.length; ti++) {
        for (var tc = 0; tc < MAX_TGT_COL; tc++) {
            var tx = "?";
            try { tx = tgtCells[ti][tc].lab.getText(); } catch (e2) {}
            LOG("[breach-target-text] t" + ti + "c" + tc + "=\"" + tx + "\"");
        }
    }
    for (var i = 0; i < cfg.numberOfSequences; i++) {
        var gb = boxOf(tgtBgs[i]);
        tb.push(gb);
        LOG("[breach-overlap] t" + i + "=(" + gb.x1 + "," + gb.y1 + ")-(" + gb.x2 + "," + gb.y2 + ") w" + gb.w + " h" + gb.h);
        if (gb.h > 19) { ok = false; why += " t" + i + ".h=" + gb.h + ">19"; }
        var gx0 = 999, gx2 = -1, gy0 = 999, gy2 = -1;
        for (var tc2 = 0; tc2 < MAX_TGT_COL; tc2++) {
            var cc2 = tgtCells[i][tc2];
            if (cc2.obj.hasFlag(lv.OBJ_FLAG_HIDDEN)) continue;
            var cb2 = boxOf(cc2.obj);
            if (cb2.x1 < gx0) gx0 = cb2.x1;
            if (cb2.x2 > gx2) gx2 = cb2.x2;
            if (cb2.y1 < gy0) gy0 = cb2.y1;
            if (cb2.y2 > gy2) gy2 = cb2.y2;
            if (!(cb2.x1 >= gb.x1 && cb2.y1 >= gb.y1 && cb2.x2 <= gb.x2 && cb2.y2 <= gb.y2)) {
                ok = false; why += " tgtcell" + i + "," + tc2 + " overflow";
            }
        }
        if (gx0 !== 999) {
            LOG("[breach-overlap] t" + i + "cells span (" + gx0 + "," + gy0 + ")-(" + gx2 + "," + gy2 + ")");
        }
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
        state.active = false;
        resStatus.setText(win ? "BREACH SUCCESS" : "BREACH FAILED");
        resStatus.setStyleTextColor(hex(win ? GREEN : RED), 0);
    }
    fadeTo(pageResult);
    if (typeof win === "boolean") startTerminal({ solved: win ? 1 : 0, total: 1, status: win ? "SUCCESS" : "FAILED" });
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
                // 超时路径：endGame(true)（模拟 60s 耗尽）
                endGame(true);
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
