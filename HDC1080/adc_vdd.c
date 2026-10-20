#include "ch32fun.h"

/**
 * @brief Инициализация АЦП и включение внутреннего канала Vrefint
 */
void ADC_Vrefint_Init(void)
{
    // 1. Включаем тактирование ADC1
    RCC->APB2PCENR |= RCC_APB2Periph_ADC1;

    // 2. Настраиваем делитель частоты АЦП (ADCPRE = HCLK / 8)
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_ADCPRE) | RCC_ADCPRE_DIV8;

    // 3. Устанавливаем время выборки для канала 8 (Vrefint) -> 241 цикл
    ADC1->SAMPTR2 &= ~(0x7 << (3 * 8));
    ADC1->SAMPTR2 |= (ADC_SampleTime_241Cycles << (3 * 8));

    // 4. Включаем АЦП (ADON), сенсор Vrefint (ADC_TSVREFE) 
    //    И КЛЮЧЕВОЙ БИТ: ADC_EXTSEL (выбор SWSTART как источника запускa)
    ADC1->CTLR2 |= ADC_ADON | ADC_TSVREFE | ADC_EXTSEL;

    // 5. Небольшая задержка для стабилизации АЦП (не менее 2 тактов ADCCLK)
    for (volatile int i = 0; i < 100; i++);

    // 6. Сброс калибровки
    ADC1->CTLR2 |= ADC_RSTCAL;
    uint32_t t = 100000;
    while (ADC1->CTLR2 & ADC_RSTCAL && --t);

    // 7. Калибровка АЦП
    ADC1->CTLR2 |= ADC_CAL;
    t = 100000;
    while (ADC1->CTLR2 & ADC_CAL && --t);
}

/**
 * @brief Чтение кода АЦП с канала Vrefint (ADC_IN8)
 * @return 10-битный результат преобразования (0..1023)
 */
uint16_t ADC_Read_Vrefint(void)
{
    ADC_Vrefint_Init();

    // Устанавливаем канал 8 (Vrefint)
    ADC1->RSQR3 = ADC_Channel_Vrefint;

    // Запускаем программное преобразование
    ADC1->CTLR2 |= ADC_SWSTART;

    // Ждем окончания преобразования
    uint32_t t = 100000;
    while (!(ADC1->STATR & ADC_EOC) && --t);

    // Считываем результат (чтение RDATAR автоматически сбрасывает флаг EOC)
    uint16_t adc = ADC1->RDATAR;

    // Выключаем АЦП
    ADC1->CTLR2 &= ~(ADC_ADON | ADC_TSVREFE);

    // Выключаем тактирование ADC1
    RCC->APB2PCENR &= ~RCC_APB2Periph_ADC1;
    
    return adc;
}

/**
 * @brief Расчёт реального напряжения VDD в милливольтах
 * @return Напряжение VDD (например, 3000 = 3.000 В)
 */
uint32_t Get_Calculated_VDD_mV(void)
{
    uint16_t adc_val = ADC_Read_Vrefint();
    if (adc_val == 0) return 0;

    // VDD (мВ) = (1200 мВ * 1024) / ADC_Value
    return (1200UL * 1024UL) / adc_val;
}