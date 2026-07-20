#include "Timer.h"
#include "Timer_Private.h"
#include "std_types.h"
#include "Bit_Operations.h"
#include "Nvic.h"

// address mapping
static uint32 timer_base_addresses[NUM_OF_TIMERS] = {TIM2_BASE_ADDR, TIM3_BASE_ADDR, TIM4_BASE_ADDR, TIM5_BASE_ADDR};
#define TIMER_GET_PERIPHERAL(TimerId)   ((TimerType *) timer_base_addresses[TimerId - TIMER_2]) 

/* NVIC IRQ numbers:  TIM2=28, TIM3=29, TIM4=30, TIM5=50 */
static uint8 Timer_NvicIrq[NUM_OF_TIMERS] = {28, 29, 30, 50};

static Timer_Callback Timer_Callbacks[NUM_OF_TIMERS] = {0};

void Timer_Init(uint8 TimerId, uint16 Prescaler, uint16 AutoReload) {
    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    timer ->CR1 = 0; //reset timer (control register)
    timer ->PSC = Prescaler;
    timer ->ARR = AutoReload;
    timer ->CNT = 0; // counter
    SET_BIT(timer -> EGR, EGR_UG); // Force update to load PSC & ARR (event generation register)
    timer ->SR = 0; // Clear the update flag that UG set (status register)
}

void Timer_Start(uint8 TimerId) {
    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    SET_BIT(timer ->CR1, CR1_CEN);
}

void Timer_Stop(uint8 TimerId) {
    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    CLEAR_BIT(timer ->CR1, CR1_CEN);
}

/**
 *  Synchronous delay — blocks until timer overflows.
 *  TIMER_CLOCK_HZ / (PSC+1 = TIMER_CLOCK_HZ/1000) = 1 kHz -> 1 tick = 1 ms
 *  (PSC/ARR math driven by the single TIMER_CLOCK_HZ constant in
 *  Timer_Private.h - verify that constant matches your real board's
 *  APB1 timer clock, see the comment there.)
 *
 *  NOTE: this previously had a software "safety timeout" that aborted
 *  the wait after a fixed 1,000,000 loop iterations. That's removed:
 *  1,000,000 iterations of a tight polling loop finishes in well under
 *  a typical DS18B20 750ms conversion wait at any realistic STM32F4
 *  clock speed, so it was silently cutting every multi-hundred-ms delay
 *  short with no error indication - almost certainly the cause of the
 *  real-hardware failures. A hardware-timed blocking wait like this
 *  should be trusted to actually reach UIF; if you want a genuine
 *  hang-guard later (e.g. against a forgotten Rcc_Enable), it needs to
 *  be based on real elapsed time (e.g. a DWT cycle-counter check), not
 *  a guessed iteration count.
 */
void Timer_DelayMs(uint8 TimerId, uint32 DelayMs) {
    if (DelayMs == 0U) {
        return;
    }

    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    timer->CR1 = 0; // Stop & reset

    timer->PSC = (uint16) ((TIMER_CLOCK_HZ / 1000UL) - 1UL);
    timer->ARR = (uint16) (DelayMs - 1U); // ARR+1 ticks occur per period
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode
    SET_BIT(timer->CR1, CR1_CEN); // Start counting

    while (!READ_BIT(timer->SR, SR_UIF)) {
        // Poll – CPU is blocked here
    }

    timer->SR = 0; // Clear UIF
    CLEAR_BIT(timer->CR1, CR1_CEN); // Stop counter
}


// Async means it doesn't freeze the program until delay is finished
void Timer_DelayMsAsync(uint8 TimerId, uint32 DelayMs, Timer_Callback Callback) {
    if (DelayMs == 0U) {
        return;
    }

    TimerType *timer = TIMER_GET_PERIPHERAL(TimerId);
    uint8 index = TimerId - TIMER_2;
    uint8 irqNum = Timer_NvicIrq[index];

    Timer_Callbacks[index] = Callback;

    timer->CR1 = 0; // Stop & reset
    timer->PSC = (uint16) ((TIMER_CLOCK_HZ / 1000UL) - 1UL);
    timer->ARR = (uint16) (DelayMs - 1U);
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode

    SET_BIT(timer->DIER, DIER_UIE); // Enable update interrupt
    Nvic_EnableIrq(irqNum); // Enable interrupt in NVIC

    SET_BIT(timer->CR1, CR1_CEN); // Start counting
}

/**
 *  Synchronous delay — blocks until timer overflows.
 *  TIMER_CLOCK_HZ / (PSC+1 = TIMER_CLOCK_HZ/1000000) = 1 MHz -> 1 tick = 1 us
 */
void Timer_DelayUs(uint8 TimerId, uint32 DelayUs) {
    if (DelayUs == 0U) {
        return;
    }

    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    timer->CR1 = 0; // Stop & reset

    timer->PSC = (uint16) ((TIMER_CLOCK_HZ / 1000000UL) - 1UL);
    timer->ARR = (uint16) (DelayUs - 1U);
    timer->CNT = 0;

    /* Direct writes instead of SET_BIT (which is read-modify-write): CR1
     * was just zeroed above and EGR reads back as 0, so there's nothing
     * to preserve - this removes 3 unnecessary register reads from the
     * hot path. Matters here specifically because this setup overhead is
     * roughly constant regardless of the requested delay, and DS18B20's
     * read-slot timing only has a few microseconds of margin to spare. */
    timer->EGR = (1UL << EGR_UG);
    timer->SR = 0;

    timer->CR1 = (1UL << CR1_OPM) | (1UL << CR1_CEN);

    while (!READ_BIT(timer->SR, SR_UIF)) {
        // Poll – CPU is blocked here waiting for exact microseconds
    }

    timer->SR = 0;
    CLEAR_BIT(timer->CR1, CR1_CEN);
}

static void Timer_HandleIrq(uint8 index) {
    TimerType *timer = (TimerType *)timer_base_addresses[index];

    if (READ_BIT(timer->SR, SR_UIF)) {
        timer->SR = 0; // Clear UIF
        CLEAR_BIT(timer->DIER, DIER_UIE); // Disable further IRQs
        CLEAR_BIT(timer->CR1, CR1_CEN); // Stop counter

        if (Timer_Callbacks[index] != 0) {
            Timer_Callbacks[index]();
        }
    }
}

void TIM2_IRQHandler(void) {
    Timer_HandleIrq(0);
}
void TIM3_IRQHandler(void) {
    Timer_HandleIrq(1);
}
void TIM4_IRQHandler(void) {
    Timer_HandleIrq(2);
}
void TIM5_IRQHandler(void) {
    Timer_HandleIrq(3);
}
