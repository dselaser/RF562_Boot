/*===========================================================================*\
 *  boot_config.h - RF562 Handpiece Bootloader Configuration
 *
 *  STM32H562RGTx Custom Bootloader for RS485 Firmware Update
 *  Copyright (c) 2024, connexthings@naver.com
\*===========================================================================*/
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

/*===========================================================================*\
 *  Flash Memory Layout (STM32H562RG, 1MB, 8KB sectors)
 *
 *  Sector  0-3  : 0x0800_0000 ~ 0x0800_7FFF  (32KB)  Bootloader
 *  Sector  4-127: 0x0800_8000 ~ 0x080F_FFFF  (992KB) Application
\*===========================================================================*/
#define BOOT_FLASH_BASE         0x08000000U
#define BOOT_FLASH_SIZE         0x00008000U     /* 32KB  */
#define APP_FLASH_BASE          0x08008000U
#define APP_FLASH_SIZE          0x000F8000U     /* 992KB */

#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE       0x00002000U     /* 8KB per sector */
#endif
#define BOOT_SECTOR_START       0U
#define BOOT_SECTOR_COUNT       4U
#define APP_SECTOR_START        4U
#define APP_SECTOR_END          127U

/* STM32H5 quadword programming unit */
#define FLASH_QUADWORD_SIZE     16U             /* 128-bit = 16 bytes */

/*===========================================================================*\
 *  UART2 RS485 Configuration
 *  PA1 = USART2_DE  (Driver Enable, hardware controlled)
 *  PA2 = USART2_TX
 *  PA3 = USART2_RX
\*===========================================================================*/
#define RS485_BAUDRATE          230400U
#define RS485_GPIO_PORT         GPIOA
#define RS485_TX_PIN            GPIO_PIN_2
#define RS485_RX_PIN            GPIO_PIN_3
#define RS485_DE_PIN            GPIO_PIN_1
#define RS485_AF                GPIO_AF7_USART2

/* DE assertion/deassertion time (in 1/16th bit time units) */
#define RS485_DE_ASSERT_TIME    16U
#define RS485_DE_DEASSERT_TIME  16U

/*===========================================================================*\
 *  Timing
\*===========================================================================*/
#define BOOT_TIMEOUT_MS         3000U   /* ASCII command wait at boot     */
#define PACKET_TIMEOUT_MS       5000U   /* CNNX packet receive timeout    */
#define ERASE_WAIT_MS          10000U   /* Max wait after FWPREPARE sent  */

/*===========================================================================*\
 *  Update Request Flag (TAMP Backup Register)
 *  Application writes this magic to TAMP->BKP0R before soft-reset
 *  to request bootloader enter update mode.
\*===========================================================================*/
#define UPDATE_MAGIC            0xDEADBEEFU

/*===========================================================================*\
 *  RS485 Node IDs (CNNX Protocol)
\*===========================================================================*/
#define RS485_ID_MASTER         0x01U
#define RS485_ID_HP             0x02U

/*===========================================================================*\
 *  CNNX Binary Packet Protocol
 *
 *  Packet: HDR[2] DST[1] SRC[1] PID[2] CMD[1] NDATA[1] DATA[N] CRC16[2] TRL[2]
 *  Total : N + 12 bytes
\*===========================================================================*/
#define PKT_HDR0                0xAAU
#define PKT_HDR1                0x55U
#define PKT_TRL0                0x55U
#define PKT_TRL1                0xAAU

/* Packet field offsets */
#define PKT_OFF_DST             2U
#define PKT_OFF_SRC             3U
#define PKT_OFF_PID_H           4U
#define PKT_OFF_PID_L           5U
#define PKT_OFF_CMD             6U
#define PKT_OFF_NDATA           7U
#define PKT_OFF_DATA            8U

#define PKT_OVERHEAD            12U     /* HDR(2)+DST+SRC+PID(2)+CMD+NDATA+CRC16(2)+TRL(2) */
#define PKT_MAX_DATA            240U    /* Max data payload per packet */
#define PKT_MAX_SIZE            (PKT_MAX_DATA + PKT_OVERHEAD)  /* 252 */

/* CNNX RS485 Commands */
#define CMD_NONE                0U
#define CMD_ACK                 1U
#define CMD_FWSTATUS            2U
#define CMD_FWVERSION           3U
#define CMD_FWWAIT              4U
#define CMD_FWWAITMAX           5U
#define CMD_SUBBOOT             6U
#define CMD_FWPREPARE           7U
#define CMD_FWREADY             8U
#define CMD_FWSTART             9U
#define CMD_FWCODE              10U
#define CMD_FWNEXT              11U
#define CMD_FWEND               12U
#define CMD_FWSUCCESS           13U
#define CMD_FWFAILURE           14U
#define CMD_RESEND              15U
#define CMD_RESET               16U
#define CMD_ERROR               17U
#define CMD_FWWRERR             18U
#define CMD_FWRDERR             19U

/* FWPREPARE data layout:  codesize[4] + blockcnt[4] + fwver[2] = 10 bytes */
#define FWPREP_OFF_CODESIZE     0U
#define FWPREP_OFF_BLOCKCNT     4U
#define FWPREP_OFF_FWVER        8U

/*===========================================================================*\
 *  Status LED  (PB0 = LED_PIN_ON on RF562)
\*===========================================================================*/
#define LED_PORT                GPIOC
#define LED_PIN                 GPIO_PIN_13

#define LED_ON()                HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET)
#define LED_OFF()               HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET)
#define LED_TOGGLE()            HAL_GPIO_TogglePin(LED_PORT, LED_PIN)

/*===========================================================================*\
 *  RAM Validity Check Range
\*===========================================================================*/
#define RAM_BASE                0x20000000U
#define RAM_END                 0x200A0000U     /* 640KB */

#endif /* BOOT_CONFIG_H */
