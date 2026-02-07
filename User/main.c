#include "stm32h7xx.h"
#include "main.h"
#include "./led/bsp_led.h" 
#include "./usart/bsp_debug_usart.h"
#include "./sd_card/bsp_sdio_sd.h"
//#include "./key/bsp_key.h" 
#include "./delay/core_delay.h"
#include "./es8388/es8388.h"
#include "./sai/bsp_sai.h" 

//#include "./mp3Player/mp3Player.h"
#include "./flacPlayer/flacPlayer.h"

/* FatFs includes component */
#include "ff.h"
#include "ff_gen_drv.h"
#include "sd_diskio.h"
/**
  ******************************************************************************
  *                              定义变量
  ******************************************************************************
  */
char SDPath[4]; /* SD逻辑驱动器路径 */
FATFS fs;		/* FatFs文件系统对象 */
FIL fnew;		/* file objects */


extern Diskio_drvTypeDef  SD_Driver;
/**
  * @brief  CPU L1-Cache enable.
  * @param  None
  * @retval None
  */
static void CPU_CACHE_Enable(void)
{
  /* Enable I-Cache */
  SCB_EnableICache();

  /* Enable D-Cache */
  SCB_EnableDCache();

  //将Cache设置write-through方式
  //SCB->CACR|=1<<2;
}

/**
  * @brief  主函数
  * @param  无
  * @retval 无
  */
int main(void)
{
	FRESULT result; 
	/* 系统时钟初始化成480MHz */
	SystemClock_Config();
	CPU_CACHE_Enable();
	LED_GPIO_Config();
	/* 初始化USART1 配置模式为 115200 8-N-1 */
	DEBUG_USART_Config();	
	//链接驱动器，创建盘符
	FATFS_LinkDriver(&SD_Driver, SDPath);
	//在外部SD卡挂载文件系统，文件系统挂载时会对SD卡初始化
	result = f_mount(&fs,"0:",1);  
	if(result!=FR_OK)
	{
		printf("\n SD卡文件系统挂载失败\n");
		while(1);
	}
	printf("\n SD卡文件系统挂载成功\n音乐播放器\n");
	printf("正在检测es8388.....\n");
	    
	HAL_Delay(500);
	LED1_OFF;

	es8388_init();                      /* ES8388初始化 */
	es8388_adda_cfg(1, 0);              /* 开启DAC关闭ADC */
	es8388_output_cfg(1, 1);            /* DAC选择通道输出 */
	es8388_hpvol_set(25);               /* 设置耳机音量 */
	es8388_spkvol_set(20);              /* 设置喇叭音量 */

	while (1)
	{
		flacPlayerDemo();
	}
}
/**
  * @brief  System Clock 配置
  *         system Clock 配置如下: 
	*            System Clock source  = PLL (HSE)
	*            SYSCLK(Hz)           = 480000000 (CPU Clock)
	*            HCLK(Hz)             = 240000000 (AXI and AHBs Clock)
	*            AHB Prescaler        = 2
	*            D1 APB3 Prescaler    = 2 (APB3 Clock  120MHz)
	*            D2 APB1 Prescaler    = 2 (APB1 Clock  120MHz)
	*            D2 APB2 Prescaler    = 2 (APB2 Clock  120MHz)
	*            D3 APB4 Prescaler    = 2 (APB4 Clock  120MHz)
	*            HSE Frequency(Hz)    = 25000000
	*            PLL_M                = 5
	*            PLL_N                = 192
	*            PLL_P                = 2
	*            PLL_Q                = 4
	*            PLL_R                = 2
	*            VDD(V)               = 3.3
	*            Flash Latency(WS)    = 4
  * @param  None
  * @retval None
  */
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/** 启用电源配置更新
	*/
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
	/** 配置主内稳压器输出电压
	*/
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

	while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
	/** 初始化CPU、AHB和、APB总线时钟
	*/
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 5;
	RCC_OscInitStruct.PLL.PLLN = 192;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
	}
	/** 初始化CPU、AHB和、APB总线时钟
	*/
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
							  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
							  |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
	}
}
/****************************END OF FILE***************************/
