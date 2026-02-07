#include <stdio.h>
#include <string.h>
#include <ctype.h>     /* 用于转换大小写 */
#include "./flacPlayer/flacPlayer.h"
#include "ff.h"
#include "./es8388/es8388.h"
#include "./sai/bsp_sai.h"
#include "./led/bsp_led.h"

/* 引入 FLAC 库头文件 */
#include "FLAC/stream_decoder.h"

/* 引入外部 DMA 回调指针 */
extern void (*SAI_DMA_TX_Callback)(void);

/* 定义输出缓冲区大小 (立体声样本数) */
#define FLAC_BUFFER_SIZE  4096 

/* 双缓冲区，用于 DMA 传输 */
#if defined ( __CC_ARM ) || defined ( __GNUC__ )
__attribute__((section(".RAM_D2"))) __attribute__((aligned(32))) 
#endif
static int16_t flac_outbuffer[2][FLAC_BUFFER_SIZE * 2]; // *2 是因为立体声(L+R)

/* 【新增】中间读取缓存，放在 D2 SRAM (0x30000000)，避开 DTCM 和 Cache 问题 */
#if defined ( __CC_ARM ) || defined ( __GNUC__ )
__attribute__((section(".RAM_D2"))) __attribute__((aligned(32)))
#endif
static uint8_t flac_file_read_buffer[8192]; // 8KB缓存

/* 全局变量 */
static FLAC_PLAYER_TYPE flacplayer;
static FIL flac_file;
static uint8_t bufflag = 0;         
static volatile uint8_t dma_needed = 0; 
static uint32_t buffer_offset = 0;  
static uint8_t is_dma_running = 0;  

static FLAC__StreamDecoder *flac_decoder = 0;
/* 【新增 1】引入 bsp_sai.c 中定义的 DMA 句柄 */
extern DMA_HandleTypeDef h_txdma;

/* 【新增 2】定义 DMA 错误回调函数 */
void DMA_Error_Callback(DMA_HandleTypeDef *hdma)
{
    printf("!!! DMA ERROR: Code 0x%08X !!!\r\n", hdma->ErrorCode);
    /* 0x01: TE (Transfer Error) - 通常是地址不对或访问被拒绝 */
    /* 0x02: FE (FIFO Error) */
    /* 0x04: DME (Direct Mode Error) */
}


/* ================= 新增：诊断 SAI 寄存器 ================= */
void Dump_SAI_Registers(void)
{
    printf("\r\n=== SAI STATE ===\r\n");
    /* CR1: 检查 MODE(位0-1) 是否为 0 (Master TX) */
    printf("CR1: 0x%08X (Mode=%d, Enable=%d)\r\n", SAI1_Block_A->CR1, SAI1_Block_A->CR1 & 0x03, (SAI1_Block_A->CR1 >> 16) & 0x01);
    /* SR: 检查 FLVL(位16-18) FIFO 水位 */
    printf("SR : 0x%08X (FIFO Level=%d/8)\r\n", SAI1_Block_A->SR, (SAI1_Block_A->SR >> 16) & 0x07);
    printf("=================\r\n");
}

/* 引用回调函数 */
extern void FlacPlayer_SAI_DMA_TX_Callback(void);

/* DMA 中断的中间层包装 */
void DMA_M0_Callback(DMA_HandleTypeDef *hdma) { FlacPlayer_SAI_DMA_TX_Callback(); }
void DMA_M1_Callback(DMA_HandleTypeDef *hdma) { FlacPlayer_SAI_DMA_TX_Callback(); }


/* ------------------------------------------------------------------
 * 辅助函数：判断文件后缀是否为 .flac (忽略大小写)
 * ------------------------------------------------------------------ */
static uint8_t is_flac_extension(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    
    char ext[6] = {0}; // 加大一点，并在定义时清零
    
    /* 复制后缀，限制长度 */
    strncpy(ext, dot, 5); 
    /* 确保末尾有结束符 (防御性编程) */
    ext[5] = '\0'; 
    
    /* 全部转为小写 */
    for(int i=0; i<5; i++) 
    {
        if(ext[i]) ext[i] = tolower(ext[i]);
    }
    
    /* 情况1: 完整后缀 .flac */
    if (strcmp(ext, ".flac") == 0) return 1;
    
    /* 情况2: 短文件名后缀 .fla (针对 HEAVEN~1.FLA 这种情况) */
    if (strcmp(ext, ".fla") == 0) return 1;
    
    return 0;
}
/* ------------------------------------------------------------------
 * FLAC 库回调函数实现 (保持不变)
 * ------------------------------------------------------------------ */
static FLAC__StreamDecoderReadStatus flac_read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    UINT bytes_read = 0;
    FRESULT res;
    size_t req_size = *bytes;

    /* 保护：不要超过中间缓存大小 */
    if (req_size > sizeof(flac_file_read_buffer))
    {
        req_size = sizeof(flac_file_read_buffer);
    }

    if (req_size > 0)
    {
        /* 1. 先读到我们在 RAM_D2 定义的中间缓存里 (这里 DMA 是安全的) */
        res = f_read(&flac_file, flac_file_read_buffer, req_size, &bytes_read);
        
        if (res != FR_OK)
        {
            printf("FatFS Read Error: %d\r\n", res);
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
        }
        
        if (bytes_read > 0)
        {
            /* 2. 再用 CPU 搬运到 FLAC 库的 buffer (库的 buffer 是 malloc 的，可能在任何地方) */
            memcpy(buffer, flac_file_read_buffer, bytes_read);
            
            /* 告诉库我们实际读了多少 */
            *bytes = bytes_read;
            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        }
        else
        {
            /* 读到 0 字节，说明文件结束 */
            *bytes = 0;
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
    }
    
    return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

static FLAC__StreamDecoderWriteStatus flac_write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
    uint32_t i;
    int32_t left, right;
    
    /* 检查状态，支持外部打断 */
    if (flacplayer.ucStatus != FLAC_STA_PLAYING)
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

    uint32_t blocksize = frame->header.blocksize;
    
    for (i = 0; i < blocksize; i++)
    {
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

        if (frame->header.bits_per_sample > 16)
        {
            left >>= (frame->header.bits_per_sample - 16);
            right >>= (frame->header.bits_per_sample - 16);
        }

        flac_outbuffer[bufflag][buffer_offset++] = (int16_t)left;
        flac_outbuffer[bufflag][buffer_offset++] = (int16_t)right;

        if (buffer_offset >= (FLAC_BUFFER_SIZE * 2))
        {
			/* 【新增】缓冲区填满了，让 CPU 把数据刷到物理 RAM，给 DMA 读 */
            if(bufflag == 0)
                SCB_CleanDCache_by_Addr((uint32_t*)&flac_outbuffer[0], FLAC_BUFFER_SIZE * 2 * 2); // *2(16bit)
            else
                SCB_CleanDCache_by_Addr((uint32_t*)&flac_outbuffer[1], FLAC_BUFFER_SIZE * 2 * 2);
			
            if (is_dma_running == 0)
            {
                if (bufflag == 0)
                {
                    bufflag = 1;
                    buffer_offset = 0;
                }
                else
                {
					/* 缓冲区 1 也满了，现在启动 DMA */
                    printf("Step 1: Clock Config...\r\n");
                    
                    /* 1. 复位 SAI (防止状态残留) */
                    __HAL_RCC_SAI1_FORCE_RESET();
                    HAL_Delay(5);
                    __HAL_RCC_SAI1_RELEASE_RESET();
                    
                    /* 2. 配置时钟 (调用 bsp_sai.c 的修复版) */
                    BSP_AUDIO_OUT_ClockConfig(flacplayer.ucFreq);

                    printf("Step 2: SAI & DMA Config...\r\n");
                    
                    /* 3. 配置 SAI (调用 bsp_sai.c 的修复版) */
                    /* 参数：标准(忽略), 位深(16), 频率 */
                    SAIxA_Tx_Config(SAI_I2S_STANDARD, SAI_DATASIZE_16, flacplayer.ucFreq);
                    
                    /* 4. 清理 Cache */
                    SCB_CleanDCache_by_Addr((uint32_t*)&flac_outbuffer[0], sizeof(flac_outbuffer));
                    
                    /* 5. 初始化 DMA 并启动 (调用 bsp_sai.c 的修复版) */
//                    SAIA_TX_DMA_Init((uint32_t)&flac_outbuffer[0], 
//                                     (uint32_t)&flac_outbuffer[1], 
//                                     FLAC_BUFFER_SIZE * 2);
                    
                    /* 6. 初始化 ES8388  */
                    es8388_init();

                    is_dma_running = 1;
                    
                    printf("Step 3: Play Started!\r\n");
                    
                    /* 之后的等待循环保持不变... */
					
                    dma_needed = 0;
					uint32_t last_ndtr = 0;
                    uint32_t timeout = 0;
                    while (dma_needed == 0)
                    {
						/* 【新增诊断】每隔一段时间打印一次 DMA 剩余数据量 (NDTR) */
                        timeout++;
                        if (timeout % 1000000 == 0) // 降频打印，防止刷屏
                        {
                            /* 读取 DMA1 Stream2 当前还剩多少数据没搬完 */
                            uint32_t cur_ndtr = DMA1_Stream2->NDTR;
                            
                            /* 打印 VTOR (检查中断表地址) 和 NDTR (检查 DMA 进度) */
                            printf("DMA Status: NDTR = %d (Total: %d) | VTOR = 0x%08X\r\n", 
                                   cur_ndtr, FLAC_BUFFER_SIZE * 2, SCB->VTOR);
                            
                            if (cur_ndtr == last_ndtr && cur_ndtr != 0)
                            {
                                printf("WARNING: DMA is STUCK! (Data not moving)\r\n");
                            }
                            last_ndtr = cur_ndtr;
                        }
						
                        if (flacplayer.ucStatus != FLAC_STA_PLAYING) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                    }
					printf("Step 4: DMA IRQ Received!\r\n"); // 如果看到这句，说明通了
                    dma_needed = 0; 
                    buffer_offset = 0; 
                }
            }
            else
            {
                dma_needed = 0;
                while (dma_needed == 0)
                {
                    if (flacplayer.ucStatus != FLAC_STA_PLAYING) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                }
                dma_needed = 0;
                buffer_offset = 0;
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

/* ------------------------------------------------------------------
 * 单曲播放函数 (内部调用)
 * ------------------------------------------------------------------ */
static void Flac_PlayFile(const char *flacfile)
{
    FRESULT res;
    
    printf("Playing: %s\r\n", flacfile);

    /* 1. 变量复位 */
    flacplayer.ucStatus = FLAC_STA_IDLE;
    bufflag = 0;
    buffer_offset = 0;
    is_dma_running = 0;
    dma_needed = 0;

    /* 2. 打开文件 */
    res = f_open(&flac_file, flacfile, FA_READ);
    if (res != FR_OK)
    {
        printf("File Open Fail: %d\r\n", res);
        return;
    }

	/* 3. 硬件初始化 (每次播放前确保硬件状态正确) */
	es8388_init();
//	es8388_adda_cfg(1, 0);   
//	es8388_output_cfg(1, 1); 
//	es8388_hpvol_set(flacplayer.ucVolume);
//	es8388_spkvol_set(flacplayer.ucVolume);
//	es8388_sai_cfg(0, 3);  


    /* 4. 创建解码器 */
    flac_decoder = FLAC__stream_decoder_new();
    if (flac_decoder == NULL)
    {
        f_close(&flac_file);
        return;
    }

    FLAC__stream_decoder_set_md5_checking(flac_decoder, false);
    FLAC__stream_decoder_set_metadata_respond(flac_decoder, FLAC__METADATA_TYPE_STREAMINFO);

    if (FLAC__stream_decoder_init_stream(flac_decoder, flac_read_callback, NULL, NULL, NULL, NULL, 
                                         flac_write_callback, flac_metadata_callback, flac_error_callback, NULL) 
        != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        FLAC__stream_decoder_delete(flac_decoder);
        f_close(&flac_file);
        return;
    }

    /* 5. 开始解码 */
    flacplayer.ucStatus = FLAC_STA_PLAYING;
    
    printf("Start Decoding...\r\n"); // 打印开始标记
    
    FLAC__bool decode_ret = FLAC__stream_decoder_process_until_end_of_stream(flac_decoder);

    /* 【新增】 打印退出原因 */
    if (!decode_ret)
    {
        FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(flac_decoder);
        printf("Decode Fail! State: %s\r\n", FLAC__StreamDecoderStateString[state]);
    }
    else
    {
        printf("Decode Finished Normally.\r\n");
    }

    /* 6. 结束清理 */
//    SAI_Play_Stop();
    flacplayer.ucStatus = FLAC_STA_IDLE;
    FLAC__stream_decoder_delete(flac_decoder);
    f_close(&flac_file);
}

/* ------------------------------------------------------------------
 * 文件夹遍历循环播放 (主入口)
 * ------------------------------------------------------------------ */
void flacPlayerDemo(void)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char path[256];
    
    /* 1. 注册中断回调 */
    SAI_DMA_TX_Callback = FlacPlayer_SAI_DMA_TX_Callback;
    
    /* 2. 初始化默认音量 */
    flacplayer.ucVolume = 10; 
    
    printf("Start FLAC Loop in /MUSIC...\r\n");

    /* 3. 无限循环播放列表 */
    while(1)
    {
        /* 打开 MUSIC 文件夹 */
        res = f_opendir(&dir, "MUSIC"); // 确保 SD 卡根目录下有 MUSIC 文件夹
        if (res != FR_OK)
        {
            printf("Open /MUSIC dir failed: %d\r\n", res);
            return;
        }

        while(1)
        {
            /* 读取下一个文件 */
            res = f_readdir(&dir, &fno);
            
            /* 如果遇到错误或者读到目录末尾，退出内层循环，准备重新开始 */
            if (res != FR_OK || fno.fname[0] == 0) break;

			/* 【新增】打印当前扫描到的文件名，看看系统到底读到了什么 */
            printf("Scan: %s (Attr:0x%02X)\r\n", fno.fname, fno.fattrib);
			
            /* 跳过子目录，只处理文件 */
            if (fno.fattrib & AM_DIR) continue;

            /* 检查文件后缀 */
            if (is_flac_extension(fno.fname))
            {
                /* 拼接完整路径 */
                sprintf(path, "MUSIC/%s", fno.fname);
                
                /* 播放单个文件 (阻塞) */
                Flac_PlayFile(path);
                
                /* 播放完一首后，稍作延时或继续 */
                HAL_Delay(500); 
            }
        }
        
        /* 关闭目录，准备下一轮循环 */
        f_closedir(&dir);
        printf("Playlist end, restarting...\r\n");
    }
}

/* ------------------------------------------------------------------
 * DMA 中断回调
 * ------------------------------------------------------------------ */
void FlacPlayer_SAI_DMA_TX_Callback(void)
{
	/* 【新增】打印一个字符，证明中断活着 */
    printf(".");
    if ((DMA1_Stream2->CR & DMA_SxCR_CT) != 0) 
        bufflag = 0;
    else
        bufflag = 1;
    
    dma_needed = 1;
}

