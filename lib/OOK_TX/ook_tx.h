#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ch32fun.h"

// Инициализация пина передатчика
void ook_tx_init(uint8_t tx_pin);

// Отправка пакета данных в эфир с использованием протокола RH_ASK (4-to-6 бит, CRC16)
void ook_tx_send_packet(const uint8_t *data, uint8_t length, uint8_t id, uint8_t id_from);