/*===========================================================================*\
 *  boot_util.h - Bootloader Utility Functions
 *
 *  LED control, update flag, jump-to-app
\*===========================================================================*/
#ifndef BOOT_UTIL_H
#define BOOT_UTIL_H

#include <stdint.h>

/**
 * @brief  Initialize LED GPIO (PB0 push-pull output)
 */
void Boot_LED_Init(void);

/**
 * @brief  Blink LED count times (on ms, off ms)
 * @param  count  Number of blinks
 * @param  ms     On/Off duration in milliseconds
 */
void Boot_LED_Blink(int count, uint32_t ms);

/**
 * @brief  Check if app requested firmware update via TAMP backup register
 * @return 1 if UPDATE_MAGIC found, 0 otherwise
 */
int Boot_CheckUpdateFlag(void);

/**
 * @brief  Clear the update request flag in TAMP backup register
 */
void Boot_ClearUpdateFlag(void);

/**
 * @brief  Deinitialize peripherals and jump to application at APP_FLASH_BASE
 * @note   Does not return on success
 */
void Boot_JumpToApp(void);

#endif /* BOOT_UTIL_H */
