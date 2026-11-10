#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

// you can put various configurations in here, you can see a full list in ch32fun.h
// part selection is made in the Makefile

#define CH32V003           1

// #define FUNCONF_USE_DEBUGPRINTF 1       // Включаем printf через SWD (minichlink)
// #define FUNCONF_DEBUGPRINTF_TIMEOUT 0x100000

#define FUNCONF_USE_HSI 1              // Используем внутренний RC-осциллятор (HSI)
#define FUNCONF_USE_PLL 0              // Отключаем PLL (умножитель частоты)

#define FUNCONF_SYSTEM_CORE_CLOCK 3000000
#define FUNCONF_SYSTICK_USE_HCLK 1

#endif