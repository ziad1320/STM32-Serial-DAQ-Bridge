#include "ds18b20.h"
#include "Timer.h"

#define DS_TIMER TIMER_2

#define DS_RESET_LOW_US        480u  /* master reset pulse,            min 480us           */
#define DS_RESET_SAMPLE_US     70u   /* wait before sampling presence, see note below       */
#define DS_RESET_RECOVER_US    410u  /* remainder of the reset slot -> 480+70+410 = 960us   */

#define DS_WRITE1_LOW_US       6u    /* write '1': low pulse,          max 15us             */
#define DS_WRITE1_RELEASE_US   64u   /* -> total write slot ~70us,     spec range 60-120us  */
#define DS_WRITE0_LOW_US       60u   /* write '0': low pulse,          min 60us             */
#define DS_WRITE0_RELEASE_US   10u   /* -> total write slot ~70us                            */

#define DS_READ_INIT_US        2u    /* read: master low pulse,        min 1us, max 15us    */
#define DS_READ_SAMPLE_US      6u    /* wait before sampling,          see note below        */
#define DS_READ_RECOVER_US     54u   /* remainder of read slot ->      2+6+54 = 62us         */

/* DS_READ_SAMPLE_US was 10us (12us nominal total to sample). A 0xFF-every-
 * byte result traced back to that: the DS18B20 only holds the bus low for
 * up to 15us on a '0' bit before releasing it regardless, so any master
 * sample point that lands late (real overhead from Timer_DelayUs's own
 * ~7-8 register writes per call was likely pushing 12us nominal past 15us
 * in practice) reads EVERY bit as 1, since a late-sampled 0 looks
 * identical to a 1. 2+6=8us nominal leaves several us of real headroom
 * even with that overhead factored in. */

/* --- Bus drive/release helpers ---
 *
 * The pin is configured ONCE, in DS18B20_Init_Pin, as OUTPUT + OPEN_DRAIN.
 * It is never switched back to INPUT mode and MODER/PUPDR/OTYPER are never
 * touched again after that. "Drive low" and "release" are both just a
 * single ODR write via Gpio_WritePin:
 *   - LOW  -> the open-drain transistor actively pulls the bus down.
 *   - HIGH -> open-drain HIGH = high-impedance (no drive), so the external
 *             4.7k pull-up is what actually brings the bus back up.
 * Gpio_ReadPin still works correctly in this state - on STM32 the IDR
 * register reflects the real physical pin voltage regardless of whether
 * MODER has the pin set as output or input (as long as it's not analog
 * mode), which is exactly the mechanism every open-drain 1-Wire/I2C
 * bit-bang driver relies on.
 *
 * This replaces the previous version, which called the full Gpio_Init()
 * (a MODER + PUPDR/OTYPER read-modify-write) on every single bit toggle,
 * twice per bit. That reconfiguration is real CPU time on actual silicon
 * (unlike in Proteus, where it was effectively free) - and it was eating
 * directly into the DS18B20's tight read-slot timing budget (must sample
 * within 15us of the slot starting), pushing the real sample point past
 * that window on every single read. That's almost certainly why every
 * scratchpad was failing CRC: not random bus noise, but a fixed timing
 * error added identically to every bit, every time.
 */
static void DS18B20_SetOutput(uint8 PortName, uint8 PinNumber) {
    Gpio_WritePin(PortName, PinNumber, LOW);
}

static void DS18B20_SetInput(uint8 PortName, uint8 PinNumber) {
    Gpio_WritePin(PortName, PinNumber, HIGH);
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
 * Celsius. */
static float DS18B20_RawToCelsius(uint16 raw_temp) {
    if (raw_temp & 0x8000u) {
        uint16 magnitude = (uint16)((~raw_temp) + 1u); /* two's complement */
        return -((float)magnitude / 16.0f);
    }
    return (float)raw_temp / 16.0f;
}

void DS18B20_Init_Pin(uint8 PortName, uint8 PinNumber) {
    /* Configure ONCE as open-drain output, released (HIGH = Hi-Z) so the
     * external pull-up holds the bus idle-high with no contention at
     * boot. Nothing after this point touches MODER/PUPDR/OTYPER again -
     * every bit-bang operation is just an ODR write via DS18B20_SetOutput/
     * DS18B20_SetInput above. */
    Gpio_Init(PortName, PinNumber, GPIO_OUTPUT, GPIO_OPEN_DRAIN);
    Gpio_WritePin(PortName, PinNumber, HIGH);
}

uint8 DS18B20_Reset(uint8 PortName, uint8 PinNumber) {
    uint8 presence;

    /* Master pulls bus low for >=480us */
    DS18B20_SetOutput(PortName, PinNumber);
    Timer_DelayUs(DS_TIMER, DS_RESET_LOW_US);

    /* Master releases the bus and waits before sampling.
     *
     * The sensor starts its presence pulse 15-60us after release
     * (tPDHIGH) and holds it low for 60-240us (tPDLOW). The only sample
     * point guaranteed to land inside that pulse for EVERY valid part is
     * > 60us (pulse has definitely started) and < 75us (=15+60, the
     * earliest the pulse can possibly end). 70us sits in that guaranteed
     * window. */
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
            Timer_DelayUs(DS_TIMER, DS_WRITE1_LOW_US);

            DS18B20_SetInput(PortName, PinNumber); /* Release */
            Timer_DelayUs(DS_TIMER, DS_WRITE1_RELEASE_US);
        } else {
            /* Write 0: hold low for the full slot */
            DS18B20_SetOutput(PortName, PinNumber);
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
         * byte instead of trusting whichever bytes happened to come
         * back on the wire. */
        if (DS18B20_CRC8(scratchpad, 8) != scratchpad[8]) {
            continue; /* corrupted transfer - retry */
        }

        raw_temp = (uint16)(((uint16)scratchpad[1] << 8) | scratchpad[0]);
        return DS18B20_RawToCelsius(raw_temp);
    }

    return DS18B20_ERROR_TEMP;
}
