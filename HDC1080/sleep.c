#include "ch32fun.h"

void sleep_30s(void)
{
    RCC->APB1PCENR |= RCC_APB1Periph_PWR;
    RCC->RSTSCKR   |= RCC_LSION;
    uint32_t t = 100000;
    while (!(RCC->RSTSCKR & RCC_LSIRDY) && --t);
    if (!(RCC->RSTSCKR & RCC_LSIRDY)) {   // LSI не запустился: просто ждём без сна
        Delay_Ms(30720);
        return;
    }

    EXTI->EVENR |= EXTI_Line9;
    EXTI->FTENR |= EXTI_Line9;

    PWR->AWUPSC = PWR_AWU_Prescaler_61440;                  // 30,72 с (по факту ~34s)
    PWR->AWUWR  = (PWR->AWUWR & ~0x3F) | 63;
    PWR->AWUCSR |= (1 << 1);

    PWR->CTLR   |= PWR_CTLR_PDDS;
    PFIC->SCTLR |= (1 << 2);
    __WFE();

    PFIC->SCTLR &= ~(1 << 2);
    SystemInit();
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV8;
    funGpioInitAll();
}