# 🚀 STM32H750-XBH6 FLAC Decoder: Agent-Synergy Edition 🎧

[![MCU](https://img.shields.io/badge/MCU-STM32H750XBH6-orange?style=for-the-badge&logo=stmicroelectronics)](https://www.stmcu.org.cn/)
[![Board](https://img.shields.io/badge/Board-反客_H750_金手指款-red?style=for-the-badge)](http://www.fanke.com/)
[![Audio](https://img.shields.io/badge/Codec-libFLAC_1.4.x-green?style=for-the-badge)](https://xiph.org/flac/)
[![Status](https://img.shields.io/badge/Status-Agent--Verified-blueviolet?style=for-the-badge)](https://github.com/)

> **这是一份由 AI 智能体集群（Agent Swarm）深度介入、从 HardFault 堆栈溢出边缘强行“救活”的高保真音频解码项目。**
> 本项目不仅实现了反客 STM32H750XBH6 对 FLAC 格式的流畅解码，更沉淀了一套针对嵌入式底层内存管理与汇编启动优化的 **多 Agent 协同工作流**。

---

## 🤖 核心黑科技：多 Agent 协作 Debug 实验室

本项目最具价值的部分并非代码本身，而是解决复杂底层冲突时的 **长链推理逻辑**。我们通过三名虚拟“Agent”的跨层级协作，攻克了嵌入式移植中 99% 的开发者都会跪下的“内存踩踏”难题。

### 🎭 Agent 角色分工与战果

| Agent 角色 | 核心职责 | 攻克的“幽灵” Bug |
| :--- | :--- | :--- |
| **🔍 异常诊断 Agent** | 串口流解析、内存快照分析 | 定位 `MEMORY_ALLOCATION_ERROR` 根源在于 libFLAC 尝试分配巨大专辑封面（Picture Block）对 RAM 的瞬时冲击。 |
| **🏗️ C/C++ 架构师 Agent** | 逻辑重构、重定向接管 | 重写自定义 `realloc` 算法（修复 0 字节申请返回 NULL 的致命误判），并注入 FatFS `f_lseek` 回调实现数据的安全跳过。 |
| **⚡ 硬件资源 Agent** | 寄存器级校验、汇编优化 | 针对 H750 芯片的 **DTCM** 物理边界，精准计算全局变量与栈空间的平衡，动态重写 `.s` 启动文件的堆栈汇编配置。 |

---

## 🔥 攻坚全记录：Agent 逻辑流可视化

### 1️⃣ 战役一：内存炸弹的“降维打击”
* **现象：** 解码器加载 StreamInfo 后直接死机，串口无报错。
* **Agent 推理：** 高码率 FLAC 往往携带数百 KB 的封面图，STM32 内部 SRAM 无法直存，硬读必崩。
* **协同方案：** **架构师 Agent** 生成 Seek 回调，**硬件 Agent** 校验扇区对齐，实现毫秒级“逻辑跳过”。

### 2️⃣ 战役二：破解 `realloc(0)` 的黑色幽默
* **现象：** AXI-SRAM 空间充足，但库始终报分配失败。
* **Agent 推理：** libFLAC 调用 `realloc(ptr, 0)` 试图释放内存，由于原底层分配器返回 `NULL` 被库误认为分配失败。
* **协同方案：** **架构师 Agent** 修正 `malloc.c`，强制对 0 字节请求分配 1 字节，完美“欺骗” libFLAC 内部状态机。

### 3️⃣ 战役三：DTCM 的“寸土寸金”
* **现象：** 栈设为 32KB 编译报错空间不足，设为 4KB 运行直接跑飞。
* **Agent 推理：** H750 的 **64KB DTCM** 必须平衡栈空间与全局变量 `.bss` 段。
* **协同方案：** **硬件 Agent** 重新规划内存图谱，大幅砍掉冗余原生 Heap，将 Stack 锁定在 **16KB**，达成稳定性与编译成功率的极致平衡。

---

## 🛠️ 技术规格 (Hardcore Specs)

* **核心主控:** **STM32H750XBH6** (Cortex-M7, 480MHz 高速频率)
* **硬件平台:** **反客 (FanKe) 金手指核心板**
* **解码引擎:** `libFLAC` 1.4.3 (AI-Optimized Version)
* **存储接口:** FatFS + 高速 SDMMC (支持 SD/SDNAND)
* **内存池规划:** * **DTCM (64KB):** 专供 Stack（16KB）与关键全局变量。
  * **AXI-SRAM (512KB):** 通过 `mymalloc` 接管音频双缓冲区（SAI-DMA）。
* **启动配置:**
  * `Stack_Size: 0x4000 (16KB)` —— 针对 FLAC 递归深度优化
  * `Heap_Size: 0x200 (512B)` —— 极简模式，内存全靠 AXI-SRAM 池管理

---

## 🚀 快速启动

### 编译要求
* **IDE:** Keil MDK v5.3x 及以上
* **编译器:** **ARMCC V6 (LLVM)** —— 提供更高的指令执行效率
* **硬件:** 反客 STM32H750 金手指开发板

### 操作指南
1. **Rebuild All**: 修改启动文件 `.s` 后必须点击全编译，确保 16KB 栈分配生效。
2. **Flash**: 使用 DAP-LINK 或 J-Link 下载。
3. **Monitor**: 开启串口调试（115200bps），见证 Agent 介入后的丝滑启动：
   ```text
   >>> FLAC Init on STM32H750...
   FLAC Signature OK.
   FLAC: Creating decoder (SRAMIN Verified)...
   FLAC: Seek Support Injected.
   >>> FLAC StreamInfo Parsed: 44100Hz, 16bit, 2ch.
   FLAC: Playback Started.
