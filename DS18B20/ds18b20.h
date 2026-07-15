#ifndef DS18B20_H
#define DS18B20_H

#include "../lib/std_types.h"
#include "../GPIO/GPIO.h"

void DS18B20_Init_Pin(uint8 PortName, uint8 PinNumber);
uint8 DS18B20_Reset(uint8 PortName, uint8 PinNumber);
void DS18B20_WriteByte(uint8 PortName, uint8 PinNumber, uint8 data);
uint8 DS18B20_ReadByte(uint8 PortName, uint8 PinNumber);
float DS18B20_ReadTemperature(uint8 PortName, uint8 PinNumber);

#endif /* DS18B20_H */