#include "ook_tx.h"

// Статическая переменная для хранения пина
static uint8_t ook_tx_pin;

// Переводим на функции CH32FUN
#define OOK_HIGH() funDigitalWrite(ook_tx_pin, FUN_HIGH)
#define OOK_LOW()  funDigitalWrite(ook_tx_pin, FUN_LOW)

#define BIT_PERIOD_US 500

static const uint8_t symbols[] = {
    0xd,  0xe,  0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c, 
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

void ook_tx_init(uint8_t tx_pin) {
    ook_tx_pin = tx_pin;

    // Инициализация пина в режим Output Push-Pull
    funPinMode(ook_tx_pin, GPIO_CFGLR_OUT_2Mhz_PP);

    OOK_LOW();
}

static uint16_t crc_ccitt_update(uint16_t crc, uint8_t data) {
    data ^= crc & 0xff;
    data ^= data << 4;
    return ((((uint16_t)data << 8) | (crc >> 8)) ^ (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
}

static void ook_send_symbol(uint8_t symbol) {
    for (uint8_t i = 0; i < 6; i++) {
        if (symbol & (1 << i)) {
            OOK_HIGH();
        } else {
            OOK_LOW();
        }
        Delay_Us(BIT_PERIOD_US);
    }
}

/**
 * @brief RadioHead
 */
void ook_tx_send_packet(const uint8_t *data, uint8_t length, uint8_t id, uint8_t id_from) {
    uint16_t crc = 0xFFFF;
    uint8_t count = length + 7; 
    
    for (uint8_t i = 0; i < 6; i++) {
        ook_send_symbol(0x2a);
    }
    
    ook_send_symbol(0x38);
    ook_send_symbol(0x2c);

    crc = crc_ccitt_update(crc, count);
    ook_send_symbol(symbols[count >> 4]);
    ook_send_symbol(symbols[count & 0x0F]);

    uint8_t headers[4] = {0xFF, id_from, id, 0x00};
    for (uint8_t i = 0; i < 4; i++) {
        crc = crc_ccitt_update(crc, headers[i]);
        ook_send_symbol(symbols[headers[i] >> 4]);
        ook_send_symbol(symbols[headers[i] & 0x0F]);
    }

    for (uint8_t i = 0; i < length; i++) {
        crc = crc_ccitt_update(crc, data[i]);
        ook_send_symbol(symbols[data[i] >> 4]);
        ook_send_symbol(symbols[data[i] & 0x0F]);
    }

    crc = ~crc;
    ook_send_symbol(symbols[(crc >> 4)  & 0x0F]);
    ook_send_symbol(symbols[crc & 0x0F]);
    ook_send_symbol(symbols[(crc >> 12) & 0x0F]);
    ook_send_symbol(symbols[(crc >> 8)  & 0x0F]);

    OOK_LOW();
}