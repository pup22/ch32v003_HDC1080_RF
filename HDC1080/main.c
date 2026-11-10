#include "funconfig.h"
#include "ch32fun.h"
#include "hdc1080.h"
#include "ook_tx.h"
#include "adc_vdd.h"
#include "sleep.h"

// фиксированный номер передатчика (1..254; 0 и 255 не использовать)
// В ESPHome он указывается как transmitter_id.
const static uint8_t id_transmitter = 0x01;

// ============================== ПИНЫ ==============================
// пины HDC1080
// Линии I2C: SDA -> PC2, SCL -> PC1
// PC3 -> VCC, PC4 -> GND
#define PIN_HDC1080_SCL       PC1
#define PIN_HDC1080_SDA       PC2
#define PIN_HDC1080_VCC       PC3
#define PIN_HDC1080_GND       PC4

// пины передатчика
#define PIN_RF_DATA           PC0
#define PIN_RF_EN             PA2

// неиспользуемые пины: вход с подтяжкой к плюсу
#define UNUSED_PINS           {PA1, PC5, PC6, PC7, PD0, PD1, PD2, PD3, PD4, PD5, PD6, PD7}
// ==================================================================

// сон: один шаг ~30 с (~34 с на практике, зависит от LSI)
#define SLEEP_STEPS  15  // 15 шагов ~ 9 минут между измерениями; 1 = быстрый режим для отладки

// Формат данных пакета: 8 байт, четыре поля по 2 байта, старший байт первым.
// Если данных для поля нет (датчика нет или он не ответил), все байты поля остаются 0xFF.
//   байты 0-1  температура, int16, 0.01 °C       (0xFFFF = нет данных)
//   байты 2-3  влажность,   uint16, 0.01 %       (0xFFFF = нет данных)
//   байты 4-5  давление,    uint16, 0.1 гПа      (0xFFFF = нет данных)
//   байты 6-7  напряжение,  uint16, мВ           (0xFFFF = нет данных)
#define PAYLOAD_LEN           8
#define OFFSET_TEMPERATURE    0
#define OFFSET_HUMIDITY       2
#define OFFSET_PRESSURE       4
#define OFFSET_VOLTAGE        6
#define NO_DATA               0xFFFF

// Записать 16-битное значение в буфер пакета, старший байт первым
static void put_u16(uint8_t *buf, uint8_t offset, uint16_t value)
{
    buf[offset]     = (uint8_t)(value >> 8);
    buf[offset + 1] = (uint8_t)(value & 0xFF);
}

int main(void)
{
    // 1. Инициализация системного тактирования
    SystemInit();
    // Делим частоту на RCC_HPRE_DIV8
    // ВАЖНО: тайминги ook_tx и Delay_Ms считаются от FUNCONF_SYSTEM_CORE_CLOCK из funconfig.h,
    // поэтому там должна быть уже поделённая частота, иначе скорость передачи будет неверной.
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV8;

    funGpioInitAll(); // включает тактирование GPIOA/C/D

    funDigitalWrite(PIN_RF_EN, FUN_HIGH); funPinMode(PIN_RF_EN, GPIO_CFGLR_OUT_2Mhz_PP); // передатчик выключен
    funDigitalWrite(PIN_RF_DATA, FUN_LOW);  funPinMode(PIN_RF_DATA, GPIO_CFGLR_OUT_2Mhz_PP);
    funDigitalWrite(PIN_HDC1080_VCC, FUN_LOW);  funPinMode(PIN_HDC1080_VCC, GPIO_CFGLR_OUT_2Mhz_PP); // датчик выключен
    funDigitalWrite(PIN_HDC1080_GND, FUN_LOW);  funPinMode(PIN_HDC1080_GND, GPIO_CFGLR_OUT_2Mhz_PP);

    Delay_Ms(5000); // Время подключиться и перепрошить микроконтроллер

    // Неиспользуемые пины
    const uint8_t unused[] = UNUSED_PINS;
    for (uint8_t i = 0; i < sizeof(unused); i++) {
        funDigitalWrite(unused[i], FUN_HIGH);   // бит OUTDR выбирает подтяжку к плюсу
        funPinMode(unused[i], GPIO_CFGLR_IN_PUPD);
    }

    while (1)
    {
        // Буфер данных передатчика: все поля заранее помечены «нет данных» (0xFF).
        // Каждый успешно прочитанный датчик перезаписывает своё поле.
        // Могут использоваться разные датчики с разным набором параметров.
        uint8_t tx_data[PAYLOAD_LEN];
        for (uint8_t i = 0; i < PAYLOAD_LEN; i++)
            tx_data[i] = 0xFF;

        // Считываем напряжение питания
        uint32_t vdd = Get_Calculated_VDD_mV();
        uint16_t v_payload = (vdd >= NO_DATA) ? (NO_DATA - 1) : (uint16_t)vdd;  // 0xFFFF зарезервировано
        put_u16(tx_data, OFFSET_VOLTAGE, v_payload);

        // --- БЕЗОПАСНОЕ СОСТОЯНИЕ ПЕРЕД ВКЛЮЧЕНИЕМ ДАТЧИКА ---
        // Переводим линии I2C в режим входа (High-Z),
        // чтобы они не шунтировали и не питали датчик паразитно во время подачи питания.
        funPinMode(PIN_HDC1080_SCL, GPIO_CFGLR_IN_FLOAT);
        funPinMode(PIN_HDC1080_SDA, GPIO_CFGLR_IN_FLOAT);

        // --- ВКЛЮЧЕНИЕ ПИТАНИЯ ДАТЧИКА ---
        funDigitalWrite(PIN_HDC1080_VCC, FUN_HIGH); // Питание (VCC)
        funDigitalWrite(PIN_HDC1080_GND, FUN_LOW);  // Земля (GND)
        funPinMode(PIN_HDC1080_VCC, GPIO_CFGLR_OUT_2Mhz_PP);
        funPinMode(PIN_HDC1080_GND, GPIO_CFGLR_OUT_2Mhz_PP);

        // --- ИНИЦИАЛИЗАЦИЯ И ЧТЕНИЕ ДАТЧИКА ---
        hdc1080_init(PIN_HDC1080_SDA, PIN_HDC1080_SCL);

        // Чтение данных с датчика. При ошибке чтения поля температуры и влажности остаются 0xFFFF.
        float temperature = 0.0f;
        float humidity = 0.0f;
        if (hdc1080_read(&temperature, &humidity))
        {
            // Температура в сотых долях градуса с округлением
            float t100 = temperature * 100.0f;
            int32_t t_i = (int32_t)(t100 < 0.0f ? (t100 - 0.5f) : (t100 + 0.5f));
            if (t_i < -4000) t_i = -4000;   // пределы, которые принимает приёмник
            if (t_i > 12500) t_i = 12500;
            // Значение -1 (-0.01 °C) имеет вид 0xFFFF и читалось бы как «нет данных»
            if (t_i == -1) t_i = 0;
            put_u16(tx_data, OFFSET_TEMPERATURE, (uint16_t)(int16_t)t_i);

            // Влажность в сотых долях процента; HDC1080 иногда выдаёт чуть меньше 0 или больше 100
            float h100 = humidity * 100.0f + 0.5f;
            if (h100 < 0.0f) h100 = 0.0f;
            if (h100 > 10000.0f) h100 = 10000.0f;
            put_u16(tx_data, OFFSET_HUMIDITY, (uint16_t)h100);
        }

        // Давление: датчика давления пока нет, поле остаётся 0xFFFF.
        // Когда появится датчик: put_u16(tx_data, OFFSET_PRESSURE, (uint16_t)(давление_гПа * 10.0f + 0.5f));

        // запускаем передатчик
        // Инициализация ASK/OOK передатчика
        ook_tx_init(PIN_RF_DATA);
        funDigitalWrite(PIN_RF_EN, FUN_LOW);  // транзистор pnp на разрыв питания передатчика
        funPinMode(PIN_RF_EN, GPIO_CFGLR_OUT_2Mhz_PP);

        // счетчик пакетов
        static uint8_t packet_id = 0;
        packet_id++;
        for (uint8_t i = 0; i < 3; i++)
        {
            // Передача пакета в эфир с CRC через OOK
            ook_tx_send_packet(tx_data, sizeof(tx_data), packet_id, id_transmitter);
            if (i < 2) Delay_Ms(15 + (id_transmitter % 8) * 7); // пауза только между пакетами, защита от наложения разных передатчиков
        }

        // --- ПОЛНОЕ ОБЕСТОЧИВАНИЕ ДАТЧИКА И ЛИНИЙ ДАННЫХ ---
        // Сначала переводим линии I2C в High-Z (вход), чтобы снять потенциалы,
        // затем отключаем питание VCC
        funPinMode(PIN_HDC1080_SCL, GPIO_CFGLR_IN_FLOAT);
        funPinMode(PIN_HDC1080_SDA, GPIO_CFGLR_IN_FLOAT);
        funDigitalWrite(PIN_HDC1080_VCC, FUN_LOW); // Отключаем VCC
        funDigitalWrite(PIN_HDC1080_GND, FUN_LOW); // GND в ноль

        // Линии I2C: вход с подтяжкой к земле (датчик обесточен)
        funDigitalWrite(PIN_HDC1080_SCL, FUN_LOW);
        funPinMode(PIN_HDC1080_SCL, GPIO_CFGLR_IN_PUPD);
        funDigitalWrite(PIN_HDC1080_SDA, FUN_LOW);
        funPinMode(PIN_HDC1080_SDA, GPIO_CFGLR_IN_PUPD);

        // Выкл передатчик
        funDigitalWrite(PIN_RF_DATA, FUN_LOW); // DATA в низкое
        funDigitalWrite(PIN_RF_EN, FUN_HIGH); // EN в высокое, выкл передатчик

        // Переход в глубокий сон
        for (uint8_t i = 0; i < SLEEP_STEPS; i++) {
            sleep_30s();
        }
    }
}
