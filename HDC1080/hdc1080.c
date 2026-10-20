#include "hdc1080.h"

// Статические переменные для хранения выбранных пинов
static uint8_t hdc_sda_pin;
static uint8_t hdc_scl_pin;

// Переводим макросы на использование переменных
#define SDA_HIGH() funDigitalWrite(hdc_sda_pin, FUN_HIGH)
#define SDA_LOW()  funDigitalWrite(hdc_sda_pin, FUN_LOW)
#define SCL_HIGH() funDigitalWrite(hdc_scl_pin, FUN_HIGH)
#define SCL_LOW()  funDigitalWrite(hdc_scl_pin, FUN_LOW)

#define SDA_READ() funDigitalRead(hdc_sda_pin)

static void i2c_delay(void) {
    Delay_Us(5);
}

static void i2c_start(void) {
    SDA_HIGH();
    SCL_HIGH();
    i2c_delay();
    SDA_LOW();
    i2c_delay();
    SCL_LOW();
    i2c_delay();
}

static void i2c_stop(void) {
    SDA_LOW();
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    SDA_HIGH();
    i2c_delay();
}

static bool i2c_write_byte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) SDA_HIGH();
        else SDA_LOW();
        data <<= 1;
        i2c_delay();
        SCL_HIGH();
        i2c_delay();
        SCL_LOW();
        i2c_delay();
    }
    
    SDA_HIGH();
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    bool ack = (SDA_READ() == 0);
    SCL_LOW();
    i2c_delay();
    return ack;
}

static uint8_t i2c_read_byte(bool ack) {
    uint8_t data = 0;
    SDA_HIGH();
    for (uint8_t i = 0; i < 8; i++) {
        data <<= 1;
        SCL_HIGH();
        i2c_delay();
        if (SDA_READ()) data |= 1;
        SCL_LOW();
        i2c_delay();
    }

    if (ack) SDA_LOW();
    else SDA_HIGH();
    i2c_delay();
    SCL_HIGH();
    i2c_delay();
    SCL_LOW();
    i2c_delay();
    SDA_HIGH();
    
    return data;
}

void hdc1080_init(uint8_t sda_pin, uint8_t scl_pin) {
    // Сохраняем пины
    hdc_sda_pin = sda_pin;
    hdc_scl_pin = scl_pin;

    // Инициализация пинов через стандартную функцию ch32fun
    funPinMode(hdc_sda_pin, GPIO_CFGLR_OUT_2Mhz_OD);
    funPinMode(hdc_scl_pin, GPIO_CFGLR_OUT_2Mhz_OD);

    SDA_HIGH();
    SCL_HIGH();
    Delay_Ms(15); // Задержка на включение датчика

    // Конфигурация HDC1080
    i2c_start();
    if (i2c_write_byte(HDC1080_ADDR << 1)) {
        i2c_write_byte(HDC1080_REG_CONFIG);
        i2c_write_byte(0x10);
        i2c_write_byte(0x00);
    }
    i2c_stop();
}

bool hdc1080_read(float *temperature, float *humidity) {
    i2c_start();
    if (!i2c_write_byte(HDC1080_ADDR << 1)) {
        i2c_stop();
        return false;
    }

    if (!i2c_write_byte(HDC1080_REG_TEMP)) {
        i2c_stop();
        return false;
    }
    i2c_stop();

    Delay_Ms(20);

    i2c_start();
    if (!i2c_write_byte((HDC1080_ADDR << 1) | 1)) {
        i2c_stop();
        return false;
    }

    uint8_t temp_h = i2c_read_byte(true);
    uint8_t temp_l = i2c_read_byte(true);
    uint8_t hum_h  = i2c_read_byte(true);
    uint8_t hum_l  = i2c_read_byte(false);
    i2c_stop();

    uint16_t raw_temp = ((uint16_t)temp_h << 8) | temp_l;
    uint16_t raw_hum  = ((uint16_t)hum_h << 8) | hum_l;

    if (raw_temp == 0xFFFF || (raw_temp == 0 && raw_hum == 0)) {
        return false;
    }

    if (temperature) {
        *temperature = ((float)raw_temp / 65536.0f) * 165.0f - 40.0f;
    }
    if (humidity) {
        *humidity = ((float)raw_hum / 65536.0f) * 100.0f;
    }

    return true;
}

bool hdc1080_read_temperature(float *temperature) {
    float hum;
    return hdc1080_read(temperature, &hum);
}

bool hdc1080_read_humidity(float *humidity) {
    float temp;
    return hdc1080_read(&temp, humidity);
}