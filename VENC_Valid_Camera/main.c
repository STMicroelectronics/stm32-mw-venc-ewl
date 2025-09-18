/**
******************************************************************************
* @file    main.c
* @author  MCD Application Team
* @brief   This project is a HAL template project for STM32N6xx devices.
******************************************************************************
* @attention
*
* Copyright (c) 2023 STMicroelectronics.
* All rights reserved.
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
*/

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stdio.h"
#include "ewl.h"

#include "H264EncApi.h"
#include "ov5640.h"
#include "stm32n6xx_ll_venc.h"
#include "stm32n6570_discovery.h"
#include "stm32n6570_discovery_sd.h"
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6570_discovery_camera.h"
#include "stm32n6570_discovery_xspi.h"
#if defined(USE_FREERTOS)
#include "cmsis_os.h"
#elif defined(USE_THREADX)
#include "tx_api.h"
#endif

/** @addtogroup Templates
* @{
*/

/** @addtogroup HAL
* @{
*/

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define N10(val) (((val) ^ 0x7FF) + 1)
#define FRAMERATE 30

#define EXTFLASH_OUTPUT_BASE (0x0000000U)
#define EXTFLASH_OUTPUT_SIZE (MX66UW1G45G_BLOCK_64K * 1024)

#define EXTFLASH_INPUT_BASE (0)
#define EXTFLASH_INPUT_SIZE (MX66UW1G45G_BLOCK_64K * 1024)

#define EXTFLASH_BLOCK_SIZE MX66UW1G45G_BLOCK_64K

#if USE_COM_LOG
#define TRACE_MAIN(...) printf(__VA_ARGS__)
#else
#define TRACE_MAIN(...)
#endif

#define USE_SD_AS_OUTPUT 1
#define USE_NOR_AS_OUTPUT 0

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/


uint16_t * pipe_buffer[2];
uint8_t buffer_idx = 0;
uint8_t buf_index_changed = 0;
H264EncIn encIn= {0};
H264EncOut encOut= {0};
H264EncInst encoder= {0};
H264EncConfig cfg= {0};
uint32_t output_size = 0;

EWLLinearMem_t outbuf;
static int frame_nb = 0;
BSP_XSPI_NOR_Init_t Flash;
#if defined(USE_FREERTOS)

#elif defined(USE_THREADX)
uint8_t thread_stack[2048];
TX_BYTE_POOL byte_pool;
TX_THREAD main_thread;
#endif

/* Private function prototypes -----------------------------------------------*/
static void SystemClock_Config(void);
static int encoder_prepare(uint32_t width, uint32_t height, uint32_t * output_buffer);
static int Encode_frame(void);
static int encoder_end(void);
#if defined(USE_FREERTOS)
void main_thread_func(void * arg);
#elif defined(USE_THREADX)
void main_thread_func(ULONG arg);
#endif

/* change save stream and read frame for preferred in/output*/
static int save_stream(uint32_t offset, uint8_t * buf, size_t size);
static int erase_enc_output(void);

/* Private functions ---------------------------------------------------------*/
uint8_t * mem_buf ;
uint8_t * next_buf;
size_t buf_index = 0;
size_t SD_index = 0;
/**
* @brief  Save an encoded buffer fragment at the given offset.
* @param  offset 
* @param  buf  pointer to the buffer to save
* @param  size  size (in bytes) of the buffer to save
* @retval err error code. 0 On success.
*/
int save_stream(uint32_t offset, uint8_t * buf, size_t size){
  int err = 0;
#if USE_SD_AS_OUTPUT
  for(int i = 0; i<size; i++){
    mem_buf[buf_index] = buf[i];
    buf_index++;
    if(buf_index >= 0x40000){

      if(BSP_SD_WriteBlocks_DMA(0, (uint32_t *) mem_buf, SD_index, 512)!= BSP_ERROR_NONE){
        err = -1;
      }
      SD_index+=512;
      buf_index = 0;
      uint8_t * temp = mem_buf;
      mem_buf = next_buf;
      next_buf = temp;
    }
  }
#elif USE_NOR_AS_OUTPUT
    /* saves image to external flash at offset 0x400 0000 */
  if (BSP_XSPI_NOR_Write(0, buf, EXTFLASH_OUTPUT_BASE + offset, size) != BSP_ERROR_NONE)
  {
    printf("failed to write buffer to flash\n");
    return -1;
  }
#endif
  return err;
}

int flush_out_buffer(void){
#if USE_SD_AS_OUTPUT
  if(BSP_SD_WriteBlocks(0, (uint32_t *) mem_buf, SD_index, 512)!= BSP_ERROR_NONE){
        return -1;
      }
#elif USE_NOR_AS_OUTPUT
#endif
  return 0;
}
/**
* @brief  erases data in output medium
* @retval err error code. 0 On success.
*/
int erase_enc_output(void){
  /* flash must be erased by blocks of 64 bytes */
#if USE_SD_AS_OUTPUT
  /* only 100 blocks are erased because erasing the 64M would take forever */
  if (BSP_SD_Erase(0, 0, 1943552) != BSP_ERROR_NONE)
  {
    TRACE_MAIN("failed to erase external flash block nb \n");
    return -1;
  }
#elif USE_NOR_AS_OUTPUT
    for(int i = 0; i < 600 ; i++){
    if (BSP_XSPI_NOR_Erase_Block(0, EXTFLASH_OUTPUT_BASE+i*EXTFLASH_BLOCK_SIZE, MX66UW1G45G_ERASE_64K) != BSP_ERROR_NONE)
    {
      printf("failed to erase external flash block nb %d\n", i);
      return -1;
    }
  }
#endif
  return 0;
}




/**
* @brief  Main program
* @param  None
* @retval None
*/
int main(void)
{
  /* Enable all AXISRAMx*/
  __HAL_RCC_RAMCFG_CLK_ENABLE();
  LL_MEM_EnableClock(LL_MEM_AXISRAM3);
  LL_MEM_EnableClock(LL_MEM_AXISRAM4); 
  LL_MEM_EnableClock(LL_MEM_AXISRAM5);
  LL_MEM_EnableClock(LL_MEM_AXISRAM6);
  
  /*enable all AXISRAM SD bits */
  *(volatile uint32_t *) 0x52023100U &= ~(0b1U<<20);
  *(volatile uint32_t *) 0x52023180U &= ~(0b1U<<20);
  *(volatile uint32_t *) 0x52023200U &= ~(0b1U<<20);
  *(volatile uint32_t *) 0x52023280U &= ~(0b1U<<20);
  
  /* set all required IPs as secure privileged */
  __HAL_RCC_RIFSC_CLK_ENABLE();
  RIFSC->RIMC_ATTRx[2] = 0x310;
  RIFSC->RIMC_ATTRx[3] = 0x310;
  RIFSC->RIMC_ATTRx[8] = 0x310;
  RIFSC->RIMC_ATTRx[9] = 0x310;
  RIFSC->RIMC_ATTRx[10] = 0x310;  
  RIFSC->RIMC_ATTRx[11] = 0x310;
  RIFSC->RIMC_ATTRx[12] = 0x310;
  RIFSC->RISC_SECCFGRx[1] |= 0b0110 << 20;
  RIFSC->RISC_PRIVCFGRx[1] |= 0b0110 << 20;
  RIFSC->RISC_SECCFGRx[2] |= 0b0111 << 28;
  RIFSC->RISC_PRIVCFGRx[2] |= 0b0111 << 28;
  RIFSC->RISC_SECCFGRx[3] |= 0b111100010;
  RIFSC->RISC_PRIVCFGRx[3] |= 0b111100010;
  
  /* allocate SD card double buffer */
  mem_buf = (uint8_t *) malloc(0x40000);
  next_buf = (uint8_t *) malloc(0x40000);
  
  /* Initialize the HAL timebase (eg. SysTick) */
  HAL_Init();
  
  /* Configure the system clock to have a frequency of 400 */
  SystemClock_Config();
  /*Check expected frequency  */
#if defined(USE_FREERTOS)
  BaseType_t ret = xTaskCreate(main_thread_func, "main_thread", 512, NULL, 5, NULL);
  
   vTaskStartScheduler();
  
  for(;;);
}
void main_thread_func(void * arg){
#elif  defined(USE_THREADX)

  tx_kernel_enter();

}

/**
* @brief  Main program
* @param  None
* @retval None
*/
void tx_application_define(void *first_unused_memory)
{
  void *thread_stack_pointer;
  tx_byte_pool_create(&byte_pool, "byte pool", thread_stack, 2048);
  tx_byte_allocate(&byte_pool, 
                   &thread_stack_pointer, 2000, TX_NO_WAIT);
  uint32_t status = tx_thread_create(&main_thread,
              "main_thread",
              main_thread_func, 0,
              thread_stack_pointer, 2000,
              8, 8,
              TX_NO_TIME_SLICE, TX_AUTO_START);
}

void main_thread_func(ULONG arg){
#endif
  /* UART log */
#if USE_COM_LOG
  COM_InitTypeDef COM_Init;
  
  /* Initialize COM init structure */
  COM_Init.BaudRate   = 256000;
  COM_Init.WordLength = COM_WORDLENGTH_8B;
  COM_Init.StopBits   = COM_STOPBITS_1;
  COM_Init.Parity     = COM_PARITY_NONE;
  COM_Init.HwFlowCtl  = COM_HWCONTROL_NONE;
  
  BSP_COM_Init(COM1, &COM_Init);
  

  if (BSP_COM_SelectLogPort(COM1) != BSP_ERROR_NONE)
  {
    TRACE_MAIN("failed to set up log port\n");
    __BKPT(0);
  }
#endif
  /* initialize LEDs to signal processing is ongoing */
  BSP_LED_Init(0);
  BSP_LED_Init(1);
  BSP_LED_On(0);
  BSP_LED_On(1);
  
  TRACE_MAIN("CPU frequency    : %d\n", HAL_RCC_GetCpuClockFreq() / 1000000);
  TRACE_MAIN("sysclk frequency : %d\n", HAL_RCC_GetSysClockFreq() / 1000000);
  TRACE_MAIN("pclk5 frequency  : %d\n", HAL_RCC_GetPCLK5Freq() / 1000000);
  
  /* initialize ext flash interface and driver */
#if USE_SD_AS_OUTPUT
  if (BSP_SD_Init(0) != BSP_ERROR_NONE){
    TRACE_MAIN("error initializing NOR flash\n");
    __BKPT(0);
  }
  BSP_SD_CardInfo card_info;
  BSP_SD_GetCardInfo(0, &card_info);
  printf("SD card info : \nblock Nbr : %d\nblock size : %d\ncard speed : %d\n", card_info.BlockNbr, card_info.BlockSize, card_info.CardSpeed);
#elif USE_NOR_AS_OUTPUT
    /* enable ext flash power domain */
  HAL_PWREx_EnableVddIO3();
   if (BSP_XSPI_NOR_Init(0, &Flash) != BSP_ERROR_NONE){
    __BKPT(0);
  }
  
#endif
  /* Initialize LCD */
  int err = BSP_LCD_InitEx(0,LCD_ORIENTATION_LANDSCAPE, LCD_PIXEL_FORMAT_RGB565,LCD_DEFAULT_WIDTH, LCD_DEFAULT_HEIGHT);
  if(err){
    TRACE_MAIN("error initializing LCD\n");
    __BKPT(0);
  }
  BSP_LCD_SetLayerAddress(0, 0, 0x34300000);
  
  /* Initialize camera */
  if(BSP_CAMERA_Init(0,CAMERA_R800x480, CAMERA_PF_RGB565, HAL_DCMIPP_MAIN_PIPE) != BSP_ERROR_NONE){
    __BKPT(0);
  }
  

  BSP_CAMERA_SetMirrorFlip(0, OV5640_MIRROR_FLIP_NONE);

  /* Increase camera from 15fps to 30fps */
  uint8_t tmp = 0x38;
  ov5640_write_reg(&((OV5640_Object_t *)Camera_CompObj)->Ctx, OV5640_SC_PLL_CONTRL2,  &tmp, 1);
  
  /* erase output*/
  TRACE_MAIN("erasing flash output blocks\n");
  erase_enc_output();
  TRACE_MAIN("Done erasing output flash blocks\n");
#if USE_SD_AS_OUTPUT
  while(BSP_SD_GetCardState(0) != SD_TRANSFER_OK);
#endif
  
  /* initialize VENC */
  LL_VENC_Init();

  /* allocate output buffer */
  uint32_t * output_buffer = (uint32_t *)malloc(800*480/4);
  
  /* initialize encoder software for camera feed encoding */
  encoder_prepare(800,480,output_buffer);
  
  /* start camera acquisition */
  if(BSP_CAMERA_Start(0,HAL_DCMIPP_MAIN_PIPE, HAL_DCMIPP_MODE_CONTINUOUS)!= BSP_ERROR_NONE){
    __BKPT(0);
  }
  
  while (1)
  {
    if(buf_index_changed){
      /* new frame available */
      buf_index_changed = 0;
      Encode_frame();
      if(output_size > (1024*32*1024)){
        /* if output is of the desired sive, end program */
        encoder_end();
        flush_out_buffer();
        BSP_LED_Off(0);
        BSP_LED_Off(1);
        __BKPT(0);
      }
    }
  }
}


static int encoder_prepare(uint32_t width, uint32_t height, uint32_t * output_buffer)
{
  H264EncRet ret;
  
  H264EncPreProcessingCfg preproc_cfg = {0};
  //H264EncCodingCtrl codingCtrl = {0};
  frame_nb = 0;
  /* Step 1: Initialize an encoder instance */
  /* set config to 1 ref frame */
  cfg.refFrameAmount = 1;
  /* 30 fps frame rate */
  cfg.frameRateDenom = 1;
  cfg.frameRateNum = FRAMERATE;
  /* Image resolution */
  cfg.width = width;
  cfg.height = height;
  /* Stream type */
  cfg.streamType = H264ENC_BYTE_STREAM;
  
  /* encoding level*/
  /*See API guide for level depending on resolution and framerate*/
  cfg.level = H264ENC_LEVEL_2_2;
  cfg.svctLevel = 0;
  
  /* Output buffer size */
  outbuf.size = cfg.width * cfg.height;
  
  ret = H264EncInit(&cfg, &encoder);
  if (ret != H264ENC_OK)
  {
    TRACE_MAIN("error initializing encoder %d\n", ret);
    return -1;
  }
  
  /* set format conversion for preprocessing */
  ret = H264EncGetPreProcessing(encoder, &preproc_cfg);
  if(ret != H264ENC_OK){
    TRACE_MAIN("error getting preproc data\n");
    return -1;
  }
  preproc_cfg.inputType = H264ENC_RGB565;
  ret = H264EncSetPreProcessing(encoder, &preproc_cfg);
  if(ret != H264ENC_OK){
    TRACE_MAIN("error setting preproc data\n");
    return -1;
  }
  /*assign buffers to input structure */
  encIn.pOutBuf = output_buffer;
  encIn.busOutBuf = (uint32_t) output_buffer;
  encIn.outBufSize = width * height;
  
  /* create stream */
  ret = H264EncStrmStart(encoder, &encIn, &encOut);
  if (ret != H264ENC_OK)
  {
    TRACE_MAIN("error starting stream\n");
    return -1;
  }
  
  /* save the stream header */
  if (save_stream(output_size, (uint8_t *)encIn.pOutBuf,  encOut.streamSize))
  {
    __BKPT(0);
    TRACE_MAIN("error saving stream\n");
    return -1;
  }
  TRACE_MAIN("stream started. saved %d bytes\n", encOut.streamSize);
  output_size+= encOut.streamSize;
  return 0;
}


static int Encode_frame(void){
  int ret = H264ENC_FRAME_READY;
  if (!frame_nb)
  {
    /* if frame is the first : set as intra coded */
    encIn.timeIncrement = 0;
    encIn.codingType = H264ENC_INTRA_FRAME;
  }
  else
  {
    /* if there was a frame previously, set as predicted */
    encIn.timeIncrement = 1;
    encIn.codingType = H264ENC_PREDICTED_FRAME;
//      encIn.codingType = H264ENC_INTRA_FRAME;
  }
  encIn.ipf = H264ENC_REFERENCE_AND_REFRESH;
  encIn.ltrf = H264ENC_REFERENCE;
  /* set input buffers to structures (planar YUV420) */
  encIn.busLuma =(uint32_t) 0x34300000;
  ret = H264EncStrmEncode(encoder, &encIn, &encOut, NULL, NULL, NULL);
  switch (ret)
  {
  case H264ENC_FRAME_READY:
    /*save stream */
    if (save_stream(output_size, (uint8_t *)encIn.pOutBuf,  encOut.streamSize))
    {
      TRACE_MAIN("error saving stream frame %d\n", frame_nb);
      return -1;
    }
    output_size += encOut.streamSize;
    break;
  case H264ENC_SYSTEM_ERROR:
    TRACE_MAIN("fatal error while encoding\n");
    break;
  default:
    TRACE_MAIN("error encoding frame %d : %d\n", frame_nb, ret);
    break;
  }
  frame_nb++;
  return 0;
}


static int encoder_end(void){
  int ret = H264EncStrmEnd(encoder, &encIn, &encOut);
  TRACE_MAIN("done encoding %d frames. size : %d\n",frame_nb ,output_size);
  if (ret != H264ENC_OK)
  {
    return -1;
  }
  else
  {
    /* save stream tail */
    if (save_stream(output_size, (uint8_t *)encIn.pOutBuf,  encOut.streamSize))
    {
      TRACE_MAIN("error saving stream\n");
      return -1;
    }
    output_size+=encOut.streamSize;
  }

  return 0;
}

/**
* @brief  System Clock Configuration
*         The system Clock is configured as follow :
*            CPU Clock source               = IC1
*            System bus Clock source        = IC2
*            SYSCLK(Hz)                     = 400000000
*            HCLK(Hz)                       = 200000000
*            AHB Prescaler                  = 2
*            APB1 Prescaler                 = 1
*            APB2 Prescaler                 = 1
*            APB4 Prescaler                 = 1
*            APB5 Prescaler                 = 1
*            HSE Frequency(Hz)              = 48000000
*            PLL1 State                     = ON
*            PLL2 State                     = OFF
*            PLL3 State                     = OFF
*            PLL4 State                     = OFF
* @retval None
*/
static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  
  
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  // PLL1: 48 x 100 / 6 = 800
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL1.PLLM = 6;
  RCC_OscInitStruct.PLL1.PLLN = 100;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  
  
  // PLL2: 48 x 125 / 6 = 1000MHz
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL2.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL2.PLLM = 6;
  RCC_OscInitStruct.PLL2.PLLFractional = 0;
  RCC_OscInitStruct.PLL2.PLLN = 125;
  RCC_OscInitStruct.PLL2.PLLP1 = 1;
  RCC_OscInitStruct.PLL2.PLLP2 = 1;
  
  // PLL3: 48 x 150 / 8 = 900MHz
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL3.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL3.PLLM = 8;
  RCC_OscInitStruct.PLL3.PLLN = 150;
  RCC_OscInitStruct.PLL3.PLLFractional = 0;
  RCC_OscInitStruct.PLL3.PLLP1 = 1;
  RCC_OscInitStruct.PLL3.PLLP2 = 1;
  
  // PLL4: 48 x 80 / 48= 80MHz
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL4.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL4.PLLM = 48;
  RCC_OscInitStruct.PLL4.PLLFractional = 0;
  RCC_OscInitStruct.PLL4.PLLN = 80;
  RCC_OscInitStruct.PLL4.PLLP1 = 1;
  RCC_OscInitStruct.PLL4.PLLP2 = 1;
  
  
  
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    /* Initialization Error */
    while (1)
      ;
  }
  
  /* configure the HCLK, PCLK1, PCLK2, PCLK4 and PCLK5 clocks dividers */
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                   RCC_CLOCKTYPE_PCLK4 | RCC_CLOCKTYPE_PCLK5);
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 2;
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 2; 
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 2;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    /* Initialization Error */
    while (1)
      ;
  }
  
  /* clock initialization for XSPI. Taken from BSP example */
  /*  Select IC3 clock from PLL1 at 200MHz (800/4) as XSPI2 source */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_XSPI2;
  PeriphClkInit.Xspi2ClockSelection = RCC_XSPI2CLKSOURCE_IC3;
  PeriphClkInit.ICSelection[RCC_IC3].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  PeriphClkInit.ICSelection[RCC_IC3].ClockDivider = 6; 
  
  /* Configure DCMIPP ck_ker_dcmipp to ic17 with PLL2 (1000MHz) / 3 = 333MHz */
  PeriphClkInit.PeriphClockSelection |= RCC_PERIPHCLK_DCMIPP;
  PeriphClkInit.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  PeriphClkInit.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
  PeriphClkInit.ICSelection[RCC_IC17].ClockDivider = 3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    while (1);
  }
  
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  PeriphClkInit.LtdcClockSelection = RCC_LTDCCLKSOURCE_IC16;
  PeriphClkInit.ICSelection[RCC_IC16].ClockSelection = RCC_ICCLKSOURCE_PLL4;
  PeriphClkInit.ICSelection[RCC_IC16].ClockDivider = 2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    while (1);
  }
  
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SDMMC2;
  PeriphClkInit.Sdmmc2ClockSelection = RCC_SDMMC2CLKSOURCE_IC4;
  PeriphClkInit.ICSelection[RCC_IC4].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  PeriphClkInit.ICSelection[RCC_IC4].ClockDivider = 4;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    while (1);
  }
}

HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp){return HAL_OK;}

void BSP_CAMERA_FrameEventMainPipeCallback(uint32_t instance)
{
  //LCD_display_stuff();
  //LCD_display_cam_image();
  //BSP_LCD_FillRGBRect(0,0,0,(uint8_t *)pipe_buffer0,320,240 );
  buffer_idx = 1-buffer_idx;
  buf_index_changed = 1;
}
HAL_StatusTypeDef  MX_DCMIPP_Init(DCMIPP_HandleTypeDef *hdcmipp, HAL_DCMIPP_PipeTypeDef pipe){
  DCMIPP_ConfPipeTypeDef pipe_conf;
  
  
  hdcmipp->Instance = DCMIPP;
  hdcmipp->Init.DCMIControl.Format           = DCMIPP_FORMAT_RGB565;
  hdcmipp->Init.DCMIControl.VPolarity        = DCMIPP_VSPOLARITY_HIGH;
  hdcmipp->Init.DCMIControl.HPolarity        = DCMIPP_HSPOLARITY_HIGH;
  hdcmipp->Init.DCMIControl.PCKPolarity      = DCMIPP_PCKPOLARITY_RISING;
  hdcmipp->Init.DCMIControl.ExtendedDataMode = DCMIPP_INTERFACE_CSI;
  hdcmipp->Init.DCMIControl.EmbeddedSynchro  = 0;
  hdcmipp->Init.DCMIControl.SwapRB           = 0;
  hdcmipp->Init.DCMIControl.SwapCycles       = DCMIPP_PARALLEL_IF_SWAPCYCLES;
  hdcmipp->Init.DCMIControl.SwapBits         = 0;
  if (HAL_DCMIPP_Init(hdcmipp) != HAL_OK)
  {
    while (1);
  }


  pipe_conf.FrameRate = DCMIPP_PIXEL_FRAME_RATE_ALL;
  pipe_conf.PipeSrc = HAL_DCMIPP_PIPE_SRC_INDEPENDANT;
  pipe_conf.PixelPacker.pDestinationMemory0 = (uint32_t *) (0x34300000);
  pipe_conf.PixelPacker.pDestinationMemory1 = NULL;
  pipe_conf.PixelPacker.pDestinationMemory2 = NULL;
  pipe_conf.PixelPacker.Pitch = 800 * 2;
  pipe_conf.PixelPacker.Format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
  pipe_conf.PixelPacker.MultiLine = DCMIPP_LINEMULT_1_LINE; /* Event after line */
  pipe_conf.PixelPacker.SwapRB = 0;
  pipe_conf.PixelPacker.HeaderEN = 0;
  pipe_conf.VcId = (0x00 << DCMIPP_P0FSCR_VC_Pos); /* CSI Virtual Channel 0 */
  pipe_conf.VcDtMode = (0 << DCMIPP_P0FSCR_DTMODE_Pos); /* CSI Flow mode selection */
  pipe_conf.VcDtIdA = (0x22 << DCMIPP_P0FSCR_DTIDA_Pos); /* CSI Data type selection ID A 0x22: RGB565 */
  pipe_conf.VcDtIdB = 0;
  int ret = HAL_DCMIPP_ConfigPipe(hdcmipp, &pipe_conf, HAL_DCMIPP_MAIN_PIPE);
  if(ret != HAL_OK){
     TRACE_MAIN("error creating DCMIPP pipe\n");
      return -1;
  }
  DCMIPP->P1PPM0AR1 = 0x34300000;
  //  DCMIPP->P1PPM0AR2 = (uint32_t) pipe_buffer[1];
  //  DCMIPP->P1PPCR |= DCMIPP_P0PPCR_DBM;
  return 0;
}

void Error_Handler(void)
{ 
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT

/**
* @brief  Reports the name of the source file and the source line number
*         where the assert_param error has occurred.
* @param  file  pointer to the source file name
* @param  line  assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t *file, uint32_t line)
{
  
  TRACE_MAIN("assert failed at line %d of file %s\n", line, file);
  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/* dump a buffer in console for debug purposes*/
void hexdump(uint8_t * buf, size_t len, char * msg){
  TRACE_MAIN("%s : ", msg);
  for(int i = 0; i<len; i++){
    if(!(i%32)){
      TRACE_MAIN("\n");
    }
    TRACE_MAIN("%02X ", buf[i]);
    
  }
  TRACE_MAIN("\n");
}

void BSP_SD_WriteCpltCallback(uint32_t Instance){
}


