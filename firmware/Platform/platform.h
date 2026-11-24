#ifndef PLATFORM_H
#define PLATFORM_H

/* Дрібні сервіси платформи, які драйвери Devices/ отримують як callback. */

#include <stdint.h>

/* Блокувальна затримка в мілісекундах (HAL_Delay, SysTick 1 мс). */
void platform_delay_ms(uint32_t ms);

void platform_i2c_reinit(void);

#endif /* PLATFORM_H */
