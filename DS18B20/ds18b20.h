#ifndef DS18B20_H
#define DS18B20_H

#include "std_types.h"
#include "GPIO.h"

/* Sentinel returned when a conversion still fails after DS18B20_MAX_RETRIES
 * attempts (no presence pulse, or a CRC-failed scratchpad every time). */
#define DS18B20_ERROR_TEMP     (-999.0f)
#define DS18B20_MAX_RETRIES    3u

void  DS18B20_Init_Pin(uint8 PortName, uint8 PinNumber);
uint8 DS18B20_Reset(uint8 PortName, uint8 PinNumber);
void  DS18B20_WriteByte(uint8 PortName, uint8 PinNumber, uint8 data);
uint8 DS18B20_ReadByte(uint8 PortName, uint8 PinNumber);
float DS18B20_ReadTemperature(uint8 PortName, uint8 PinNumber);

/* Presence-only check - isolates wiring/pull-up/timing issues from the
 * rest of the protocol stack (ROM/function commands, scratchpad read,
 * CRC). Returns 1 if a presence pulse was seen, 0 otherwise. */
uint8 DS18B20_IsPresent(uint8 PortName, uint8 PinNumber);

#endif /* DS18B20_H */
