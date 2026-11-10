#ifndef SLEEP_H
#define SLEEP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32fun.h"

/**
 * @brief Переводит микроконтроллер в режим сна примерно на 30 секунд с помощью AWU[cite: 2]
 */
void sleep_30s(void);

#ifdef __cplusplus
}
#endif

#endif /* SLEEP_H */