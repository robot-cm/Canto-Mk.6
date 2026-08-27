// Calc - light-weight calculator, 240x240 round screen
// Layout: system header(y0-24) | mode toggle(y32) | expr(y54) | result(y70)
//   | key grid y104-228 (4 cols x 6 rows, cap fixed, caps swap per mode)
// STD : C DEL / * | 7 8 9 - | 4 5 6 + | 1 2 3 = | 0 . (grey) (grey)
// ADV : sin cos tan / | sqrt ^ % * | 7 8 9 - | 4 5 6 + | 1 2 3 = | 0 . C DEL
// Math: no eval; small state machine (acc/op/cur). Trig in DEGREES.
// ANSI caps only ("/ * - + = ^ %", "sqrt/DEL") - safe for built-in fonts.
// EVENT_CLICKED broken in this fork -> use EVENT_PRESSED (same as timer).

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "Calc");

var BG = 0x0E0E14;
var WHITE = 0xFFFFFF;
var GREY = 0x8A8F98;
var ACCENT = 0x4A90D9;
var KEY_OPA = 16;         // key idle bg
var KEY_OPA_PRESS = 46;   // pressed highlight
var DISABLED_OPA = 6;     // grey placeholder cells

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

// ===================== display area =====================
var modeBtn = new lv.button(R.root);
modeBtn.setSize(60, 20);
modeBtn.setPos(140, 32);
modeBtn.setStyleRadius(999, 0);
modeBtn.setStyleBgOpa(26, 0);
modeBtn.setStyleBgColor(hex(ACCENT), 0);
modeBtn.setStylePadAll(0, 0);
modeBtn.setStyleBorderWidth(0, 0);
modeBtn.setExtClickArea(6);
var modeLbl = new lv.label(modeBtn);
modeLbl.setSize(60, 20);
modeLbl.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
modeLbl.setFontSize(12);
modeLbl.setStyleTextColor(WHITE, 0);
modeLbl.setStyleTextOpa(240, 0);
modeLbl.setText("ADV");   // label = mode you switch TO
modeLbl.align(lv.ALIGN_CENTER, 0, 0);
modeLbl.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var exprLabel = new lv.label(R.root);
exprLabel.setSize(192, 16);   // 右缘 216 ≤ 圆 y52 处右界 219（透明背景，右对齐）
exprLabel.setPos(24, 52);
exprLabel.setFontSize(12);   // 12px 请求落 jbm_13（系统新小字号档）
exprLabel.setStyleTextColor(GREY, 0);
exprLabel.setStyleTextOpa(170, 0);
exprLabel.setStyleTextAlign(lv.TEXT_ALIGN_RIGHT, 0);
exprLabel.removeFlag(lv.OBJ_FLAG_CLICKABLE);
exprLabel.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

var resLabel = new lv.label(R.root);
resLabel.setSize(196, 30);   // 右缘 218 ≤ 圆 y70 处右界 229
resLabel.setPos(22, 70);
resLabel.setFontSize(18);
resLabel.setStyleTextColor(WHITE, 0);
resLabel.setStyleTextOpa(250, 0);
resLabel.setStyleTextAlign(lv.TEXT_ALIGN_RIGHT, 0);
resLabel.removeFlag(lv.OBJ_FLAG_CLICKABLE);
resLabel.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== key grid =====================
var COLS = 4, ROWS = 6;
var COL_W = 38, COL_GAP = 3, ROW_H = 21, ROW_GAP = 2;
var GRID_X = 40, GRID_Y = 90;

var STD = [["C", "DEL", "/", "*"],
           ["7", "8", "9", "-"],
           ["4", "5", "6", "+"],
           ["1", "2", "3", "="],
           ["0", ".", null, null],
           [null, null, null, null]];

var ADV = [["sin", "cos", "tan", "/"],
           ["sqrt", "^", "%", "*"],
           ["7", "8", "9", "-"],
           ["4", "5", "6", "+"],
           ["1", "2", "3", "="],
           ["0", ".", "C", "DEL"]];

var adv = false;
var keys = [];

function makeKey(r, c) {
    var b = new lv.button(R.root);
    b.setSize(COL_W, ROW_H);
    b.setPos(GRID_X + c * (COL_W + COL_GAP), GRID_Y + r * (ROW_H + ROW_GAP));
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(KEY_OPA, 0);
    b.setStyleBgColor(hex(WHITE), 0);
    b.setStylePadAll(0, 0);
    b.setStyleBorderWidth(0, 0);
    b.setExtClickArea(4);
    var l = new lv.label(b);
    l.setSize(COL_W, ROW_H);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(9);
    l.setStyleTextColor(WHITE, 0);
    l.setStyleTextOpa(230, 0);
    l.setText("");
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    var k = { btn: b, lbl: l, r: r, c: c, cap: null };
    b.addEventCb(function () {
        if (k.cap) b.setStyleBgOpa(KEY_OPA_PRESS, 0);   // press feedback (grey: none)
        dispatch(k.cap);
    }, lv.EVENT_PRESSED, null);
    b.addEventCb(function () {
        b.setStyleBgOpa(k.cap ? KEY_OPA : DISABLED_OPA, 0);
    }, lv.EVENT_RELEASED, null);
    keys.push(k);
}

for (var r = 0; r < ROWS; r++) {
    for (var c = 0; c < COLS; c++) { makeKey(r, c); }
}

// ===================== math state machine =====================
var acc = null;        // stored operand (null = none)
var op = null;         // pending binary op
var cur = "0";         // current entry (string)
var fresh = true;      // true: next digit starts a new operand
var over = false;      // error state

function num(s) { return parseFloat(s); }

function fmtNum(x) {
    if (typeof x !== "number" || isNaN(x) || !isFinite(x)) return "Error";
    var s = parseFloat(x.toPrecision(12)).toString();
    if (s.length > 14) s = x.toExponential(6);
    return s;
}

function evalOp(a, b, o) {
    switch (o) {
        case "add": return a + b;
        case "sub": return a - b;
        case "mul": return a * b;
        case "div": return b === 0 ? NaN : a / b;
        case "pow": return Math.pow(a, b);
        case "mod": return b === 0 ? NaN : a % b;
    }
    return b;
}

function opSym(o) {
    switch (o) {
        case "add": return "+";
        case "sub": return "-";
        case "mul": return "*";
        case "div": return "/";
        case "pow": return "^";
        case "mod": return "%";
    }
    return "";
}

function err() {
    over = true;
    cur = "Error";
    render();
}

function digit(d) {
    if (over) { clearAll(); }
    if (fresh) {
        cur = (d === ".") ? "0." : d;
        fresh = false;
    } else if (d === ".") {
        if (cur.indexOf(".") < 0) cur += ".";
    } else {
        if (cur === "0") cur = d;
        else if (cur === "-0") cur = "-" + d;
        else cur += d;
    }
    render();
}

function binop(o) {
    if (over) return;
    if (op !== null && !fresh) {                 // chain: settle previous first
        var r = evalOp(num(acc), num(cur), op);
        if (isNaN(r) || !isFinite(r)) return err();
        cur = fmtNum(r);
        acc = cur;
    } else if (op === null) {
        acc = cur;
    }
    op = o;
    fresh = true;
    render();
}

function equals() {
    if (over || op === null) return;
    var r = evalOp(num(acc), num(cur), op);
    if (isNaN(r) || !isFinite(r)) return err();
    acc = null;
    op = null;
    cur = fmtNum(r);
    fresh = true;
    render();
}

function unary(f) {
    if (over) return;
    var r = f(num(cur));
    if (isNaN(r) || !isFinite(r)) return err();
    cur = fmtNum(r);
    fresh = true;
    render();
}

function clearAll() {
    acc = null; op = null; cur = "0"; fresh = true; over = false;
    render();
}

function back() {
    if (over) { clearAll(); return; }
    if (fresh) return;                           // settled result: no backspace
    cur = cur.length > 1 ? cur.slice(0, -1) : "0";
    if (cur === "-") cur = "0";
    render();
}

function deg(f) {                                // trig in degrees
    return function (x) { return f(x * Math.PI / 180); };
}

function dispatch(cap) {
    if (!cap) return;
    switch (cap) {
        case "0": case "1": case "2": case "3": case "4":
        case "5": case "6": case "7": case "8": case "9":
            digit(cap); break;
        case ".": digit("."); break;
        case "/": binop("div"); break;
        case "*": binop("mul"); break;
        case "-": binop("sub"); break;
        case "+": binop("add"); break;
        case "^": binop("pow"); break;
        case "%": binop("mod"); break;
        case "=": equals(); break;
        case "C": clearAll(); break;
        case "DEL": back(); break;
        case "sin": unary(deg(Math.sin)); break;
        case "cos": unary(deg(Math.cos)); break;
        case "tan": unary(deg(Math.tan)); break;
        case "sqrt": unary(Math.sqrt); break;
    }
}

// ===================== render / mode =====================
function render() {
    resLabel.setText(cur);
    var expr = "";
    if (acc !== null) {
        expr = acc;
        if (op) expr += " " + opSym(op);
        if (op && !fresh) expr += " " + cur;
    }
    exprLabel.setText(expr);
    try { resLabel.setFontSize(cur.length > 12 ? 13 : 18); } catch (e) {}
}

function updateGrid() {
    var tbl = adv ? ADV : STD;
    for (var i = 0; i < keys.length; i++) {
        var k = keys[i];
        k.cap = tbl[k.r][k.c];
        if (k.cap) {
            k.lbl.setText(k.cap);
            k.lbl.setStyleTextOpa(230, 0);
            k.btn.setStyleBgOpa(KEY_OPA, 0);
        } else {
            k.lbl.setText("");
            k.btn.setStyleBgOpa(DISABLED_OPA, 0);
        }
    }
}

function toggleMode() {
    adv = !adv;
    modeLbl.setText(adv ? "STD" : "ADV");   // label = mode you switch TO
    updateGrid();
}

modeBtn.addEventCb(function () { toggleMode(); }, lv.EVENT_PRESSED, null);

// ===================== boot =====================
updateGrid();
render();
