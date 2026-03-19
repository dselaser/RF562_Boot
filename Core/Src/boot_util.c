/*===========================================================================*\
 *  boot_util.c - Bootloader Utility Functions
 *
 *  LED control, TAMP update flag, jump-to-app
\*===========================================================================*/

#include "boot_util.h"
#include "boot_config.h"
#include "stm32h5xx_hal.h"


/*===========================================================================*\
 *  LED (PB0)
\*===========================================================================*/

void
Boot_LED_Init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gi.Pin   = LED_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gi);
    LED_OFF();
}

void
Boot_LED_Blink(int count, uint32_t ms)
{
    for (int i = 0; i < count; i++) {
        LED_ON();
        HAL_Delay(ms);
        LED_OFF();
        HAL_Delay(ms);
    }
}


/*===========================================================================*\
 *  TAMP Backup Register - Update Flag
 *
 *  Application writes UPDATE_MAGIC (0xDEADBEEF) to TAMP->BKP0R
 *  then soft-resets.  Bootloader checks this on startup.
\*===========================================================================*/

int
Boot_CheckUpdateFlag(void)
{
    return (TAMP->BKP0R == UPDATE_MAGIC) ? 1 : 0;
}

void
Boot_ClearUpdateFlag(void)
{
    /* STM32H5: PWR 클럭은 항상 활성화 상태 - 별도 enable 불필요 */
    HAL_PWR_EnableBkUpAccess();
    TAMP->BKP0R = 0;
    HAL_PWR_DisableBkUpAccess();
}


/*===========================================================================*\
 *  Jump to Application
 *
 *  1. Deinitialize all peripherals & clocks
 *  2. Disable SysTick & interrupts
 *  3. Set VTOR to app vector table
 *  4. Load app SP, jump to app Reset_Handler
\*===========================================================================*/

void
Boot_JumpToApp(void)
{
    uint32_t app_sp = *(volatile uint32_t *)(APP_FLASH_BASE);
    uint32_t app_pc = *(volatile uint32_t *)(APP_FLASH_BASE + 4);

    /* 1. ICACHE 비활성화 (STM32H5 필수 - SysTick 불필요) */
    if (ICACHE->CR & ICACHE_CR_EN) {
        ICACHE->CR &= ~ICACHE_CR_EN;
        while (ICACHE->SR & ICACHE_SR_BUSYF) {}
    }

    /* 2. HAL & 클럭 디이니셜라이즈 (SysTick 필요 → 먼저 실행) */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 3. SysTick 중지 (HAL DeInit 완료 후) */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    /* 4. 모든 인터럽트 비활성화 */
    __disable_irq();

    /* 5. 모든 NVIC IRQ 비활성화 + 펜딩 클리어 */
    for (uint32_t i = 0; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* 6. 벡터 테이블을 앱으로 재배치 */
    SCB->VTOR = APP_FLASH_BASE;

    /* 7. MSP 설정 & 점프 (인라인 어셈블리 - 스택 접근 회피) */
    /*    __set_MSP() 후 로컬변수(스택)를 읽으면 엉뚱한 값이 됨.
     *    app_sp, app_pc를 레지스터에 넣고 어셈블리로 직접 점프. */
    __asm volatile (
        "MSR MSP, %0  \n"  /* 앱의 SP 설정 */
        "DSB          \n"
        "ISB          \n"
        "CPSIE I      \n"  /* 인터럽트 재활성화 */
        "BX %1        \n"  /* 앱의 Reset_Handler로 점프 */
        : /* no outputs */
        : "r" (app_sp), "r" (app_pc)
        : "memory"
    );

    /* Should never reach here */
}
