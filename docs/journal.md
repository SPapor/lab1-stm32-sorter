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