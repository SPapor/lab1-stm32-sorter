# Журнал виконання ЛР1

## Проблема 1: CubeMX не ставиться, хоча Java є
Симптом: інсталятор просить JRE 17+, хоча `java -version` = 21.
Фікс: `ln -s /usr/lib/jvm/java-17-openjdk-amd64 ~/Загрузки/jre`

## Проблема 2: CubeMX падає з UnsupportedClassVersionError\
Симптом: після встановлення лаунчер знову просить JRE 17;
Фікс: `ln -s /usr/lib/jvm/java-21-openjdk-amd64 ~/STM32CubeMX/jre`.

## Проблема 3: збірка падає на 'H_EXTI_13' undeclared
Симптом: `make build` → stm32c0xx_it.c:151: error: 'H_EXTI_13' undeclared у EXTI4_15_IRQHandler.
Фікс: вимкнув BSP-кнопку в CubeMX і перегенерував — обробник став `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13)`.

## Проблема 4: Wokwi не стартує — wokwi.toml not found
Фікс: створив wokwi.toml у корені stm32-sorter

## Проблема 5: MPU-6050 — accel завжди 0 0 0
Симптом: I2C-скан бачить 0x27 і 0x68, WHO_AM_I = 0x68, але читання 6 байт з 0x3B дає `0 0 0` навіть після зміни прискорення в симуляторі.
Фікс: `mpu_read`/`mpu_write` через `HAL_I2C_Mem_Read`/`HAL_I2C_Mem_Write` (відкочено, див. проблему 7); після запису 0x00 у 0x6B — пауза 100 мс і readback 0x6B; у лог пишуться HAL-статуси та сирі байти 0x3B у hex.

## Проблема 6: АЦП — gate і class завжди однакові (1001)
Симптом: `adc_read(ADC_CHANNEL_0)` і `adc_read(ADC_CHANNEL_1)` повертають одне й те саме значення.
Фікс: в `adc_read` для всіх інших каналів `ConfigChannel` з `Rank = ADC_RANK_NONE` (знімає біт), для потрібного — `ADC_RANK_CHANNEL_NUMBER`. adc.c не чіпав.

## Проблема 7: Mem_Read/Mem_Write → HAL_ERROR, WHO_AM_I перестав читатись
Симптом: після переходу на `HAL_I2C_Mem_Read`/`Mem_Write` усі транзакції з MPU-6050 повертають `st=1` (HAL_ERROR), навіть WHO_AM_I, хоча скан пристрої бачить.
Фікс: відкат на `HAL_I2C_Master_Transmit` + `HAL_I2C_Master_Receive` (зі STOP між ними); діагностика статусів і raw-байтів 0x3B лишилась.
