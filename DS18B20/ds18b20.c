#include "Ds18b20.h"
#include "../Timer/Timer.h"

#define DS_TIMER TIMER_2 /* Make sure this matches the timer enabled in main.c */

/* --- Dynamic Pin Switching Helpers --- */
static void DS18B20_SetOutput(uint8 PortName, uint8 PinNumber) {
    /* Drive the pin actively using standard push-pull[cite: 1, 2] */
    Gpio_Init(PortName, PinNumber, GPIO_OUTPUT, GPIO_PUSH_PULL);
}

static void DS18B20_SetInput(uint8 PortName, uint8 PinNumber) {
    /* Become passive so the Proteus external 4.7k resistor works[cite: 1, 2] */
    Gpio_Init(PortName, PinNumber, GPIO_INPUT, GPIO_NO_PULL_DOWN);
}
/* ------------------------------------- */

void DS18B20_Init_Pin(uint8 PortName, uint8 PinNumber) {
    /* Initialize passively to prevent bus contention */
    DS18B20_SetInput(PortName, PinNumber);
}

uint8 DS18B20_Reset(uint8 PortName, uint8 PinNumber) {
    uint8 presence = 0;

    /* Master pulls bus low for 480us */
    DS18B20_SetOutput(PortName, PinNumber);
    Gpio_WritePin(PortName, PinNumber, LOW); /*[cite: 1, 2] */
    Timer_DelayUs(DS_TIMER, 480);

    /* Master releases bus and waits for sensor */
    DS18B20_SetInput(PortName, PinNumber);
    Timer_DelayUs(DS_TIMER, 80);

    /* Read pin state using your modular driver */
    presence = Gpio_ReadPin(PortName, PinNumber); /*[cite: 1, 2] */

    /* Wait for the rest of the timeslot */
    Timer_DelayUs(DS_TIMER, 400);

    return (presence == 0) ? 1 : 0;
}

void DS18B20_WriteByte(uint8 PortName, uint8 PinNumber, uint8 data) {
    for (uint8 i = 0; i < 8; i++) {
        if (data & (1 << i)) {
            /* Write 1 */
            DS18B20_SetOutput(PortName, PinNumber);
            Gpio_WritePin(PortName, PinNumber, LOW); /*[cite: 1, 2] */
            Timer_DelayUs(DS_TIMER, 1);

            DS18B20_SetInput(PortName, PinNumber); /* Release */
            Timer_DelayUs(DS_TIMER, 60);
        } else {
            /* Write 0 */
            DS18B20_SetOutput(PortName, PinNumber);
            Gpio_WritePin(PortName, PinNumber, LOW); /*[cite: 1, 2] */
            Timer_DelayUs(DS_TIMER, 60);

            DS18B20_SetInput(PortName, PinNumber); /* Release */
            Timer_DelayUs(DS_TIMER, 1);
        }
    }
}

uint8 DS18B20_ReadByte(uint8 PortName, uint8 PinNumber) {
    uint8 data = 0;
    for (uint8 i = 0; i < 8; i++) {
        DS18B20_SetOutput(PortName, PinNumber);
        Gpio_WritePin(PortName, PinNumber, LOW); /*[cite: 1, 2] */
        Timer_DelayUs(DS_TIMER, 2);

        DS18B20_SetInput(PortName, PinNumber); /* Release */
        Timer_DelayUs(DS_TIMER, 10);

        if (Gpio_ReadPin(PortName, PinNumber) == HIGH) { /*[cite: 1, 2] */
            data |= (1 << i);
        }
        Timer_DelayUs(DS_TIMER, 50);
    }
    return data;
}

float DS18B20_ReadTemperature(uint8 PortName, uint8 PinNumber) {
    uint8 temp_LSB = 0;
    uint8 temp_MSB = 0;
    uint16 raw_temp = 0;

    if (!DS18B20_Reset(PortName, PinNumber)) return -999.0;
    DS18B20_WriteByte(PortName, PinNumber, 0xCC); /* Skip ROM */
    DS18B20_WriteByte(PortName, PinNumber, 0x44); /* Convert T */

    Timer_DelayMs(DS_TIMER, 750);

    if (!DS18B20_Reset(PortName, PinNumber)) return -999.0;
    DS18B20_WriteByte(PortName, PinNumber, 0xCC); /* Skip ROM */
    DS18B20_WriteByte(PortName, PinNumber, 0xBE); /* Read Scratchpad */

    temp_LSB = DS18B20_ReadByte(PortName, PinNumber);
    temp_MSB = DS18B20_ReadByte(PortName, PinNumber);

    raw_temp = (temp_MSB << 8) | temp_LSB;
    return (float)raw_temp / 16.0;
}