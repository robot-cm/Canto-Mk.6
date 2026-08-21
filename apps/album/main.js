// ElenixOS 相册 — P1 真窗口探针（视觉验证 png/jpg 解码 + FM 跳转 + album_pick 回传）
// id = com.elenix.album（与 FM 拦截回传写死的 app id 一致，验证最终生产链路）
//
// 布局：img 160x160 居中(y8) / 状态行 y178 font10 / 底部三胶囊 y200 [PNG][JPG][FM]
// 行为：PNG→setSrc 0.png；JPG→setSrc test.jpg；FM→eos.app.openFiles()；
//       1s 轮询 album_pick（FM 选图回传后显示并清空）
// 红线：opa 裸数字 / hex 数字 / setFontSize / 自建 R.root 全屏 radius0 禁滚 /
//       游标布局禁 flex / 胶囊 radius999 禁 border fill 区分 / label align CENTER /
//       timer 1000ms + setRepeatCount(-1) / 中文 / 标题走 statusbar

var activity = eos.activity.current();
var view = eos.activity.getView(activity);
eos.activity.setTitle(activity, "相册探针");

var R = {};
var BG = 0x12121A;
var WHITE = 0xFFFFFF;
var ACCENT = 0x4A90D9;
var GREY = 0x8A8F98;

function hex(v) { return lv.color.hex(v); }

// ===================== R.root 全屏容器 =====================
R.root = new lv.obj(view);
R.root.setSize(240, 240);
R.root.setPos(0, 0);
R.root.setStyleRadius(0, 0);
R.root.setStyleBgOpa(255, 0);
R.root.setStyleBgColor(hex(BG), 0);
R.root.setStylePadAll(0, 0);
R.root.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
R.root.setScrollbarMode(0);

// ===================== 图片区 160x160 =====================
var img = new lv.image(R.root);
img.setSize(160, 160);
img.setPos(40, 8);
img.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
img.setScrollbarMode(0);

// ===================== 状态行 =====================
var status = new lv.label(R.root);
status.setSize(240, 14);
status.setPos(0, 178);
status.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
status.setFontSize(10);
status.setStyleTextColor(hex(GREY), 0);
status.setText("点下方按钮");
status.removeFlag(lv.OBJ_FLAG_SCROLLABLE);

// ===================== 胶囊按钮 =====================
function pill(x, text, color) {
    var b = new lv.button(R.root);
    b.setSize(50, 26);
    b.setPos(x, 200);
    b.setStyleRadius(999, 0);
    b.setStyleBgOpa(255, 0);
    b.setStyleBgColor(hex(color), 0);
    b.setStyleBorderWidth(0, 0);
    b.setStylePadAll(0, 0);
    b.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    var l = new lv.label(b);
    l.setSize(50, 26);
    l.setStyleTextAlign(lv.TEXT_ALIGN_CENTER, 0);
    l.setFontSize(12);
    l.setStyleTextColor(hex(WHITE), 0);
    l.setText(text);
    l.align(lv.ALIGN_CENTER, 0, 0);
    l.removeFlag(lv.OBJ_FLAG_SCROLLABLE);
    return b;
}
var bPng = pill(41, "PNG", ACCENT);
var bJpg = pill(95, "JPG", ACCENT);
var bFm  = pill(149, "FM", 0x3A6EA5);

// ===================== 行为 =====================
bPng.addEventCb(function () {
    img.setSrc("/sdcard/ALBUM/0.png");
    status.setText("PNG: /sdcard/ALBUM/0.png");
}, lv.EVENT_PRESSED, null);

bJpg.addEventCb(function () {
    img.setSrc("/sdcard/ALBUM/test.jpg");
    status.setText("JPG: /sdcard/ALBUM/test.jpg");
}, lv.EVENT_PRESSED, null);

bFm.addEventCb(function () {
    eos.app.openFiles();
    status.setText("FM 已打开，去选一张图");
}, lv.EVENT_PRESSED, null);

// 回传轮询：1s 查 album_pick（FM 选图 -> 写 com.elenix.album/config.json -> back）
// 命中 -> 体积护栏（>300KB 跳过）-> 自动 setSrc 加载该图（P1 完整链路验证）
var poll = new lv.timer(function () {
    var pick = "";
    try { pick = eos.config.getStr("album_pick"); } catch (e) {}
    if (pick && pick.length > 0) {
        var sz = 0;
        try { sz = eos.fs.size(pick); } catch (e) {}
        if (sz > 300 * 1024) {
            status.setText("图过大 " + Math.floor(sz / 1024) + "KB 跳过");
        } else if (sz <= 0) {
            status.setText("文件不存在/不可读 " + pick);
        } else {
            img.setSrc(pick);
            status.setText("已加载 " + pick);
        }
        try { eos.config.setStr("album_pick", ""); } catch (e) {}
    }
}, 1000, null);
poll.setRepeatCount(-1);
