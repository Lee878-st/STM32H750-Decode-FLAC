#include "./sai/bsp_sai.h" 

//#include "./mp3Player/mp3Player.h"
SAI_HandleTypeDef h_sai;
DMA_HandleTypeDef h_txdma;   //DMA发送句柄
void (*SAI_DMA_TX_Callback)(void);
void SAI_DMAConvCplt(DMA_HandleTypeDef *hdma);
/* SAI DMA回调函数指针 */
void (*sai1_tx_callback)(void);  /* TX回调函数 */

/**
 * @brief       开启SAIA的DMA功能,HAL库没有提供此函数
 * @note        因此我们需要自己操作寄存器编写一个
 * @param       无
 * @retval      无
 */
void sai1_saia_dma_enable(void)
{
    uint32_t tempreg = 0;
    tempreg = SAI1_Block_A->CR1;            /* 先读出以前的设置 */
    tempreg |= 1 << 17;                     /* 使能DMA */
    SAI1_Block_A->CR1 = tempreg;            /* 写入CR1寄存器中 */
}

/**
 * @brief       SAI1 Block A初始化, I2S,飞利浦标准
 * @param       mode    : 00,主发送器;01,主接收器;10,从发送器;11,从接收器
 * @param       cpol    : 0,时钟下降沿选通;1,时钟上升沿选通
 * @param       datalen : 数据大小,0/1,未用到,2,8位;3,10位;4,16位;5,20位;6,24位;7,32位.
 * @retval      无
 */
void sai1_saia_init(uint32_t mode, uint32_t cpol, uint32_t datalen)
{
	SAI_CLK_ENABLE();
    HAL_SAI_DeInit(&h_sai);                            /* 清除以前的配置 */

    h_sai.Instance = SAI1_Block_A;                     /* SAI1 Bock A */
    h_sai.Init.AudioMode = mode;                       /* 设置SAI1工作模式 */
    h_sai.Init.Synchro = SAI_ASYNCHRONOUS;             /* 音频模块异步 */
    h_sai.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;   /* 立即驱动音频模块输出 */
    h_sai.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;   /* 使能主时钟分频器(MCKDIV) */
    h_sai.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_FULL;  /* 设置FIFO阈值 */
    h_sai.Init.MonoStereoMode = SAI_STEREOMODE;        /* 立体声模式 */
    h_sai.Init.Protocol = SAI_FREE_PROTOCOL;           /* 设置SAI1协议为:自由协议(支持I2S/LSB/MSB/TDM/PCM/DSP等协议) */
    h_sai.Init.DataSize = datalen;                     /* 设置数据大小 */
    h_sai.Init.FirstBit = SAI_FIRSTBIT_MSB;            /* 数据MSB位优先 */
    h_sai.Init.ClockStrobing = cpol;                   /* 数据在时钟的上升/下降沿选通 */
    h_sai.Init.MckOutput = SAI_MCK_OUTPUT_ENABLE;
	
    /* 帧设置 */
    h_sai.FrameInit.FrameLength = 64;                  /* 设置帧长度为64,左通道32个SCK,右通道32个SCK. */
    h_sai.FrameInit.ActiveFrameLength = 32;            /* 设置帧同步有效电平长度,在I2S模式下=1/2帧长 */
    h_sai.FrameInit.FSDefinition = SAI_FS_CHANNEL_IDENTIFICATION;/* FS信号为SOF信号+通道识别信号 */
    h_sai.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;    /* FS低电平有效(下降沿) */
    h_sai.FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;  /* 在slot0的第一位的前一位使能FS,以匹配飞利浦标准 */

    /* SLOT设置 */
    h_sai.SlotInit.FirstBitOffset = 0;                 /* slot偏移(FBOFF)为0 */
    h_sai.SlotInit.SlotSize = SAI_SLOTSIZE_32B;        /* slot大小为32位 */
    h_sai.SlotInit.SlotNumber = 2;                     /* slot数为2个 */
    h_sai.SlotInit.SlotActive = SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1;/* 使能slot0和slot1 */
    
    HAL_SAI_Init(&h_sai);                              /* 初始化SAI */
//    __HAL_SAI_ENABLE(&h_sai);                          /* 使能SAI */
}


/**
 * @brief       SAI底层驱动，引脚配置，时钟使能
 * @note        此函数会被HAL_SAI_Init()调用
 * @param       hsdram:SAI句柄
 * @retval      无
 */
void HAL_SAI_MspInit(SAI_HandleTypeDef *hsai)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	//Msp Clock Init
	SAI_CLK_ENABLE();
	SAI_LRC_GPIO_CLK_ENABLE();
	SAI_BCLK_GPIO_CLK_ENABLE();
	SAI_SDA_GPIO_CLK_ENABLE();
	SAI_MCLK_GPIO_CLK_ENABLE();  
	SAI_SDB_GPIO_CLK_ENABLE();

	//SAI1_blockA
	GPIO_InitStruct.Pin = SAI_LRC_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = SAI_LRC_GPIO_AF;
	HAL_GPIO_Init(SAI_LRC_PORT, &GPIO_InitStruct);  

	GPIO_InitStruct.Pin = SAI_BCLK_PIN;
	GPIO_InitStruct.Alternate = SAI_BCLK_GPIO_AF;
	HAL_GPIO_Init(SAI_BCLK_PORT, &GPIO_InitStruct);    

	GPIO_InitStruct.Pin = SAI_SDA_PIN;
	GPIO_InitStruct.Alternate = SAI_SDA_GPIO_AF;
	HAL_GPIO_Init(SAI_SDA_PORT, &GPIO_InitStruct);   

	GPIO_InitStruct.Pin = SAI_SDB_PIN;
	GPIO_InitStruct.Alternate = SAI_SDB_GPIO_AF;
	HAL_GPIO_Init(SAI_SDB_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = SAI_MCLK_PIN;
	GPIO_InitStruct.Alternate = SAI_MCLK_GPIO_AF;
	HAL_GPIO_Init(SAI_MCLK_PORT, &GPIO_InitStruct);     
}


/* SAI Block A采样率设置 
 * 采样率计算公式(以NOMCK=0,OSR=0为前提):
 * Fmclk = 256*Fs = sai_x_ker_ck / MCKDIV[5:0]
 * Fs = sai_x_ker_ck / (256 * MCKDIV[5:0])
 * Fsck = Fmclk * (FRL[7:0]+1) / 256 = Fs * (FRL[7:0] + 1)
 * 其中:
 * sai_x_ker_ck = (HSE / PLL2DIVM) * (PLL2DIVN + 1) / (PLL2DIVP + 1)
 * HSE:一般为25Mhz
 * PLL2DIVM     :1~63,表示1~63分频
 * PLL2DIVN     :3~511,表示4~512倍频
 * PLL2DIVP     :0~127,表示1~128分频
 * MCKDIV       :0~63,表示1~63分频,0也是1分频,推荐设置为偶数
 * SAI A分频系数表@PLL2DIVM=25,HSE=25Mhz,即vco输入频率为1Mhz
 * 表格式:
 * 采样率|(PLL2DIVN+1)|(PLL2DIVP+1)|MCKDIV
 */
const uint16_t SAI_PSC_TBL[][5]=
{
    { 800 , 344, 7, 0, 12 },    /* 8Khz采样率 */
    { 1102, 429, 2, 18, 2 },    /* 11.025Khz采样率 */
    { 1600, 344, 7, 0, 6 },     /* 16Khz采样率 */
    { 2205, 429, 2, 18, 1 },    /* 22.05Khz采样率 */
    { 3200, 344, 7, 0, 3 },     /* 32Khz采样率 */
    { 4410, 429, 2, 18, 0 },    /* 44.1Khz采样率 */
    { 4800, 344, 7, 0, 2 },     /* 48Khz采样率 */
    { 8820, 271, 2, 2, 1 },     /* 88.2Khz采样率 */
    { 9600, 344, 7, 0, 1 },     /* 96Khz采样率 */
    { 17640, 271, 2, 2, 0 },    /* 176.4Khz采样率  */
    { 19200, 344, 7, 0, 0 },    /* 192Khz采样率 */
};

/**
 * @brief       设置SAIA的时钟和采样率(@MCKEN)
 * @param       samplerate:采样率, 单位:Hz
 * @retval      0,设置成功
 *              1,无法设置
 */
uint8_t sai1_samplerate_set(uint32_t samplerate)
{   
    uint8_t i = 0; 
    
    RCC_PeriphCLKInitTypeDef rcc_sai1_sture;  

    for (i = 0; i < (sizeof(SAI_PSC_TBL) / 10); i++)                    /* 看看改采样率是否可以支持 */
    {
        if ((samplerate / 10) == SAI_PSC_TBL[i][0])
        {
            break;
        }
    }

    if (i == (sizeof(SAI_PSC_TBL) / 10))
    {
        return 1;                       /* 搜遍了也找不到 */
    }

    rcc_sai1_sture.PeriphClockSelection = RCC_PERIPHCLK_SAI1;           /* 外设时钟源选择 */ 
    rcc_sai1_sture.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;         /* 设置PLL2 */
    rcc_sai1_sture.PLL2.PLL2M = 25;                                     /* 设置PLL2M */
    rcc_sai1_sture.PLL2.PLL2N = (uint32_t)SAI_PSC_TBL[i][1];            /* 设置PLL2N */
    rcc_sai1_sture.PLL2.PLL2P = (uint32_t)SAI_PSC_TBL[i][2];            /* 设置PLL2P */
    HAL_RCCEx_PeriphCLKConfig(&rcc_sai1_sture);                         /* 设置时钟 */

    __HAL_SAI_DISABLE(&h_sai);                                /* 关闭SAI */
    h_sai.Init.AudioFrequency = samplerate;                   /* 设置播放频率 */
    HAL_SAI_Init(&h_sai);                                     /* 初始化SAI */
    sai1_saia_dma_enable();                                   /* 开启SAI的DMA功能 */
    __HAL_SAI_ENABLE(&h_sai);                                 /* 开启SAI */

    return 0;
}


/**
 * @brief       SAIA TX DMA配置
 * @note        设置为双缓冲模式,并开启DMA传输完成中断
 * @param       buf0:M0AR地址.
 * @param       buf1:M1AR地址.
 * @param       num:每次传输数据量
 * @param       width:位宽(存储器和外设,同时设置),0,8位;1,16位;2,32位;
 * @retval      0,设置成功
 *              1,无法设置
 */
void sai1_tx_dma_init(uint8_t *buf0, uint8_t *buf1, uint16_t num, uint8_t width)
{
	uint32_t memwidth = 0, perwidth = 0;      /* 外设和存储器位宽 */

    switch (width)
    {
        case 0:         /* 8位 */
            memwidth = DMA_MDATAALIGN_BYTE;
            perwidth = DMA_PDATAALIGN_BYTE;
            break;

        case 1:         /* 16位 */
            memwidth = DMA_MDATAALIGN_HALFWORD;
            perwidth = DMA_PDATAALIGN_HALFWORD;
            break;

        case 2:         /* 32位 */
            memwidth = DMA_MDATAALIGN_WORD;
            perwidth = DMA_PDATAALIGN_WORD;
            break;
    }

    SAI1_TX_DMA_CLK_ENABLE();                                        /* 使能DMA时钟 */
    __HAL_LINKDMA(&h_sai, hdmatx, h_txdma);   /* 将DMA与SAI联系起来 */

    h_txdma.Instance = SAI1_TX_DMASx;                   /* DMA2数据流3 */
    h_txdma.Init.Request = DMA_REQUEST_SAI1_A;          /* SAI1 Block A */
    h_txdma.Init.Direction = DMA_MEMORY_TO_PERIPH;      /* 存储器到外设模式 */
    h_txdma.Init.PeriphInc = DMA_PINC_DISABLE;          /* 外设非增量模式 */
    h_txdma.Init.MemInc = DMA_MINC_ENABLE;              /* 存储器增量模式 */
    h_txdma.Init.PeriphDataAlignment = perwidth;        /* 外设数据长度:16/32位 */
    h_txdma.Init.MemDataAlignment = memwidth;           /* 存储器数据长度:16/32位 */
    h_txdma.Init.Mode = DMA_CIRCULAR;                   /* 使用循环模式 */ 
    h_txdma.Init.Priority = DMA_PRIORITY_VERY_HIGH;          /* 高优先级 */
    h_txdma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;        /* 使用FIFO */
	h_txdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    h_txdma.Init.MemBurst = DMA_MBURST_SINGLE;          /* 存储器单次突发传输 */
    h_txdma.Init.PeriphBurst = DMA_PBURST_SINGLE;       /* 外设突发单次传输 */ 
    HAL_DMA_DeInit(&h_txdma);                           /* 先清除以前的设置 */
    HAL_DMA_Init(&h_txdma);                             /* 初始化DMA */
	
	/* 🚀【新增】清除所有错误标志，防止复位后残留 */
    __HAL_DMA_CLEAR_FLAG(&h_txdma, DMA_FLAG_FEIF3_7 | DMA_FLAG_DMEIF3_7 | DMA_FLAG_TEIF3_7 | DMA_FLAG_HTIF3_7 | DMA_FLAG_TCIF3_7);

    HAL_DMAEx_MultiBufferStart(&h_txdma, (uint32_t)buf0, (uint32_t)&SAI1_Block_A->DR, (uint32_t)buf1, num);/* 开启双缓冲 */
    __HAL_DMA_DISABLE(&h_txdma);                        /* 先关闭DMA */
    CPU_TS_Tmr_Delay_US(10);                            /* 10us延时，防止-O2优化出问题 */

    __HAL_DMA_ENABLE_IT(&h_txdma, DMA_IT_TC);           /* 开启传输完成中断 */
    __HAL_DMA_CLEAR_FLAG(&h_txdma, SAI1_TX_DMASx_FLAG); /* 清除DMA传输完成中断标志位 */
    HAL_NVIC_SetPriority(SAI1_TX_DMASx_IRQ, 0, 0);      /* DMA中断优先级 */
    HAL_NVIC_EnableIRQ(SAI1_TX_DMASx_IRQ);

}

/**
 * @brief       DMA2_Stream3中断服务函数
 * @param       无
 * @retval      无
 */
void SAI1_TX_DMASx_IRQHandler(void)
{
    if (__HAL_DMA_GET_FLAG(&h_txdma, SAI1_TX_DMASx_FLAG) != RESET) /* DMA传输完成 */
    {
        __HAL_DMA_CLEAR_FLAG(&h_txdma, SAI1_TX_DMASx_FLAG);        /* 清除DMA传输完成中断标志位 */
        if (sai1_tx_callback != NULL)
        {
            sai1_tx_callback();  /* 执行回调函数,读取数据等操作在这里面处理 */
        }
    }
}


/**
 * @brief       SAI开始播放
 * @param       无
 * @retval      无
 */
void sai1_play_start(void)
{
	/* 1. 先开启 DMA 传输 */
    __HAL_DMA_ENABLE(&h_txdma); 
    
    /* 2. 开启 SAI 的 DMA 请求 (此时 SAI 还是关的，写入有效) */
    SAI1_Block_A->CR1 |= SAI_xCR1_DMAEN; 
    
    /* 3. 最后开启 SAI 模块，开始产生时钟并请求数据 */
    __HAL_SAI_ENABLE(&h_sai);}

/**
 * @brief       关闭SAI播放
 * @param       无
 * @retval      无
 */
void sai1_play_stop(void)
{   
    __HAL_DMA_DISABLE(&h_txdma);  /* 结束播放 */
} 



