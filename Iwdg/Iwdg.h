#ifndef IWDG_H
#define IWDG_H

#include "std_types.h"

/**
 * @brief Starts the independent watchdog with a fixed ~8 second timeout.
 *
 * Clocked by the internal LSI oscillator, which starts automatically the
 * moment IWDG is started - no Rcc_Enable() needed, it isn't gated by
 * APB1ENR/AHB1ENR like the other peripherals in this project.
 *
 * IMPORTANT: once started, the IWDG cannot be stopped or reconfigured by
 * software - by hardware design, so a runaway/corrupted program can't
 * disable its own safety net. Call IWDG_Refresh() regularly (well under
 * 8s apart) after this, or the MCU resets.
 */
void IWDG_Init(void);

/**
 * @brief "Kicks" the watchdog, resetting its countdown back to ~8 seconds.
 * Call this only from places confirmed to be reached regularly during
 * normal operation - never from inside the exact loop you're trying to
 * guard against hanging (that would defeat the whole point).
 */
void IWDG_Refresh(void);

#endif