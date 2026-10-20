#include "funconfig.h"
#include "ch32fun.h"
#include "hdc1080.h"
#include "ook_tx.h"

// фиксированный номер передатчика
const static uint8_t id_transmitter = 0x01;

// функции измерения напряжения питания
extern uint32_t Get_Calculated_VDD_mV(void);
// функции сна
extern void sleep_30s(void);

int main(void)
{
    // 1. Инициализация системного тактирования
    SystemInit();
    // Делим частоту на RCC_HPRE_DIV8
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV8;

    funGpioInitAll(); // включает тактирование GPIOA/C/D

    funDigitalWrite(PA2, FUN_HIGH); funPinMode(PA2, GPIO_CFGLR_OUT_2Mhz_PP); // передатчик выключен
    funDigitalWrite(PC0, FUN_LOW);  funPinMode(PC0, GPIO_CFGLR_OUT_2Mhz_PP);
    funDigitalWrite(PC3, FUN_LOW);  funPinMode(PC3, GPIO_CFGLR_OUT_2Mhz_PP); // датчик выключен
    funDigitalWrite(PC4, FUN_LOW);  funPinMode(PC4, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(PA2, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(PC0, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(PC3, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(PC4, GPIO_CFGLR_OUT_2Mhz_PP);

    Delay_Ms(5000); // Время подключиться и перепрошить микроконтроллер

    // Неиспользуемые пины
    const uint8_t unused[] = {PA1, PC5, PC6, PC7, PD0, PD1, PD2, PD3, PD4, PD5, PD6, PD7};
    for (uint8_t i = 0; i < sizeof(unused); i++) {
        funDigitalWrite(unused[i], FUN_HIGH);   // бит OUTDR выбирает подтяжку к плюсу
        funPinMode(unused[i], GPIO_CFGLR_IN_PUPD);
    }

    while (1)
    {
        // буфер данных передатчика
        uint8_t tx_data[6] = {[0 ... 5] = 0xFF};

        // Считываем напряжение питания
        uint32_t vdd = Get_Calculated_VDD_mV();
        uint16_t v_payload = (uint16_t)vdd;
        // Формируем пакет для отправки по радиоканалу
        tx_data[4] = (uint8_t)(v_payload >> 8);
        tx_data[5] = (uint8_t)(v_payload & 0xFF);

        // --- БЕЗОПАСНОЕ СОСТОЯНИЕ ПЕРЕД ВКЛЮЧЕНИЕМ ДАТЧИКА ---
        // Переводим линии I2C (PC1, PC2) в режим входа (High-Z),
        // чтобы они не шунтировали и не питали датчик паразитно во время подачи питания.
        funPinMode(PC1, GPIO_CFGLR_IN_FLOAT);
        funPinMode(PC2, GPIO_CFGLR_IN_FLOAT);

        // --- ВКЛЮЧЕНИЕ ПИТАНИЯ ДАТЧИКА ---
        funDigitalWrite(PC3, FUN_HIGH); // Питание (VCC)
        funDigitalWrite(PC4, FUN_LOW);  // Земля (GND)
        // PC3 -> VCC, PC4 -> GND
        funPinMode(PC3, GPIO_CFGLR_OUT_2Mhz_PP);
        funPinMode(PC4, GPIO_CFGLR_OUT_2Mhz_PP);

        // --- ИНИЦИАЛИЗАЦИЯ И ЧТЕНИЕ ДАТЧИКА ---
        // Линии I2C: SDA -> PC2, SCL -> PC1
        hdc1080_init(PC2, PC1);

        // Чтение данных с датчика
        float temperature = 0.0f;
        float humidity = 0.0f;
        if (hdc1080_read(&temperature, &humidity))
        {
            // Формируем пакет для отправки по радиоканалу
            int16_t t_payload = (int16_t)(temperature < 0.0f ? (temperature * 100.0f - 0.5f) : (temperature * 100.0f + 0.5f));
            uint16_t h_payload = (uint16_t)(humidity * 100.0f + 0.5f);
            
            tx_data[0] = (uint8_t)(t_payload >> 8);
            tx_data[1] = (uint8_t)(t_payload & 0xFF);
            tx_data[2] = (uint8_t)(h_payload >> 8);
            tx_data[3] = (uint8_t)(h_payload & 0xFF);
        }

        // запускаем передатчик
        // Инициализация ASK/OOK передатчика
        ook_tx_init(PC0);
        funDigitalWrite(PA2, FUN_LOW);  // транзистор pnp на разрыв питания передатчика
        funPinMode(PA2, GPIO_CFGLR_OUT_2Mhz_PP);

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
        // затем отключаем питание VCC (PC3 -> LOW)
        funPinMode(PC1, GPIO_CFGLR_IN_FLOAT);
        funPinMode(PC2, GPIO_CFGLR_IN_FLOAT);
        funDigitalWrite(PC3, FUN_LOW); // Отключаем VCC
        funDigitalWrite(PC4, FUN_LOW); // GND в ноль

        // Линии I2C: вход с подтяжкой к земле (датчик обесточен)
        funDigitalWrite(PC1, FUN_LOW);
        funPinMode(PC1, GPIO_CFGLR_IN_PUPD);
        funDigitalWrite(PC2, FUN_LOW);
        funPinMode(PC2, GPIO_CFGLR_IN_PUPD);
        
        // Выкл передатчик
        funDigitalWrite(PC0, FUN_LOW); // DATA в низкое
        funDigitalWrite(PA2, FUN_HIGH); // EN в высокое, выкл передатчик

        // Переход в глубокий сон
        for(uint8_t i = 0; i < 15; i++){ // ~ 9 минут
            sleep_30s();
        }
    }
}