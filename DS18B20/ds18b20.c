#include "ds18b20.h"
#include "../Timer/Timer.h"

#define DS_TIMER TIMER_2 

#define DS_RESET_LOW_US        480u  /* master reset pulse,            min 480us           */
#define DS_RESET_SAMPLE_US     70u   /* wait before sampling presence, see note below       */
#define DS_RESET_RECOVER_US    410u  /* remainder of the reset slot -> 480+70+410 = 960us   */

#define DS_WRITE1_LOW_US       6u    /* write '1': low pulse,          max 15us             */
#define DS_WRITE1_RELEASE_US   64u   /* -> total write slot ~70us,     spec range 60-120us  */
#define DS_WRITE0_LOW_US       60u   /* write '0': low pulse,          min 60us             */
#define DS_WRITE0_RELEASE_US   10u   /* -> total write slot ~70us                            */

#define DS_READ_INIT_US        2u    /* read: master low pulse,        min 1us, max 15us    */
#define DS_READ_SAMPLE_US      10u   /* wait before sampling,          sample point <15us    */
#define DS_READ_RECOVER_US     50u   /* remainder of read slot ->      2+10+50 = 62us        */

/* --- Dynamic Pin Switching Helpers --- */
static void DS18B20_SetOutput(uint8 PortName, uint8 PinNumber) {
    /* Drive the pin actively using standard push-pull */
    Gpio_Init(PortName, PinNumber, GPIO_OUTPUT, GPIO_PUSH_PULL);
}

static void DS18B20_SetInput(uint8 PortName, uint8 PinNumber) {
    /* Become passive so the external 4.7k pull-up resistor works */
    Gpio_Init(PortName, PinNumber, GPIO_INPUT, GPIO_NO_PULL_DOWN);
}
/* ------------------------------------- */

/* Dallas/Maxim CRC8 (poly x^8+x^5+x^4+1, reflected form, 0x8C). Validates
 * the scratchpad so a corrupted 1-Wire transfer is rejected instead of
 * silently turned into a wrong-but-plausible-looking temperature. */
static uint8 DS18B20_CRC8(const uint8 *data, uint8 len) {
    uint8 crc = 0;
    uint8 i, j;

    for (i = 0; i < len; i++) {
        uint8 inbyte = data[i];
        for (j = 0; j < 8; j++) {
            uint8 mix = (uint8)((crc ^ inbyte) & 0x01u);
            crc >>= 1;
            if (mix) {
                crc ^= 0x8Cu;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

/* Converts the raw 16-bit two's-complement scratchpad value into degrees
 * Celsius. The old version divided the raw uint16 directly, which only
 * works for positive temperatures - anything below 0C wrapped around to a
 * large positive number instead of going negative. */
static float DS18B20_RawToCelsius(uint16 raw_temp) {
    if (raw_temp & 0x8000u) {
        uint16 magnitude = (uint16)((~raw_temp) + 1u); /* two's complement */
        return -((float)magnitude / 16.0f);
    }
    return (float)raw_temp / 16.0f;
}

void DS18B20_Init_Pin(uint8 PortName, uint8 PinNumber) {
    /* Initialize passively to prevent bus contention */
    DS18B20_SetInput(PortName, PinNumber);
}

uint8 DS18B20_Reset(uint8 PortName, uint8 PinNumber) {
    uint8 presence;

    /* Master pulls bus low for >=480us */
    DS18B20_SetOutput(PortName, PinNumber);
    Gpio_WritePin(PortName, PinNumber, LOW);
    Timer_DelayUs(DS_TIMER, DS_RESET_LOW_US);

    /* Master releases the bus and waits before sampling.
     *
     * The sensor starts its presence pulse 15-60us after release
     * (tPDHIGH) and holds it low for 60-240us (tPDLOW). The only sample
     * point guaranteed to land inside that pulse for EVERY valid part is
     * > 60us (pulse has definitely started) and < 75us (=15+60, the
     * earliest the pulse can possibly end). 70us sits in that guaranteed
     * window; the original 80us did not. */
    DS18B20_SetInput(PortName, PinNumber);
    Timer_DelayUs(DS_TIMER, DS_RESET_SAMPLE_US);

    presence = Gpio_ReadPin(PortName, PinNumber);

    /* Finish out the reset time slot */
    Timer_DelayUs(DS_TIMER, DS_RESET_RECOVER_US);

    return (presence == LOW) ? 1u : 0u;
}

void DS18B20_WriteByte(uint8 PortName, uint8 PinNumber, uint8 data) {
    for (uint8 i = 0; i < 8; i++) {
        if (data & (1u << i)) {
            /* Write 1: short low pulse, well under the 15us max */
            DS18B20_SetOutput(PortName, PinNumber);
            Gpio_WritePin(PortName, PinNumber, LOW);
            Timer_DelayUs(DS_TIMER, DS_WRITE1_LOW_US);

            DS18B20_SetInput(PortName, PinNumber); /* Release */
            Timer_DelayUs(DS_TIMER, DS_WRITE1_RELEASE_US);
        } else {
            /* Write 0: hold low for the full slot */
            DS18B20_SetOutput(PortName, PinNumber);
            Gpio_WritePin(PortName, PinNumber, LOW);
            Timer_DelayUs(DS_TIMER, DS_WRITE0_LOW_US);

            DS18B20_SetInput(PortName, PinNumber); /* Release */
            Timer_DelayUs(DS_TIMER, DS_WRITE0_RELEASE_US);
        }
    }
}

uint8 DS18B20_ReadByte(uint8 PortName, uint8 PinNumber) {
    uint8 data = 0;
    for (uint8 i = 0; i < 8; i++) {
        DS18B20_SetOutput(PortName, PinNumber);
        Gpio_WritePin(PortName, PinNumber, LOW);
        Timer_DelayUs(DS_TIMER, DS_READ_INIT_US);

        DS18B20_SetInput(PortName, PinNumber); /* Release */
        Timer_DelayUs(DS_TIMER, DS_READ_SAMPLE_US);

        if (Gpio_ReadPin(PortName, PinNumber) == HIGH) {
            data |= (uint8)(1u << i);
        }
        Timer_DelayUs(DS_TIMER, DS_READ_RECOVER_US);
    }
    return data;
}

uint8 DS18B20_IsPresent(uint8 PortName, uint8 PinNumber) {
    return DS18B20_Reset(PortName, PinNumber);
}

float DS18B20_ReadTemperature(uint8 PortName, uint8 PinNumber) {
    for (uint8 attempt = 0; attempt < DS18B20_MAX_RETRIES; attempt++) {
        uint8 scratchpad[9];
        uint16 raw_temp;
        uint8 i;

        if (!DS18B20_Reset(PortName, PinNumber)) {
            continue; /* no presence pulse - retry */
        }
        DS18B20_WriteByte(PortName, PinNumber, 0xCC); /* Skip ROM */
        DS18B20_WriteByte(PortName, PinNumber, 0x44); /* Convert T */

        /* Datasheet max conversion time at 12-bit resolution */
        Timer_DelayMs(DS_TIMER, 750);

        if (!DS18B20_Reset(PortName, PinNumber)) {
            continue;
        }
        DS18B20_WriteByte(PortName, PinNumber, 0xCC); /* Skip ROM */
        DS18B20_WriteByte(PortName, PinNumber, 0xBE); /* Read Scratchpad */

        for (i = 0; i < 9; i++) {
            scratchpad[i] = DS18B20_ReadByte(PortName, PinNumber);
        }

        /* Validate the full 8 data bytes against the scratchpad's CRC
         * byte instead of trusting whichever 2 bytes happened to come
         * back on the wire. */
        if (DS18B20_CRC8(scratchpad, 8) != scratchpad[8]) {
            continue; /* corrupted transfer - retry */
        }

        raw_temp = (uint16)(((uint16)scratchpad[1] << 8) | scratchpad[0]);
        return DS18B20_RawToCelsius(raw_temp);
    }

    return DS18B20_ERROR_TEMP;
}