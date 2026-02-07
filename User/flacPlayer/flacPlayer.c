#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "./flacPlayer/flacPlayer.h"
#include "ff.h"
#include "./es8388/es8388.h"
#include "./sai/bsp_sai.h"
#include "./led/bsp_led.h"

/* 引入 FLAC 库头文件 */
#include "FLAC/stream_decoder.h"

/* ==================================================================
 * 宏定义与内存分配
 * ================================================================== */
/* 定义输出缓冲区大小 (单声道样本数，立体声需*2) */
/* 4096 * 2 * 2 Bytes = 16KB per buffer */
#define FLAC_BUFFER_SIZE 4096

/* 双缓冲区 (DMA读取区) - 放在 D2 SRAM */
#if defined(__CC_ARM) || defined(__GNUC__)
__attribute__((section(".RAM_D2"))) __attribute__((aligned(32)))
#endif
static int16_t flac_outbuffer[2][FLAC_BUFFER_SIZE * 2]; // *2 是因为立体声(L+R)

/* 文件读取临时缓存 - 放在 D2 SRAM */
#if defined(__CC_ARM) || defined(__GNUC__)
__attribute__((section(".RAM_D2"))) __attribute__((aligned(32)))
#endif
static uint8_t flac_file_read_buffer[8192];

/* ==================================================================
 * 全局变量
 * ================================================================== */
static FLAC_PLAYER_TYPE flacplayer;
static FIL flac_file;
static FLAC__StreamDecoder *flac_decoder = 0;

/* 播放控制标志 */
static volatile uint8_t flac_transfer_end = 0;  /* DMA 传输完成标志 */
static volatile uint8_t flac_witch_buf = 0;     /* 当前DMA正在使用的Buf索引(0或1), CPU应填充另一个 */
static uint8_t is_dma_started = 0;              /* DMA是否已启动 */
static uint32_t buffer_offset = 0;              /* 当前缓冲区填充偏移量 */
static uint8_t current_cpu_buf_idx = 0;         /* CPU当前正在填充的缓冲区索引 */
static volatile uint32_t led_blink_counter = 0; /* LED闪烁计数器 */

/* 引用 bsp_sai.c 中的回调函数指针 */
extern void (*sai1_tx_callback)(void);
extern SAI_HandleTypeDef h_sai;
/* ==================================================================
 * DMA 回调函数
 * ==================================================================
 */
/**
 * @brief  SAI DMA传输完成回调
 */
void flac_sai_dma_tx_callback(void)
{
    /* 使用标准宏 DMA_SxCR_CT (即 bit 19) */
    if (SAI1_TX_DMASx->CR & DMA_SxCR_CT)
    {
        /* CT=1，说明 DMA 正在处理 Buffer 1 */
        /* 所以 CPU 应该去填充 Buffer 0 */
        flac_witch_buf = 0;
    }
    else
    {
        /* CT=0，说明 DMA 正在处理 Buffer 0 */
        /* 所以 CPU 应该去填充 Buffer 1 */
        flac_witch_buf = 1;
    }

    /* 标记传输完成，通知主循环可以开始填充数据了 */
    flac_transfer_end = 1;

    /* LED闪烁：每4次DMA传输完成翻转一次LED */
    led_blink_counter++;
    if (led_blink_counter >= 4)
    {
        LED1_TOGGLE; /* 翻转LED */
        led_blink_counter = 0;
    }
}

/* ==================================================================
 * FLAC 解码库回调函数
 * ================================================================== */

/**
 * @brief  FLAC 读文件回调
 */
static FLAC__StreamDecoderReadStatus flac_read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    UINT bytes_read = 0;
    FRESULT res;
    size_t req_size = *bytes;

    if (req_size > sizeof(flac_file_read_buffer))
        req_size = sizeof(flac_file_read_buffer);

    if (req_size > 0)
    {
        res = f_read(&flac_file, flac_file_read_buffer, req_size, &bytes_read);
        if (res != FR_OK)
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;

        if (bytes_read > 0)
        {
            memcpy(buffer, flac_file_read_buffer, bytes_read);
            *bytes = bytes_read;
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        }
        else
        {
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
    }
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

/**
 * @brief  FLAC 写音频数据回调 (核心播放逻辑)
 */
static FLAC__StreamDecoderWriteStatus flac_write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
    uint32_t i;
    int32_t left, right;

    if (flacplayer.ucStatus != FLAC_STA_PLAYING)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

    uint32_t blocksize = frame->header.blocksize;

    for (i = 0; i < blocksize; i++)
    {
        /* 1. 声道处理 */
        if (frame->header.channels == 2)
        {
            left = buffer[0][i];
            right = buffer[1][i];
        }
        else
        {
            left = buffer[0][i];
            right = buffer[0][i];
        }

        /* 2. 位深适配 (24->16) */
        if (frame->header.bits_per_sample > 16)
        {
            left >>= (frame->header.bits_per_sample - 16);
            right >>= (frame->header.bits_per_sample - 16);
        }

        /* 3. 填充当前缓冲区 */
        flac_outbuffer[current_cpu_buf_idx][buffer_offset++] = (int16_t)left;
        flac_outbuffer[current_cpu_buf_idx][buffer_offset++] = (int16_t)right;

        /* 4. 缓冲区满处理 */
        if (buffer_offset >= (FLAC_BUFFER_SIZE * 2))
        {
            /* 刷新 Cache */
            SCB_CleanDCache_by_Addr((uint32_t *)&flac_outbuffer[current_cpu_buf_idx], FLAC_BUFFER_SIZE * 2 * 2);
            buffer_offset = 0;

            /* 第一次启动硬件 */
            if (is_dma_started == 0)
            {
                if (current_cpu_buf_idx == 0)
                {
                    current_cpu_buf_idx = 1; /* 填满 Buffer 0，切换去填 Buffer 1 */
                }
                else
                {
                    // printf("Buffers Prefilled. Starting DMA...\r\n");

                    /* 1. 初始化配置  */
                    es8388_sai_cfg(0, 3);    // I2S 16bit
                    es8388_adda_cfg(1, 0);   // 使能DAC，关闭ADC
                    es8388_output_cfg(1, 1); // 使能输出通道1和2

                    /* 2. 配置 DMA (FIFO 已在函数内开启) */
                    sai1_tx_dma_init((uint8_t *)&flac_outbuffer[0],
                                     (uint8_t *)&flac_outbuffer[1],
                                     FLAC_BUFFER_SIZE * 2,
                                     1);

                    /* 3. 设置采样率 */
                    sai1_samplerate_set(flacplayer.ucFreq);

                    /* 4. 一键启动 (内部会自动处理 DMAEN 和 SAIEN 的顺序) */
                    sai1_play_start();
                    /* ========================================================= */

                    /* [DEBUG] 再次检查 CR1，这次 Bit 16 和 17 应该都是 1 */
                    // printf(">>> SAI1 CR1: 0x%08lX (Expect Bit16=1, Bit17=1)\r\n", SAI1_Block_A->CR1);

                    is_dma_started = 1;

                    /* 等待第一次中断 */
                    while (flac_transfer_end == 0)
                    {
                        if (flacplayer.ucStatus != FLAC_STA_PLAYING)
                            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                    }
                    flac_transfer_end = 0;
                    current_cpu_buf_idx = flac_witch_buf;
                }
            }
            else
            {
                /* 正常播放中：等待 DMA 释放缓冲区 */
                while (flac_transfer_end == 0)
                {
                    if (flacplayer.ucStatus != FLAC_STA_PLAYING)
                        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
                flac_transfer_end = 0;
                current_cpu_buf_idx = flac_witch_buf;
            }
        }
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flac_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
    {
        flacplayer.ucFreq = metadata->data.stream_info.sample_rate;
        flacplayer.ucChannels = metadata->data.stream_info.channels;
        flacplayer.ucBits = metadata->data.stream_info.bits_per_sample;
        printf("FLAC Info: %dHz, %d Ch, %d Bits\r\n", flacplayer.ucFreq, flacplayer.ucChannels, flacplayer.ucBits);
    }
}

static void flac_error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
    printf("FLAC Error: %s\r\n", FLAC__StreamDecoderErrorStatusString[status]);
}

/* ==================================================================
 * 播放文件主逻辑
 * ================================================================== */

static void Flac_PlayFile(const char *flacfile)
{
    FRESULT res;

    printf("Playing: %s\r\n", flacfile);

    /* 1. 状态复位 */
    flacplayer.ucStatus = FLAC_STA_IDLE;
    current_cpu_buf_idx = 0;
    buffer_offset = 0;
    is_dma_started = 0;
    flac_transfer_end = 0;

    /* 2. 打开文件 */
    res = f_open(&flac_file, flacfile, FA_READ);
    if (res != FR_OK)
    {
        printf("File Open Fail: %d\r\n", res);
        return;
    }

    /* 3. 创建并初始化解码器 */
    flac_decoder = FLAC__stream_decoder_new();
    if (flac_decoder == NULL)
    {
        f_close(&flac_file);
        return;
    }

    FLAC__stream_decoder_set_md5_checking(flac_decoder, false);
    FLAC__stream_decoder_set_metadata_respond(flac_decoder, FLAC__METADATA_TYPE_STREAMINFO);

    if (FLAC__stream_decoder_init_stream(flac_decoder, flac_read_callback, NULL, NULL, NULL, NULL,
                                         flac_write_callback, flac_metadata_callback, flac_error_callback, NULL) != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        printf("Decoder Init Failed\r\n");
        FLAC__stream_decoder_delete(flac_decoder);
        f_close(&flac_file);
        return;
    }

    /* 4. 解析元数据 (获取采样率和位数) */
    FLAC__stream_decoder_process_until_end_of_metadata(flac_decoder);

    /* 5. 硬件参数配置 (参考 wavplay.c) */
    /* 停止 DMA，准备重新配置 */
    sai1_play_stop();

    /* 配置 ES8388 SAI 接口: 0=I2S */
    /* 如果是 16位: len=3; 如果是 24位: len=0 */
    /* 注意：由于输出 buffer 是 int16，即使源文件是 24bit，也转成 16bit 输出给 SAI */
    es8388_sai_cfg(0, 3);    // 设置为 I2S, 16-bit 数据长度
    es8388_adda_cfg(1, 0);   // 使能DAC，关闭ADC
    es8388_output_cfg(1, 1); // 使能输出通道1和2（耳机和喇叭）

    /* 配置 STM32 SAI */
    /* Mode=MasterTX, CPOL=Rising , DataLen=16bit */
    sai1_saia_init(SAI_MODEMASTER_TX, SAI_CLOCKSTROBING_RISINGEDGE, SAI_DATASIZE_16);

    /* 配置 SAI DMA */
    /* buf0, buf1, 单个缓冲区长度(样本数), 位宽(1=16bit) */
    sai1_tx_dma_init((uint8_t *)flac_outbuffer[0], (uint8_t *)flac_outbuffer[1], FLAC_BUFFER_SIZE * 2, 1);

    /* 设置采样率 (PLL) */
    sai1_samplerate_set(flacplayer.ucFreq);

    /* 挂载回调函数 */
    sai1_tx_callback = flac_sai_dma_tx_callback;

    /* 6. 开始解码循环 */
    /* 注意：process 函数会不断调用 write_callback，在 write_callback 中启动 DMA 并维持双缓冲 */
    flacplayer.ucStatus = FLAC_STA_PLAYING;

    printf("Start Decoding Loop...\r\n");

    FLAC__stream_decoder_process_until_end_of_stream(flac_decoder);

    printf("Decode Finished.\r\n");

    /* 7. 结束清理 */
    sai1_play_stop();
    flacplayer.ucStatus = FLAC_STA_IDLE;

    FLAC__stream_decoder_delete(flac_decoder);
    f_close(&flac_file);

    /* 清除回调，防止野指针 */
    sai1_tx_callback = NULL;
}

/* ==================================================================
 * 辅助函数
 * ================================================================== */
static uint8_t is_flac_extension(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return 0;
    char ext[6] = {0};
    strncpy(ext, dot, 5);
    for (int i = 0; i < 5; i++)
        if (ext[i])
            ext[i] = tolower(ext[i]);
    if (strcmp(ext, ".flac") == 0 || strcmp(ext, ".fla") == 0)
        return 1;
    return 0;
}

/* ==================================================================
 * Demo 入口
 * ================================================================== */
void flacPlayerDemo(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char path[256];

    /* 默认音量设置已经在 main.c 中 es8388_hpvol_set 完成，这里只更新结构体 */
    flacplayer.ucVolume = 25;

    printf("Start FLAC Loop in /MUSIC...\r\n");

    while (1)
    {
        res = f_opendir(&dir, "MUSIC");
        if (res != FR_OK)
        {
            printf("Open /MUSIC fail: %d\r\n", res);
            HAL_Delay(1000);
            continue;
        }

        while (1)
        {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0)
                break;

            if (fno.fattrib & AM_DIR)
                continue;

            if (is_flac_extension(fno.fname))
            {
                sprintf(path, "MUSIC/%s", fno.fname);
                Flac_PlayFile(path);
                HAL_Delay(500);
            }
        }
        f_closedir(&dir);
    }
}
