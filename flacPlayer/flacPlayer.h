#ifndef __FLACPLAYER_H__
#define __FLACPLAYER_H__

#include <inttypes.h>
#include "main.h" 

/* 状态定义 */
enum
{
    FLAC_STA_IDLE = 0,   /* 待机状态 */
    FLAC_STA_PLAYING,    /* 放音状态 */
    FLAC_STA_ERR,        /* 出错 */
    FLAC_STA_NEXT,       /* 切歌（预留） */
};

/* FLAC播放器控制结构体 */
typedef struct
{
    uint8_t ucVolume;       /* 当前放音音量 */
    uint8_t ucStatus;       /* 状态 */    
    uint32_t ucFreq;        /* 采样频率 */
    uint8_t  ucChannels;    /* 声道数 */
    uint8_t  ucBits;        /* 采样位数 */
} FLAC_PLAYER_TYPE; 

/* 对外接口 */
void flacPlayerDemo(void); /* 现在的Demo不需要参数，自动扫描 MUSIC 文件夹 */
void FlacPlayer_SAI_DMA_TX_Callback(void);

#endif /* __FLACPLAYER_H__ */

