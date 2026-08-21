> **⚠️ 非官方 Fork 声明 / Unofficial Fork Notice**
>
> 本仓库是 [ElenixOS](https://github.com/ElenixOS/ElenixOS) 的**个人非官方 fork**，与官方项目及其维护者**无任何关联**。
> 原始项目以 **Apache License 2.0** 发布；本 fork 沿用该许可，并保留全部原始版权与许可声明（见文末 `LICENSE` 与第三方署名章节，未做任何删减）。
>
> **相对上游的主要改动 / Key changes vs. upstream：**
> - 新增「入侵协议 / Breach Protocol」应用（`apps/breach/`），移植自 [yet3/cyberpunk2077-breach-protocol](https://github.com/yet3/cyberpunk2077-breach-protocol)（MIT License，署名见 `apps/breach/LICENSE`）。
> - 适配并统一了部分 app 的启动器图标（`icon/`）。
>
> ---

<div style="display: flex; align-items: center;">
  <img src="./assets/logo-light.png#gh-light-mode-only" />
  <img src="./assets/logo-dark.png#gh-dark-mode-only" />
</div>

[![STATUS](https://img.shields.io/badge/Status-DEVELOPING-red)](#)
[![License](https://img.shields.io/badge/Licence-Apache%202.0-brightgreen.svg)](LICENSE)
[![GUI](https://img.shields.io/badge/GUI-LVGL-blue)](https://lvgl.io)
[![Script Engine](https://img.shields.io/badge/Script_Engine-JerryScript-orange)](https://jerryscript.net)
![release](https://img.shields.io/github/v/tag/ElenixOS/ElenixOS)
[![Simulator](https://img.shields.io/badge/Simulator-Try_it_Online-success)](https://simulator.elenixos.com/wasm/latest/main.html)


<div>
  <a href="./README.md">English</a> | <a href="./README.zh-CN.md">简体中文</a>
</div>

---

## Overview

ElenixOS is an open-source smartwatch operating system that builds its graphical interface based on LVGL, with watch faces and applications running on a unified script engine powered by JerryScript.

The system adopts a script-based application runtime model, where UI structure and interaction logic are described in JavaScript, while underlying native code handles graphics rendering, event scheduling, and hardware resource management. This enables a flexible and efficient application development experience on resource-constrained embedded platforms.

ElenixOS emphasizes portability and extensibility in its design, providing clear abstraction layers and unified interfaces for easy porting to different MCUs and hardware platforms. Meanwhile, watch faces and applications share the same runtime environment and API system, reducing system complexity and improving overall consistency.

In terms of user experience, ElenixOS's overall UI and interaction style references Apple Watch, focusing on animation smoothness, gesture operations, and interface hierarchy. Combined with LVGL's animation system and event mechanism, it aims to provide a visual and interactive experience close to native smartwatches on embedded devices.

ElenixOS is suitable for both personal projects and learning research, as well as serving as a customizable smart wearable system foundation framework, providing developers with a complete solution from the bottom layer to the application layer.

## Online Simulator

ElenixOS provides an online simulator that allows you to experience the system directly in your web browser without any setup. You can access the latest version of the simulator here:
[Online Simulator](https://simulator.elenixos.com/wasm/latest/main.html)

## Quick Start

Please refer to the [Quick Start](https://elenixos.com/en/docs/getting_started/quick_start).

## Reference Documentation

Please refer to the [ElenixOS Documentation](https://elenixos.com/en).

## Community

- [GitHub Discussions](https://github.com/ElenixOS/ElenixOS/discussions)
- [Tencent Channel](https://pd.qq.com/s/2arlf3js7)

## Open Source License

This software is released under the Apache License 2.0 open-source license. This license allows individuals and organizations to freely use, modify, and distribute this software and its derivatives for commercial purposes, but requires retaining the original copyright notice, license text, and related statements. The software is provided "as is" without any form of warranty.

## Third-Party Software

ElenixOS used the following open-source projects during development, and we would like to express our gratitude to the relevant authors and communities:

- LVGL: https://lvgl.io
- JerryScript: https://jerryscript.net
- RemixIcon: https://remixicon.com
- cJSON: https://github.com/DaveGamble/cJSON
