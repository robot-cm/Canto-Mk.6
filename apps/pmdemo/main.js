// ElenixOS Plugin Manager — real-chain demo (main.js)
//
// Proves the full plugin chain:
//   scan .eapk -> parse manifest -> register with Plugin Manager ->
//   Launcher shows the app -> tap launches it via SPM ->
//   JerryScript runs THIS script -> Back releases all resources.
//
// JS namespaces available:
//   eos  -> activity / console / appHeader / view / time / ...
//   lv    -> label / button / obj / timer / color / ... (LVGL bindings)

var activity = eos.activity.current();
var view = eos.activity.getView(activity);

eos.activity.setAppHeaderVisible(activity, true);

// Title
var title = new lv.label(view);
title.setText("Plugin OK");
title.setStyleTextColor(lv.color.hex(0xFFFFFF), 0);
title.center();

// Subtitle / status line
var status = new lv.label(view);
status.setText("ElenixOS plugin chain");
status.setStyleTextColor(lv.color.hex(0x9AA4B2), 0);
status.align(lv.ALIGN_BOTTOM_MID, 0, -46);

// Back button -> exits the app and releases every resource it created
var back = new lv.button(view);
back.setSize(120, 44);
back.align(lv.ALIGN_BOTTOM_MID, 0, -16);
back.setStyleBgColor(lv.color.hex(0x2F80ED), 0);

var blabel = new lv.label(back);
blabel.setText("Back");
blabel.center();

back.addEventCb(function (e) {
    eos.console.log("[pmdemo] back pressed -> exiting app");
    eos.activity.back();
}, lv.EVENT_CLICKED, null);

eos.console.log("[pmdemo] main.js executed successfully");
