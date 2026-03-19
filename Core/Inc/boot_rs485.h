/*===========================================================================*\
 *  boot_rs485.h - RS485 Communication & CNNX Protocol for Bootloader
\*===========================================================================*/
#ifndef BOOT_RS485_H
#define BOOT_RS485_H

#include <stdint.h>
#include "stm32h5xx_hal.h"

/*===========================================================================*\
 *  ASCII Command Interface (Phase 1 - Boot Command Detection)
 *  Frame format: @<payload>*XX\n   (XX = XOR parity hex)
\*===========================================================================*/

/**
 * @brief  Wait for ASCII command on RS485 with timeout
 * @param  timeout_ms  Maximum wait time in milliseconds
 * @return  'B' = boot command (@B*42\n)
 *          'X' = update command (@X*58\n)
 *           0  = timeout (no valid command received)
 *          -1  = error
 */
int RS485_WaitForAsciiCmd(uint32_t timeout_ms);

/**
 * @brief  Send ASCII response frame: @<payload>*XX\n
 * @param  payload  Payload string (null-terminated)
 */
void RS485_SendAsciiResp(const char *payload);

/*===========================================================================*\
 *  CNNX Binary Protocol Interface (Phase 2 - Firmware Transfer)
\*===========================================================================*/

/**
 * @brief  Main firmware receive loop (called when update mode is entered)
 *         Handles full CNNX protocol: FWPREPARE → FWCODE → FWEND
 * @return  0 = success (firmware updated)
 *         -1 = timeout
 *         -2 = flash write error
 *         -3 = verify error
 *         -4 = protocol error
 */
int RS485_FirmwareReceive(void);

/*===========================================================================*\
 *  Low-Level RS485 UART Functions
\*===========================================================================*/

/**
 * @brief  Initialize UART2 in RS485 mode with hardware DE control
 */
void RS485_Init(void);

/**
 * @brief  Deinitialize UART2
 */
void RS485_DeInit(void);

/**
 * @brief  Receive a single byte with timeout (polling)
 * @param  byte      Pointer to store received byte
 * @param  timeout_ms  Timeout in milliseconds
 * @return  1 = byte received, 0 = timeout
 */
int RS485_RecvByte(uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief  Send data buffer via RS485 (blocking, hardware DE)
 * @param  data  Data to send
 * @param  len   Number of bytes
 */
void RS485_Send(const uint8_t *data, uint16_t len);

#endif /* BOOT_RS485_H */
