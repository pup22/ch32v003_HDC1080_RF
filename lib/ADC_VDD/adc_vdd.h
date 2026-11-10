#ifndef ADC_VDD_H
#define ADC_VDD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32fun.h"

/**
 * @brief Инициализация АЦП и включение внутреннего канала Vrefint[cite: 1]
 */
void ADC_Vrefint_Init(void);

/**
 * @brief Чтение кода АЦП с канала Vrefint (ADC_IN8)[cite: 1]
 * @return 10-битный результат преобразования (0..1023)[cite: 1]
 */
uint16_t ADC_Read_Vrefint(void);

/**
 * @brief Расчёт реального напряжения VDD в милливольтах[cite: 1]
 * @return Напряжение VDD (например, 3000 = 3.000 В)[cite: 1]
 */
uint32_t Get_Calculated_VDD_mV(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_VDD_H */