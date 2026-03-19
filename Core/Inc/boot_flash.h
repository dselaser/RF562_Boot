/*===========================================================================*\
 *  boot_flash.h - STM32H562 Flash Operations for Bootloader
\*===========================================================================*/
#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>
#include "stm32h5xx_hal.h"

/**
 * @brief  Erase flash sectors
 * @param  start_sector  First sector number to erase
 * @param  num_sectors   Number of sectors to erase
 * @return HAL_OK on success
 */
HAL_StatusTypeDef Boot_Flash_EraseSectors(uint32_t start_sector, uint32_t num_sectors);

/**
 * @brief  Erase application area (enough sectors for given firmware size)
 * @param  fw_size  Firmware size in bytes
 * @return HAL_OK on success
 */
HAL_StatusTypeDef Boot_Flash_EraseApp(uint32_t fw_size);

/**
 * @brief  Write a block of data to flash (handles quadword alignment)
 * @param  addr  Destination flash address (must be 16-byte aligned)
 * @param  data  Source data buffer (must be 4-byte aligned)
 * @param  len   Number of bytes to write
 * @return HAL_OK on success
 * @note   Remaining bytes (< 16) are padded with 0xFF
 */
HAL_StatusTypeDef Boot_Flash_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief  Verify flash contents against buffer
 * @param  addr  Flash address to verify
 * @param  data  Expected data
 * @param  len   Length to compare
 * @return Number of matching bytes (== len means all match)
 */
uint32_t Boot_Flash_Verify(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief  Check if application at APP_FLASH_BASE is valid
 * @return 1 if valid (SP in RAM range, PC in Flash range), 0 otherwise
 */
int Boot_Flash_IsAppValid(void);

#endif /* BOOT_FLASH_H */
