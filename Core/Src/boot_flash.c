/*===========================================================================*\
 *  boot_flash.c - STM32H562 Flash Operations for Bootloader
 *
 *  STM32H562 Flash Characteristics:
 *    - 1MB total, dual bank (Bank1: 512KB, Bank2: 512KB)
 *    - 128 sectors x 8KB each (64 per bank)
 *    - Programming unit: Quadword (128-bit = 16 bytes)
 *    - Erase: per sector (8KB)
\*===========================================================================*/

#include <string.h>
#include "boot_flash.h"
#include "boot_config.h"

/* ---- ICACHE helpers (STM32H5 caches all flash reads) ---- */
static void icache_disable(void)
{
    if (ICACHE->CR & ICACHE_CR_EN) {
        ICACHE->CR &= ~ICACHE_CR_EN;
        while (ICACHE->SR & ICACHE_SR_BUSYF) {}
    }
}

static void icache_invalidate(void)
{
    /* Full invalidate — required after flash write so verify reads real data */
    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while (ICACHE->SR & ICACHE_SR_BUSYF) {}
}

HAL_StatusTypeDef
Boot_Flash_EraseSectors(uint32_t start_sector, uint32_t num_sectors)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0;
    HAL_StatusTypeDef ret;
    uint32_t end_sector = start_sector + num_sectors - 1;

    /* STM32H562 dual-bank: Bank1 = sectors 0-63, Bank2 = sectors 64-127 */
    #define SECTORS_PER_BANK  (FLASH_BANK_SIZE / FLASH_SECTOR_SIZE)  /* 64 */

    /* Clamp to valid range */
    if (end_sector > APP_SECTOR_END) {
        end_sector = APP_SECTOR_END;
        num_sectors = end_sector - start_sector + 1;
    }

    /* ICACHE must be disabled before flash erase on STM32H5 */
    icache_disable();

    ret = HAL_FLASH_Unlock();
    if (ret != HAL_OK) return ret;

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    /* ---- Erase Bank 1 portion (sectors 0-63) ---- */
    if (start_sector < SECTORS_PER_BANK) {
        uint32_t bank1_end = (end_sector < SECTORS_PER_BANK) ? end_sector : (SECTORS_PER_BANK - 1);

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks     = FLASH_BANK_1;
        erase.Sector    = start_sector;
        erase.NbSectors = bank1_end - start_sector + 1;

        ret = HAL_FLASHEx_Erase(&erase, &sector_error);
        if (ret != HAL_OK) {
            HAL_FLASH_Lock();
            return ret;
        }
    }

    /* ---- Erase Bank 2 portion (sectors 64-127) ---- */
    if (end_sector >= SECTORS_PER_BANK) {
        uint32_t bank2_start = (start_sector >= SECTORS_PER_BANK) ? start_sector : SECTORS_PER_BANK;

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Banks     = FLASH_BANK_2;
        erase.Sector    = bank2_start - SECTORS_PER_BANK;  /* Per-bank sector number */
        erase.NbSectors = end_sector - bank2_start + 1;

        ret = HAL_FLASHEx_Erase(&erase, &sector_error);
        if (ret != HAL_OK) {
            HAL_FLASH_Lock();
            return ret;
        }
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}


HAL_StatusTypeDef
Boot_Flash_EraseApp(uint32_t fw_size)
{
    uint32_t max_sectors = APP_SECTOR_END - APP_SECTOR_START + 1;  /* 124 */
    uint32_t num_sectors;

    /* Calculate number of sectors needed */
    num_sectors = (fw_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    if (num_sectors > max_sectors) {
        num_sectors = max_sectors;
    }
    /* Erase at least 1 sector */
    if (num_sectors == 0) num_sectors = 1;

    return Boot_Flash_EraseSectors(APP_SECTOR_START, num_sectors);
}


HAL_StatusTypeDef
Boot_Flash_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef ret;
    uint32_t i;
    uint8_t pad_buf[FLASH_QUADWORD_SIZE] __attribute__((aligned(4)));

    if (len == 0) return HAL_OK;

    /* ICACHE must stay disabled during flash programming */
    icache_disable();

    ret = HAL_FLASH_Unlock();
    if (ret != HAL_OK) return ret;

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    /* Write full quadwords (16-byte chunks) */
    for (i = 0; (i + FLASH_QUADWORD_SIZE) <= len; i += FLASH_QUADWORD_SIZE) {
        /* Copy to aligned buffer to guarantee 4-byte alignment */
        memcpy(pad_buf, data + i, FLASH_QUADWORD_SIZE);
        ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                addr + i, (uint32_t)pad_buf);
        if (ret != HAL_OK) {
            HAL_FLASH_Lock();
            return ret;
        }
    }

    /* Handle remaining bytes (< 16), pad with 0xFF */
    if (i < len) {
        memset(pad_buf, 0xFF, FLASH_QUADWORD_SIZE);
        memcpy(pad_buf, data + i, len - i);
        ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                addr + i, (uint32_t)pad_buf);
        if (ret != HAL_OK) {
            HAL_FLASH_Lock();
            return ret;
        }
    }

    HAL_FLASH_Lock();

    /* Invalidate ICACHE so subsequent verify reads real flash data */
    icache_invalidate();

    return HAL_OK;
}


uint32_t
Boot_Flash_Verify(uint32_t addr, const uint8_t *data, uint32_t len)
{
    volatile uint8_t *flash_ptr = (volatile uint8_t *)addr;
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (flash_ptr[i] != data[i]) {
            return i;   /* Mismatch at offset i */
        }
    }
    return len;  /* All bytes match */
}


int
Boot_Flash_IsAppValid(void)
{
    uint32_t app_sp = *(volatile uint32_t *)(APP_FLASH_BASE);
    uint32_t app_pc = *(volatile uint32_t *)(APP_FLASH_BASE + 4);

    /* Stack pointer must be within RAM range */
    if (app_sp < RAM_BASE || app_sp > RAM_END) {
        return 0;
    }
    /* Reset handler must be within application flash range */
    if (app_pc < APP_FLASH_BASE || app_pc > (APP_FLASH_BASE + APP_FLASH_SIZE)) {
        return 0;
    }
    /* Additional: SP must be word-aligned */
    if (app_sp & 0x3) {
        return 0;
    }

    return 1;
}
