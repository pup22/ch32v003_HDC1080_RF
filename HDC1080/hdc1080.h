#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ch32fun.h"

// Стандартный I2C адрес HDC1080 (7-битный)
#define HDC1080_ADDR        0x40

// Регистры HDC1080
#define HDC1080_REG_TEMP    0x00
#define HDC1080_REG_HUMI    0x01
#define HDC1080_REG_CONFIG  0x02
#define HDC1080_REG_SERID_H 0xFB
#define HDC1080_REG_SERID_M 0xFC
#define HDC1080_REG_SERID_L 0xFD
#define HDC1080_REG_MANUFID 0xFE
#define HDC1080_REG_DEVID   0xFF

// Инициализация шины I2C и настройка HDC1080 с указанием пинов
void hdc1080_init(uint8_t sda_pin, uint8_t scl_pin);

// Чтение температуры в градусах Цельсия
bool hdc1080_read_temperature(float *temperature);

// Чтение относительной влажности в процентах (%)
bool hdc1080_read_humidity(float *humidity);

// Чтение сразу обоих параметров (более эффективно за один цикл опроса)
bool hdc1080_read(float *temperature, float *humidity);