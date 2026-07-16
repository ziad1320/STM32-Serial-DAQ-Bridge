/**
 * @file main.c
 * @brief Custom bare-metal pipeline to read temperatures from two DS18B20 1-Wire sensors
 *        and transmit their formatted values as a JSON string over USART2.
 *
 * This implementation utilizes only custom bare-metal drivers (GPIO, Timer, RCC, USART, DS18B20).
 * No STM32 HAL or Standard Peripheral Library is used.
 */

#include "../Rcc/Rcc.h"
#include "../GPIO/GPIO.h"
#include "../Usart/Usart.h"
#include "../DS18B20/ds18b20.h"
#include "../Timer/Timer.h"

#define SYSTEM_TIMER TIMER_2

/**
 * @brief Lightweight, bare-metal float to string formatter.
 * 
 * Avoids referencing the heavy stdio float formatting library which is disabled
 * in nano.specs to keep code size small and avoid runtime/linker issues.
 *
 * @param buffer Output char array to store the string.
 * @param val The float value to convert.
 * @param precision Number of decimal places to keep.
 */
static void float_to_str(char *buffer, float val, int precision) {
    // Handle negative numbers
    if (val < 0.0f) {
        *buffer++ = '-';
        val = -val;
    }

    // Add rounding offset based on the requested precision
    float rounding = 0.5f;
    for (int i = 0; i < precision; i++) {
        rounding /= 10.0f;
    }
    val += rounding;

    // Separate and convert the integer part
    uint32 integer_part = (uint32)val;
    char temp_int[16];
    int i = 0;
    if (integer_part == 0) {
        temp_int[i++] = '0';
    } else {
        while (integer_part > 0) {
            temp_int[i++] = (char)('0' + (integer_part % 10));
            integer_part /= 10;
        }
    }
    // Reverse integer part characters into the destination buffer
    while (i > 0) {
        *buffer++ = temp_int[--i];
    }

    // Process and convert the fractional part
    if (precision > 0) {
        *buffer++ = '.';
        float fractional_part = val - (uint32)val;
        for (int p = 0; p < precision; p++) {
            fractional_part *= 10.0f;
            uint32 digit = (uint32)fractional_part;
            *buffer++ = (char)('0' + digit);
            fractional_part -= digit;
        }
    }
    *buffer = '\0';
}

/**
 * @brief Formats a sensor reading for JSON, mapping the DS18B20 error sentinel
 *        to JSON `null` instead of the literal number -999.0.
 *
 * Without this, a failed read (bad CRC, no presence pulse after retries) would
 * serialize as a normal-looking numeric value, and anything parsing the JSON
 * on the other end of UART (e.g. the ESP-01S / whatever consumes this payload)
 * has no way to tell a real -999.0C reading apart from a sensor fault.
 *
 * @param buffer Output char array to store the string.
 * @param temp The temperature reading returned by DS18B20_ReadTemperature.
 */
static void format_sensor_value(char *buffer, float temp) {
    if (temp <= (DS18B20_ERROR_TEMP + 1.0f)) {
        buffer[0] = 'n';
        buffer[1] = 'u';
        buffer[2] = 'l';
        buffer[3] = 'l';
        buffer[4] = '\0';
    } else {
        float_to_str(buffer, temp, 1);
    }
}

/**
 * @brief Custom JSON string builder.
 *
 * Constructs the JSON payload `{"sensor1": <val1>, "sensor2": <val2>}\r\n` without using
 * the heavy and memory-unsafe sprintf() standard library function.
 *
 * @param buffer Output buffer to receive the complete JSON string.
 * @param temp1_str Formatted string of sensor 1.
 * @param temp2_str Formatted string of sensor 2.
 */
static void build_json_payload(char *buffer, const char *temp1_str, const char *temp2_str) {
    char *p = buffer;
    const char *s;

    // Append JSON key/prefix for sensor1
    s = "{\"sensor1\": ";
    while (*s) *p++ = *s++;

    // Append sensor1 value string
    s = temp1_str;
    while (*s) *p++ = *s++;

    // Append separator and key for sensor2
    s = ", \"sensor2\": ";
    while (*s) *p++ = *s++;

    // Append sensor2 value string
    s = temp2_str;
    while (*s) *p++ = *s++;

    // Append closing brace and CRLF line ending
    s = "}\r\n";
    while (*s) *p++ = *s++;

    // Null terminate
    *p = '\0';
}

int main(void) {
    Rcc_Init();
    Rcc_Enable(RCC_GPIOA);
    Rcc_Enable(RCC_USART2);
    Rcc_Enable(RCC_TIM2);

    /* Initialize DS18B20 Temperature Sensors on GPIO Port A */
    DS18B20_Init_Pin(GPIO_A, 0);
    DS18B20_Init_Pin(GPIO_A, 1);

    /* Initialize the USART Peripheral (9600 baud, 8 data bits, 1 stop bit, no parity) */
    Usart2_Init();

    /* Buffer structures to store string representations and JSON payload */
    char temp1_buffer[16];
    char temp2_buffer[16];
    char json_payload[128];

    /* 4. Infinite Monitoring Loop */
    while (1) {
        /* a. Read the temperature values from both sensors */
        float temp1 = DS18B20_ReadTemperature(GPIO_A, 0);
        float temp2 = DS18B20_ReadTemperature(GPIO_A, 1);

        /* b. Format float values into clean decimal-point strings (or "null" on sensor error) */
        format_sensor_value(temp1_buffer, temp1);
        format_sensor_value(temp2_buffer, temp2);

        /* c. Format temperature readings into a single JSON string */
        build_json_payload(json_payload, temp1_buffer, temp2_buffer);

        /* d. Transmit the complete JSON payload over the USART2 connection */
        Usart2_TransmitString(json_payload);

        /* e. Wait 3 seconds using the custom Timer_DelayMs block before next reading */
        Timer_DelayMs(SYSTEM_TIMER, 3000);
    }

    return 0;
}