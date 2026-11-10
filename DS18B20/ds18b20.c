#include "ds18b20.h"

// Команды 1-Wire и DS18B20
#define OW_CMD_SKIP_ROM    0xCC
#define DS_CMD_CONVERT_T   0x44
#define DS_CMD_WRITE_SP    0x4E
#define DS_CMD_READ_SP     0xBE

#define DS_POLL_MS         5

// Микросекунды -> такты SysTick (HCLK)
#define OW_TICKS_PER_US    (FUNCONF_SYSTEM_CORE_CLOCK / 1000000UL)
#if OW_TICKS_PER_US < 1
#error "DS18B20: для 1-Wire нужна тактовая частота HCLK не ниже 1 МГц"
#endif
#define OW_US(n)           ((uint32_t) (n) * OW_TICKS_PER_US)

static GPIO_TypeDef *ow_port;
static uint32_t ow_mask;     // бит пина в INDR
static uint32_t ow_release;  // значение для BSHR: отпустить линию (подтяжка тянет вверх)
static uint32_t ow_pull;     // значение для BSHR: прижать линию к земле

static inline uint32_t ow_now(void) {
    return SysTick->CNT;
}

// Ждать, пока SysTick дойдёт до абсолютной метки t (устойчиво к переполнению счётчика)
static inline void ow_wait_until(uint32_t t) {
    while ((int32_t) (ow_now() - t) < 0) {
    }
}

static inline bool ow_level(void) {
    uint32_t in = ow_port->INDR;
    return (in & ow_mask) != 0;
}

void ds18b20_init(uint8_t pin) {
    ow_port = GpioOf(pin);
    ow_mask = 1u << (pin & 0xF);
    ow_release = ow_mask;
    ow_pull = ow_mask << 16;

    funDigitalWrite(pin, FUN_HIGH);  // сначала уровень («отпущено»), потом режим
    funPinMode(pin, GPIO_CFGLR_OUT_2Mhz_OD);
}

// Сброс и импульс присутствия
static bool ow_reset(void) {
    ow_port->BSHR = ow_release;

    // Линия должна быть высокой (подтяжка). Если за 1 мс она не поднялась, шина замкнута.
    uint32_t t = ow_now();
    while (!ow_level()) {
        if ((int32_t) (ow_now() - t) >= (int32_t) OW_US(1000))
            return false;
    }

    t = ow_now();
    ow_port->BSHR = ow_pull;
    ow_wait_until(t + OW_US(500));  // не менее 480 мкс
    ow_port->BSHR = ow_release;
    const uint32_t tr = ow_now();
    ow_wait_until(tr + OW_US(70));  // датчик отвечает через 15..60 мкс и держит 60..240 мкс
    const bool present = !ow_level();
    ow_wait_until(tr + OW_US(500));  // дождаться конца слота сброса
    return present && ow_level();
}

// Слот записи: «1» = низкий уровень ~6 мкс, «0» = низкий уровень ~62 мкс; слот не короче 60 мкс
static void ow_write_bit(bool bit) {
    const uint32_t t = ow_now();
    ow_port->BSHR = ow_pull;
    if (bit) {
        ow_wait_until(t + OW_US(6));
        ow_port->BSHR = ow_release;
        ow_wait_until(t + OW_US(70));
    } else {
        ow_wait_until(t + OW_US(62));
        ow_port->BSHR = ow_release;
        ow_wait_until(t + OW_US(72));
    }
}

// Слот чтения: низкий уровень ~3 мкс, выборка через ~9 мкс от начала слота (допустимо до 15 мкс)
static bool ow_read_bit(void) {
    const uint32_t t = ow_now();
    ow_port->BSHR = ow_pull;
    ow_wait_until(t + OW_US(3));
    ow_port->BSHR = ow_release;
    ow_wait_until(t + OW_US(9));
    const bool v = ow_level();
    ow_wait_until(t + OW_US(70));
    return v;
}

static void ow_write_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        ow_write_bit((b & 1) != 0);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(void) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (ow_read_bit())
            v |= (uint8_t) (1u << i);
    }
    return v;
}

// CRC-8 Dallas/Maxim (полином x^8 + x^5 + x^4 + 1)
static uint8_t ow_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t b = *data++;
        for (uint8_t i = 0; i < 8; i++) {
            const uint8_t mix = (uint8_t) ((crc ^ b) & 1);
            crc >>= 1;
            if (mix)
                crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

bool ds18b20_present(void) {
    return ow_reset();
}

bool ds18b20_set_resolution(uint8_t bits) {
    if (bits < 9)
        bits = 9;
    if (bits > 12)
        bits = 12;
    if (!ow_reset())
        return false;
    ow_write_byte(OW_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_WRITE_SP);
    ow_write_byte(0x7F);  // TH (порог тревоги, не используется)
    ow_write_byte(0x80);  // TL (порог тревоги, не используется)
    ow_write_byte((uint8_t) (((bits - 9) << 5) | 0x1F));  // регистр конфигурации
    return true;
}

bool ds18b20_start_conversion(void) {
    if (!ow_reset())
        return false;
    ow_write_byte(OW_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_CONVERT_T);
    return true;
}

bool ds18b20_wait_conversion(uint16_t timeout_ms) {
    for (uint16_t waited = 0; waited <= timeout_ms; waited += DS_POLL_MS) {
        Delay_Ms(DS_POLL_MS);
        if (ow_read_bit())  // пока идёт преобразование, датчик отвечает «0»
            return true;
    }
    return false;
}

bool ds18b20_read_scratchpad(uint8_t sp[9]) {
    if (!ow_reset())
        return false;
    ow_write_byte(OW_CMD_SKIP_ROM);
    ow_write_byte(DS_CMD_READ_SP);
    uint8_t all_ff = 0xFF;
    for (uint8_t i = 0; i < 9; i++) {
        sp[i] = ow_read_byte();
        all_ff &= sp[i];
    }
    if (all_ff == 0xFF)  // линия читается как «1»: датчик не отвечает
        return false;
    return ow_crc8(sp, 8) == sp[8];
}

int16_t ds18b20_raw_to_x100(int16_t raw, uint8_t config) {
    // при разрешении ниже 12 бит младшие биты не определены
    switch ((config >> 5) & 3) {
        case 0:
            raw &= (int16_t) ~7;  // 9 бит
            break;
        case 1:
            raw &= (int16_t) ~3;  // 10 бит
            break;
        case 2:
            raw &= (int16_t) ~1;  // 11 бит
            break;
        default:
            break;  // 12 бит
    }
    int32_t v = (int32_t) raw * 100;  // сотые доли °C = raw * 100 / 16, округление к ближайшему
    v += (v >= 0) ? 8 : -8;
    return (int16_t) (v / 16);
}

bool ds18b20_measure_x100(int16_t *t_x100) {
    for (uint8_t attempt = 0; attempt < DS18B20_ATTEMPTS; attempt++) {
        if (attempt)
            Delay_Ms(10);
        uint8_t sp[9];
        if (!ds18b20_set_resolution(DS18B20_RESOLUTION_BITS))
            continue;
        if (!ds18b20_start_conversion())
            continue;
        if (!ds18b20_wait_conversion(DS18B20_CONVERSION_TIMEOUT_MS))
            continue;
        if (!ds18b20_read_scratchpad(sp))
            continue;
        const int16_t raw = (int16_t) (((uint16_t) sp[1] << 8) | sp[0]);
        *t_x100 = ds18b20_raw_to_x100(raw, sp[4]);
        return true;
    }
    return false;
}
