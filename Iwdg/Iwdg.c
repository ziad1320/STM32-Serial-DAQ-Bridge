#include "IWDG.h"
#include "IWDG_Private.h"

void IWDG_Init(void) {
    IWDG->KR = 0x5555U; /* Unlock PR/RLR for writing */
    IWDG->PR = 6U;      /* /256 prescaler: ~32kHz LSI / 256 ~= 8ms per tick */

    while (IWDG->SR & (1UL << IWDG_SR_PVU)) {
        /* Wait for the prescaler write to sync into the LSI clock domain */
    }

    IWDG->RLR = 1000U;  /* 1000 ticks * ~8ms ~= 8 second timeout */

    while (IWDG->SR & (1UL << IWDG_SR_RVU)) {
        /* Wait for the reload write to sync too */
    }

    IWDG->KR = 0xAAAAU; /* Reload the counter with RLR now */
    IWDG->KR = 0xCCCCU; /* Start the watchdog - counting begins now */
}

void IWDG_Refresh(void) {
    IWDG->KR = 0xAAAAU;
}