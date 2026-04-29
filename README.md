# 🚀 STM32H7R7 FLAC Decoder: Agent-Synergy Edition 🎧

[![MCU](https://img.shields.io/badge/MCU-STM32H7R7-blue?style=for-the-badge&logo=stmicroelectronics)](https://www.st.com/)
[![Audio](https://img.shields.io/badge/Codec-libFLAC-green?style=for-the-badge)](https://xiph.org/flac/)
[![Status](https://img.shields.io/badge/Status-AI--Agent%20Verified-blueviolet?style=for-the-badge)](https://github.com/)
[![Debug](https://img.shields.io/badge/Debug-HardFault--Proof-brightgreen?style=for-the-badge)](https://github.com/)

> **这是一份由 AI 智能体集群（Agent Swarm）深度介入、从 HardFault 死堆栈中强行“救活”的高保真音频解码项目。** > 本项目不仅实现了 STM32H7R7 对 FLAC 格式的流畅解码，更沉淀了一套针对嵌入式底层 C/汇编级问题的 **多 Agent 协同 Debug 工作流**。

---

## 🤖 核心黑科技：多 Agent 协作 Debug 实验室

本项目最具价值的部分并非代码本身，而是解决复杂底层冲突时的 **长链推理逻辑**。我们通过三名虚拟“Agent”的跨层级协作，攻克了嵌入式移植中 99% 的开发者都会跪下的“静默死机”难题。

### 🎭 Agent 角色分工与战果

| Agent 角色 | 核心职责 | 攻克的“幽灵” Bug |
| :--- | :--- | :--- |
| **🔍 异常诊断 Agent** | 串口流解析、内存快照分析 | 定位 `MEMORY_ALLOCATION_ERROR` 根源在于 FLAC 巨大专辑封面对 RAM 的瞬时冲击。 |
| **🏗️ C/C++ 架构师 Agent** | 逻辑重构、回调注入 | 重写自定义 `realloc` 逻辑（解决 0 字节申请返回 NULL 的乌龙），并注入 FatFS `Seek` 跳过冗余元数据。 |
| **⚡ 硬件资源 Agent** | 寄存器级校验、汇编优化 | 在 64KB DTCM 的“寸土寸金”之地，极限平衡 Stack 与 Heap 大小，动态修改 `.s` 启动文件。 |

---

## 🔥 攻坚全记录：逻辑流可视化

### 1️⃣ 第一波：跳过“内存炸弹”
* **现象：** 解码器加载 StreamInfo 后直接卡死。
* **Agent 推理：** 封面图（Picture Block）往往高达数百 KB，单片机 SRAM 无法直存。
* **协作方案：** * **架构师 Agent** 生成 `f_lseek` 回调。
  * **硬件 Agent** 校验文件系统扇区对齐，实现毫秒级“无感跳过”。

### 2️⃣ 第二波：破解 `realloc(0)` 迷局
* **现象：** 内存明明够用，库却报错内存不足。
* **Agent 推理：** 库函数调用 `realloc(ptr, 0)` 试图收缩内存，原分配器返回 `NULL` 被误判为分配失败。
* **协作方案：** * **架构师 Agent** 修改 `malloc.c`，强制对 0 字节请求分配 1 字节，完美“欺骗” libFLAC 状态机。

### 3️⃣ 第三波：平衡 DTCM 的天平
* **现象：** 栈改大编译就报错，栈改小运行就跑飞。
* **Agent 推理：** 全局变量占用了 DTCM 太多空间，留给 Stack 的余量不足以支撑 libFLAC 的深度递归。
* **协作方案：** * **硬件 Agent** 重新规划 `.bss` 段，大幅砍掉冗余 Heap，将 Stack 精准锁定在 `16KB`，实现稳定性与编译性的极致平衡。

---

## 🛠️ 技术规格 (Hardcore Specs)

* **核心芯片:** STM32H7R7L8H6H (Cortex-M7, 280MHz)
* **解码引擎:** `libFLAC` 1.4.x (AI-Optimized)
* **存储方案:** FatFS + SD/SDNAND
* **内存池:** AXI-SRAM (MEM1) 全局重定向接管标准 `malloc`
* **堆栈策略:** * `Stack_Size: 0x4000 (16KB)` —— 针对深度递归优化
  * `Heap_Size: 0x200 (512B)` —— 极简兜底模式

---

## 🚀 快速启动

### 环境要求
* **IDE:** Keil MDK v5.x / VS Code (EIDE)
* **Compiler:** ARMCC V6.x (LLVM)
* **Hardware:** 正点原子 H7R7 开发板 (或同架构板卡)

### 编译与烧录
1. **Rebuild All**: 必须全编译以确保 `.s` 文件中堆栈设置生效。
2. **Flash**: 通过 DAP/JLINK 下载。
3. **Monitor**: 观察串口输出，你将看到 Agent 介入后的完美初始化日志：
   ```text
   FLAC Signature OK.
   FLAC: Creating decoder...
   FLAC: Init stream with Seek support...
   >>> FLAC StreamInfo Parsed (AI Verified)
   FLAC: Playback Started.
