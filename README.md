# STM32H750-Decode-FLAC
基于STM32H750的FLAC音频解码播放项目

## 一、项目简介
本项目以反客金手指款STM32H750开发板为硬件核心，搭配ES8388音频解码芯片，通过移植开源FLAC解码库，实现了对FLAC无损音频文件的解码与播放功能。

项目以野火电子的MP3播放例程为参考模板，对底层硬件驱动进行了适配与扩展，修改了SAI接口驱动，并新增了ES8388解码芯片的完整驱动代码。音频文件通过SD卡读取，无需依赖外部Flash存储音频资源。

## 二、硬件环境
- **主控芯片**：STM32H750XBH6（反客金手指开发板）
- **音频解码芯片**：ES8388（I2S接口）
- **外部存储**：SD卡（用于存放FLAC音频文件）
- **外设接口**：SAI音频接口、I2C控制接口、SDIO/SPIFlash接口（SD卡读写）

## 三、软件环境
- **开发工具**：Keil MDK-ARM V5.x及以上版本
- **依赖库**：
  - STM32H7 HAL库（官方底层驱动）
  - 开源FLAC解码库（已移植至工程）
  - 野火电子STM32H7开发板BSP驱动库
- **参考例程**：野火电子MP3播放例程（作为工程模板）

## 四、功能说明
✅ **核心功能**：
- 实现FLAC格式音频文件的实时解码
- 通过ES8388芯片输出高质量音频
- 支持从SD卡指定目录读取音频文件

✅ **驱动适配**：
- 重构BSP层SAI驱动，适配ES8388的I2S接口时序
- 新增ES8388初始化、音量控制、音频格式配置等驱动接口
- 移植开源flac解码库

## 五、项目结构
- Libraries/ # 依赖hal库文件
- Output/ # 编译输出文件（.axf、.hex 等）
- Project/ # Keil 工程文件
- User/ # 业务代码（主函数、FLAC 解码逻辑、、FLAC 库、BSP 驱动）
- README.md # 项目说明文档
- keilkill.bat # Keil 工程清理脚本

## 六、注意事项
⚠️ **存储说明**：
- STM32H750XBH6仅支持从SD卡读取音频文件，不支持外部Flash读取音频资源。
- SD卡需严格按照要求创建`MUSIC`文件夹，否则程序无法识别音频文件。

⚠️ **驱动适配**：
- 若更换开发板或音频芯片，需重新适配SAI驱动与ES8388控制逻辑。
- SD卡的读写速度会影响音频播放流畅度，建议选用Class10及以上规格的SD卡。

⚠️ **文件格式**：
- 仅支持标准FLAC无损音频格式，不支持非标准压缩的FLAC变种文件。
- 建议将FLAC文件采样率控制在44.1kHz/48kHz，避免因采样率过高导致解码卡顿。

⚠️ **烧录说明**：
- 由于比较懒，所以程序仅在外部flash中运行
- 需要反客例程的boot与下载算法
- 分散加载按需自用

⚠️ **移植说明**：
- keil里面需要添加c语言宏定义：USE_HAL_DRIVER,STM32H750xx,EXT_Flash_SPI,FLAC__NO_ASM, CPU_IS_LITTLE_ENDIAN,FLAC__HAS_OGG=0, FLAC__NO_STDIO,NDEBUG
- EXT_Flash_SPI为外部flash所用，这里是因为之前测试时将反客的system_stm32h7xx.c替换野火工程。理论上野火的工程可以不用替换，它本身就是下载到外部flash工程
- 开源flac库移植仅保留src与include两个文件夹，由于库太繁琐，不一一赘述裁剪步骤，裁剪后的为本项目中User/flac中
- keil工程添加flac解码库，仅需要添加以下文件即可：
- bitmath.c
  bitreader.c
  cpu.c
  crc.c
  fixed.c
  float.c
  format.c
  lpc.c
  md5.c
  memory.c
  stream_decoder.c
  window.c

## 七、致谢与参考
- 野火电子STM32H7开发板例程（提供了基础工程框架）
- 开源FLAC音频解码库（[https://xiph.org/flac/](https://github.com/xiph/flac)）
- 正点原子es8388驱动
