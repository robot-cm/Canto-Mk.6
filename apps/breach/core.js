// breach/core.js — 纯游戏逻辑（ES5，无 LVGL）。唯一真源，可被 node 直接 require 跑压测。
// 规则以 github.com/yet3/cyberpunk2077-breach-protocol 为准（fetch 到）：
//   码池 ["55","BD","1C","E9","7A"]（5 个）；5x5；缓冲 5；序列 3（长 2-4）；初始选择方向 ROW/0（首选第0行）。
// 可解性采用构造式：先生成一条合法解路径，再从解中取连续子数组作为目标 → 天然可解。
(function (root) {
    "use strict";

    var CODES = ["55", "BD", "1C", "E9", "7A"];

    // mulberry32 —— 可种子 PRNG，保证压测可复现
    function mulberry32(seed) {
        var s = seed >>> 0;
        return function () {
            s = (s + 0x6D2B79F5) | 0;
            var t = Math.imul(s ^ (s >>> 15), 1 | s);
            t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        };
    }
    function randInt(rng, min, max) { return Math.floor(rng() * (max - min + 1)) + min; }
    function randCode(rng) { return CODES[randInt(rng, 0, CODES.length - 1)]; }
    function idxToRC(idx, cols) { return { col: idx % cols, row: Math.floor(idx / cols) }; }

    function defaultConfig() {
        return {
            matrixCols: 5, matrixRows: 5, bufferSize: 5,
            solutionSize: 5, numberOfSequences: 3,
            minSequenceSize: 2, maxSequenceSize: 4,
            time: 60, availableCodes: CODES.slice()
        };
    }

    // 构造式生成矩阵 + 解路径 + 序列（仓库 generateMatrix 移植）
    // 返回 { matrix, solution, path, solvable }
    function generateMatrix(rng, cfg) {
        var cols = cfg.matrixCols, rows = cfg.matrixRows, n = cols * rows;
        var matrix = [];
        for (var i = 0; i < n; i++) {
            var rc = idxToRC(i, cols);
            matrix.push({ idx: i, code: randCode(rng), col: rc.col, row: rc.row });
        }
        // 初始方向 ROW、初始线 value=0 → 首选第 0 行（与仓库 isCodeSelectable 一致）
        var dir = "ROW", row = 0, col = 0;
        var used = {};
        var path = [], solution = [], safety = 0, solvable = true;
        while (path.length < cfg.solutionSize && safety < 2000) {
            safety++;
            var free = [];
            if (dir === "COL") {
                for (var r = 0; r < rows; r++) { var ci = r * cols + col; if (!used[ci]) free.push(ci); }
            } else {
                for (var c = 0; c < cols; c++) { var ci2 = row * cols + c; if (!used[ci2]) free.push(ci2); }
            }
            if (free.length === 0) {
                var last = path.pop();
                if (last == null) { solvable = false; break; }
                used[last] = false;
                dir = (dir === "ROW") ? "COL" : "ROW";
                var lc = idxToRC(last, cols);
                row = lc.row; col = lc.col;
                continue;
            }
            var pick = free[randInt(rng, 0, free.length - 1)];
            used[pick] = true; path.push(pick);
            var pc = idxToRC(pick, cols); row = pc.row; col = pc.col;
            if (path.length < cfg.solutionSize) dir = (dir === "ROW") ? "COL" : "ROW";
        }
        if (path.length < cfg.solutionSize) solvable = false;
        for (var k = 0; k < path.length; k++) {
            var cc = randCode(rng);
            matrix[  path[k]].code = cc;
            solution.push(cc);
        }
        return { matrix: matrix, solution: solution, path: path, solvable: solvable };
    }

    // 从解里取子数组作为目标序列（仓库 generateSequences 移植 + 恒产 numberOfSequences 条修复）
    // 原版按首字节去重会清空 possible 提前 break → 序列数可能 < 3，导致多余目标标签停在占位符 "-- -- --"（横线）。
    // 修复：优先选首字节不同于已选者（视觉区分），无可选则退化为任意剩余 → 始终凑满 numberOfSequences 条。
    function generateSequences(rng, cfg, solution) {
        var possible = [];
        for (var z = 0; z < solution.length; z++) {
            for (var i = cfg.minSequenceSize; i <= solution.length - z; i++) {
                if (i <= cfg.maxSequenceSize) possible.push(solution.slice(z, z + i));
            }
        }
        var seqs = [];
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

    function filterArr(arr, pred) {
        var out = [];
        for (var i = 0; i < arr.length; i++) if (pred(arr[i])) out.push(arr[i]);
        return out;
    }

    // 连续子数组匹配（任意起点）—— 仓库 selectBreachFinishDetails 的匹配语义
    function isSubsequence(buf, seq) {
        if (seq.length > buf.length) return false;
        for (var i = 0; i + seq.length <= buf.length; i++) {
            var ok = true;
            for (var j = 0; j < seq.length; j++) { if (buf[i + j] !== seq[j]) { ok = false; break; } }
            if (ok) return true;
        }
        return false;
    }

    // 选择规则：direction=ROW 时只允许同行(row==value)，COL 时只允许同列(col==value)
    function isSelectable(sel, code) {
        return (sel.direction === "ROW") ? (sel.value === code.row) : (sel.value === code.col);
    }
    function afterSelect(sel, code) {
        var nd = (sel.direction === "ROW") ? "COL" : "ROW";
        var val = (sel.direction === "ROW") ? code.col : code.row;
        return { direction: nd, value: val };
    }

    var API = {
        CODES: CODES, mulberry32: mulberry32, randInt: randInt, randCode: randCode,
        idxToRC: idxToRC, defaultConfig: defaultConfig, generateMatrix: generateMatrix,
        generateSequences: generateSequences, isSubsequence: isSubsequence,
        isSelectable: isSelectable, afterSelect: afterSelect, filterArr: filterArr
    };
    if (typeof module !== "undefined" && module.exports) module.exports = API;
    if (root) root.BreachCore = API;
})(typeof window !== "undefined" ? window : this);
