#include "Timer.h"
#include "Timer_Private.h"
#include "../lib/std_types.h"
#include "../lib/Bit_Operations.h"
#include "../Nvic/Nvic.h"

// address mapping
static uint32 timer_base_addresses[NUM_OF_TIMERS] = {TIM2_BASE_ADDR, TIM3_BASE_ADDR, TIM4_BASE_ADDR, TIM5_BASE_ADDR};
#define TIMER_GET_PERIPHERAL(TimerId)   ((TimerType *) timer_base_addresses[TimerId - TIMER_2]) 

/* NVIC IRQ numbers:  TIM2=28, TIM3=29, TIM4=30, TIM5=50 */
static uint8 Timer_NvicIrq[NUM_OF_TIMERS] = {28, 29, 30, 50};

static Timer_Callback Timer_Callbacks[NUM_OF_TIMERS] = {0};

/* Safety timeout limit for polling loops */
#define TIMER_TIMEOUT_LIMIT 1000000UL

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
 *  Synchronous delay — blocks until timer overflows
 *  16 MHz HSI  ÷  (PSC+1 = 16000) = 1 kHz  →  1 tick = 1 ms
 */
void Timer_DelayMs(uint8 TimerId, uint32 DelayMs) {
    if (DelayMs == 0) return;
    
    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    timer->CR1 = 0; // Stop & reset
    
    /* Hardware Clock: 16MHz HSI. 
     * To get 1ms resolution: 16,000,000 / 16,000 = 1,000 Hz.
     * PSC = 16000 - 1 = 15999. */
    timer->PSC = 15999U; 
    timer->ARR = (uint16) (DelayMs - 1); 
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode
    SET_BIT(timer->CR1, CR1_CEN); // Start counting

    uint32 timeout = TIMER_TIMEOUT_LIMIT;
    while (!READ_BIT(timer->SR, SR_UIF) && (timeout > 0)) {
        timeout--;
    }

    timer->SR = 0; // Clear UIF
    CLEAR_BIT(timer->CR1, CR1_CEN); // Stop counter
}


// Async means it doesn't freeze the program until delay is finished
void Timer_DelayMsAsync(uint8 TimerId, uint32 DelayMs, Timer_Callback Callback) {
    if (DelayMs == 0) return;

    TimerType *timer = TIMER_GET_PERIPHERAL(TimerId);
    uint8 index = TimerId - TIMER_2;
    uint8 irqNum = Timer_NvicIrq[index]; 

    Timer_Callbacks[index] = Callback; 

    timer->CR1 = 0; // Stop & reset
    timer->PSC = 15999U;
    timer->ARR = (uint16) (DelayMs - 1);
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG); // Load shadow registers
    timer->SR = 0; // Clear UIF caused by UG

    SET_BIT(timer->CR1, CR1_OPM); // One-pulse mode

    SET_BIT(timer->DIER, DIER_UIE); // Enable update interrupt
    Nvic_EnableIrq(irqNum); // Enable interrupt in NVIC

    SET_BIT(timer->CR1, CR1_CEN); // Start counting
}

/**
 *  Synchronous delay — blocks until timer overflows
 *  16 MHz HSI  ÷  (PSC+1 = 16) = 1 MHz  →  1 tick = 1 us
 */
void Timer_DelayUs(uint8 TimerId, uint32 DelayUs) {
    if (DelayUs == 0) return;

    TimerType* timer = TIMER_GET_PERIPHERAL(TimerId);
    timer->CR1 = 0; // Stop & reset

    /* Strict 16MHz to 1MHz conversion. PSC = 16 - 1 = 15. */
    timer->PSC = 15U;

    timer->ARR = (uint16) (DelayUs - 1);
    timer->CNT = 0;

    SET_BIT(timer->EGR, EGR_UG);
    timer->SR = 0;

    SET_BIT(timer->CR1, CR1_OPM);
    SET_BIT(timer->CR1, CR1_CEN);

    uint32 timeout = TIMER_TIMEOUT_LIMIT;
    while (!READ_BIT(timer->SR, SR_UIF) && (timeout > 0)) {
        timeout--;
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
