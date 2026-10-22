# Архітектура прошивки Warehouse Sorter (ЛР2, п. 3.4, 4.5)

Документ описує, що є в коді станом на гілку `lab2/architecture`: шари, правило залежностей і його
автоматичну перевірку, доступ драйверів до шини, роботу з CubeMX, модулі HAL, версії сторонніх
компонентів і прапорці збірки.

![Component diagram](lab_2/img/components.png)

Джерело діаграми — [lab_2/img/components.drawio](lab_2/img/components.drawio). Стрілки йдуть у напрямку
`#include`, підпис — які саме заголовки включаються.

## 1. Шари

| Шар | Каталог | Що лежить | Від чого залежить (`#include`) | Хто його включає |
|---|---|---|---|---|
| Core (CubeMX) | `firmware/Core/` | ініціалізація периферії `MX_*_Init`, обробники переривань, `main.c` із секціями USER CODE, `stm32c0xx_hal_conf.h` | HAL; `main.h` → BSP `stm32c0xx_nucleo.h`; `main.c` у USER CODE — ще Platform, Devices, App | `Platform/board.h` (`main.h`, `adc.h`, `i2c.h`, `tim.h`, `usart.h`) |
| Drivers (ST) | `firmware/Drivers/` | STM32C0xx HAL, CMSIS, BSP NUCLEO-C031C6; не редагуються | CMSIS | Core, Platform |
| Platform | `firmware/Platform/` | `board.h` (знання про плату), обгортки над HAL: `i2c_hal`, `adc_hal`, `servo_hal`, `uart_hal`, `platform` | HAL; заголовки CubeMX через `board.h`; з Devices — лише інтерфейс `i2c_bus.h` | `main.c` |
| Devices | `firmware/Devices/` | інтерфейс шини `i2c_bus.h`, драйвери `mpu6050`, `lcd1602` | тільки `i2c_bus.h` і libc | `main.c`, `Platform/i2c_hal.h` (лише `i2c_bus.h`), `tests/` |
| App | `firmware/App/` | `cli` — розбір рядкових команд з UART | тільки libc (`string.h`, `stddef.h`, `stdint.h`) | `main.c`, `tests/` |
| tests | `tests/` | хостові тести (Unity), дубль шини `fakes/fake_i2c_bus` | Devices, App, `unity/`, `fakes/` | — (у прошивку не потрапляє) |

App зараз не включає Devices: `cli` — чистий розбір тексту, а виклики драйверів робить `main.c` (див. §6).
Напрямок App → Devices дозволений правилом, але поки не використовується.

### Файл → шар → відповідальність

| Файл | Шар | Відповідальність |
|---|---|---|
| `Core/Src/main.c` | Core (USER CODE) | composition root: створює шину й пристрої, самоперевірка, цикл CLI, телеметрія, команди `cap …`, `_write` → USART2 |
| `Core/Src/{gpio,adc,i2c,tim,usart}.c`, `Core/Inc/*.h` | Core (CubeMX) | `MX_*_Init` і `HAL_*_MspInit` (такти, піни), хендли `hadc1`, `hi2c1`, `htim3`, `huart2`, імена пінів (`LED_MARK_Pin` …) |
| `Core/Src/stm32c0xx_it.c`, `stm32c0xx_hal_msp.c` | Core (CubeMX) | обробники IRQ (SysTick, `EXTI4_15` кнопки E-STOP); `HAL_MspInit`: такти SYSCFG і PWR |
| `Core/Inc/stm32c0xx_hal_conf.h` | Core (CubeMX) | які модулі HAL увімкнені (§7) |
| `startup_stm32c031xx.s`, `STM32C031xx_FLASH.ld`, `Makefile` | Core (CubeMX) | старт, карта пам'яті (FLASH 32 КБ, RAM 12 КБ), згенерована збірка |
| `app.mk` | збірка | надбудова над `Makefile`: власні каталоги, `-Os`, `-Wextra -Werror` (§5, §9) |
| `Platform/board.h` | Platform | хендли, канали АЦП, канал таймера, маркери, кнопка, адреси I²C, межі імпульсу серво |
| `Platform/i2c_hal.{h,c}` | Platform | реалізація `i2c_bus_t` над `HAL_I2C_Master_Transmit/Receive`, відновлення шини після збою |
| `Platform/adc_hal.{h,c}` | Platform | одне перетворення каналу, маркер `ADC_MARK` на час перетворення |
| `Platform/servo_hal.{h,c}` | Platform | ШИМ TIM3_CH1, кут 0..180° → 1000..2000 мкс, зворотне читання CCR |
| `Platform/uart_hal.{h,c}` | Platform | прийом символу з USART2 опитуванням, скидання ORE |
| `Platform/platform.{h,c}` | Platform | `platform_delay_ms` — затримка, яку драйвери отримують як callback |
| `Devices/i2c_bus.h` | Devices | інтерфейс шини: `write`/`read` + `ctx`, `bus_status_t` |
| `Devices/mpu6050.{h,c}` | Devices | WHO_AM_I, вихід зі SLEEP зі зворотним читанням, читання прискорення, raw → mg |
| `Devices/lcd1602.{h,c}` | Devices | HD44780 у 4-бітному режимі через PCF8574, лише 1-байтові записи |
| `App/cli.{h,c}` | App | розбір команд `help`, `stream`, `cap …`; нічого не друкує і не знає про HAL |
| `tests/test_*.c` | tests | тести `mpu6050`, `lcd1602`, `cli` (по одному виконуваному файлу) |
| `tests/fakes/fake_i2c_bus.{h,c}` | tests | дубль `i2c_bus_t`: mock-сценарій або модель регістрів, ін'єкція NACK/TIMEOUT |
| `tests/unity/` | tests | Unity v2.7.0, vendoring без змін (§8) |
| `tools/check-deps.sh` | інструменти | перевірка правила залежностей (§2) |

## 2. Правило залежностей

- **App → Devices → `i2c_bus.h`**. App і Devices не знають про HAL, CubeMX і Platform.
- **Platform → HAL і Core**. Platform реалізує інтерфейс Devices (`i2c_hal.h` включає `i2c_bus.h`), а не навпаки.
- **Core/`main.c` → усі шари**: це composition root.
- **tests → Devices, App**. Тести не бачать ні HAL, ні Platform.

Перевірка автоматична:

- **Скрипт.** `tools/check-deps.sh` шукає в `firmware/App` і `firmware/Devices` рядки `#include`, ім'я в яких
  (з можливим шляхом попереду) збігається з `stm32*`, `*_hal*`, `board.h`, `platform.h`, `main.h`, `i2c.h`,
  `adc.h`, `tim.h`, `usart.h`, `gpio.h`. Знайдено — друкує `файл:рядок:текст` і завершується з кодом 1.
- **Make.** `make check-deps` запускає скрипт на хості, Docker не потрібен.
- **CI.** У `.github/workflows/ci.yml` це крок «Layer dependencies», виконується на кожен push і pull request
  до збірки й тестів.
- **Неявна друга перевірка.** Тести збираються хостовим gcc лише з `-I` на `Devices/`, `App/`, `unity/`,
  `fakes/`. Тому заголовок HAL у драйвері чи `cli` зламає `make test`.

Приклад: до копії дерева додано `#include "stm32c0xx_hal.h"` у `App/cli.c` і `#include "../Platform/board.h"`
у `Devices/mpu6050.c`. Скрипт виводить:

```
firmware/App/cli.c:2:#include "stm32c0xx_hal.h"
firmware/Devices/mpu6050.c:2:#include "../Platform/board.h"
deps FAIL: заборонені #include у App/ або Devices/
```

Код виходу 1, отже CI-крок падає. На поточному дереві скрипт друкує `deps OK`.

Чого скрипт **не** перевіряє:
- напрямки Devices → App і Platform → App;
- каталог `tests/`;
- включення, сховані за макросом (`#include MACRO`).

Зараз таких включень немає, але це перевіряється лише переглядом.

## 3. Доступ драйвера до шини

Драйвер отримує шину як структуру з вказівниками на функції та непрозорим контекстом
(`firmware/Devices/i2c_bus.h`):

```c
typedef enum { BUS_OK = 0, BUS_ERR_NACK, BUS_ERR_TIMEOUT, BUS_ERR_OTHER } bus_status_t;

typedef struct {
    bus_status_t (*write)(void *ctx, uint8_t addr7, const uint8_t *data, size_t len); /* START, addr+W, data, STOP */
    bus_status_t (*read)(void *ctx, uint8_t addr7, uint8_t *data, size_t len);        /* START, addr+R, data, STOP */
    void *ctx;
} i2c_bus_t;
```

Драйвер зберігає вказівник на шину в структурі пристрою (`mpu6050_t.bus`, `lcd1602_t.bus`) і викликає тільки
`bus->write` / `bus->read`. Про `HAL_I2C_*` він не знає (`Devices/mpu6050.c`):

```c
if (bus->write(bus->ctx, dev->addr, &reg, 1) != BUS_OK)
    return MPU6050_ERR_BUS;
if (bus->read(bus->ctx, dev->addr, buf, len) != BUS_OK)
    return MPU6050_ERR_BUS;
```

**Прошивка.** `Platform/i2c_hal.c` заповнює ту саму структуру функціями поверх HAL, а `ctx` вказує на
`i2c_hal_ctx_t` з хендлом `I2C_HandleTypeDef` і функцією переініціалізації:

```c
static bus_status_t hal_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    i2c_hal_ctx_t *c = ctx;
    ...
    HAL_StatusTypeDef s = HAL_I2C_Master_Transmit(c->h, (uint16_t)(addr7 << 1),
                                                  (uint8_t *)data, (uint16_t)len,
                                                  I2C_HAL_TIMEOUT_MS);
    return finish(c, s, "wr");   /* статус HAL → bus_status_t, при помилці — recover */
}
```

**Зв'язування** робить `main.c`:
`i2c_hal_bus_init(&i2c1_bus, &i2c1_ctx, BOARD_I2C, BOARD_I2C_REINIT)`, далі
`mpu6050_init(&imu, &i2c1_bus, BOARD_MPU_ADDR)` і `lcd1602_init(&lcd, &i2c1_bus, BOARD_LCD_ADDR, platform_delay_ms)`.
Затримку LCD-драйвер теж отримує як callback, тож `HAL_Delay` у Devices немає.

**Тести.** `tests/fakes/fake_i2c_bus` підставляє в `i2c_bus_t` свої `write`/`read`. Режими:
- `FAKE_I2C_SCRIPT` (mock): очікувана послідовність транзакцій `{write|read, адреса, байти, статус}`, тест падає
  на першій розбіжності;
- `FAKE_I2C_REGS` (fake): модель регістрів MPU-6050 з автоінкрементом вказівника.

В обох режимах `fake_i2c_fail_at` змушує N-ту транзакцію повернути NACK або TIMEOUT.

## 4. Знання про плату — `Platform/board.h` ↔ `docs/pinout.md` §1

Призначення пінів задає `firmware.ioc`. CubeMX перетворює його на `MX_*_Init` і макроси в `main.h`
(`LED_MARK_Pin`, `ESTOP_Pin`, `ADC_MARK_Pin` …). Власний код бачить плату лише через `board.h`: ролі
(«I²C пристроїв», «LDR gate», «серво») прив'язані до хендлів і імен CubeMX. Після перейменування в `.ioc`
змінюється тільки цей файл. Devices і App `board.h` не включають (перевіряє `check-deps`).

| Макрос `board.h` | Периферія / пін | Рядок `pinout.md` §1 |
|---|---|---|
| `BOARD_I2C`, `BOARD_I2C_REINIT` | `hi2c1`, `MX_I2C1_Init` | PA9 I2C1_SCL, PA10 I2C1_SDA |
| `BOARD_ADC`, `BOARD_ADC_CH_GATE`, `BOARD_ADC_CH_CLASS` | `hadc1`, `ADC_CHANNEL_0/1` | PA0 ADC1_IN0, PA1 ADC1_IN1 |
| `BOARD_SERVO_TIM`, `BOARD_SERVO_CH` | `htim3`, `TIM_CHANNEL_1` | PA6 TIM3_CH1 |
| `BOARD_UART` | `huart2` | PA2 USART2_TX, PA3 USART2_RX (VCP) |
| `BOARD_LED_MARK_PORT/PIN` | `LED_MARK` | PA5 (LD4, маркер, канал D4) |
| `BOARD_ADC_MARK_PORT/PIN` | `ADC_MARK` | PA15 (маркер АЦП, канал D5) |
| `BOARD_ESTOP_PIN` | `ESTOP` | PC13 (B1, EXTI) |
| `BOARD_MPU_ADDR`, `BOARD_LCD_ADDR` | 0x68, 0x27 | `pinout.md` §2 |
| `BOARD_SERVO_MIN_US`, `BOARD_SERVO_MAX_US` | 1000 / 2000 мкс | `pinout.md` §3.3 |

## 5. CubeMX

**Що генерується.** З `firmware/firmware.ioc` (STM32CubeMX 6.18.1, `ProjectManager.TargetToolchain=Makefile`)
генеруються:
- `Core/Inc`, `Core/Src`;
- `startup_stm32c031xx.s`, `STM32C031xx_FLASH.ld`, `Makefile`, `.mxproject`;
- каталог `Drivers/`. Його скопійовано повністю (`ProjectManager.LibraryCopy=0`), тому там є й невикористані
  частини CMSIS (DSP, NN, RTOS).

**Чому `firmware/Makefile` не редагуємо.** CubeMX перезаписує його повністю, секцій USER CODE у ньому немає.
Власна збірка — у `firmware/app.mk` (`make -f app.mk`), який:
- включає згенерований `Makefile`;
- перевизначає `OPT = -Os`;
- додає `Platform/`, `Devices/`, `App/` у `C_SOURCES` і `C_INCLUDES`;
- вмикає `-Wextra -Werror` для власних об'єктів і `main.o`;
- явно додає власні об'єкти в пререквізити `firmware.elf`, бо правило лінкування в `Makefile` розкриває
  список об'єктів у момент читання, до `C_SOURCES +=`.

**USER CODE BEGIN/END.** Після регенерації зберігається лише текст між маркерами
(`ProjectManager.KeepUserCode=true`). Обмеження:
- маркери фіксовані: додати власну секцію не можна, а будь-яка правка поза маркерами зникає;
- файли без потрібних секцій (`Makefile`, перелік модулів у `stm32c0xx_hal_conf.h`) переписуються повністю,
  тому не редагуються;
- код у USER CODE посилається на імена з `.ioc` (`hi2c1`, `LED_MARK_Pin`). Саме тому ці імена зібрано в
  `board.h`, а не розкидано по коду.

**CRLF.** CubeMX пише `Core/*` і `Makefile` з кінцями рядків CRLF, власні файли мають LF, `.gitattributes`
у репозиторії немає. В історії `main.c` двічі збережено в LF (`032079c`, `67afe00`), і регенерація з `.ioc`
у `679124e` повернула CRLF: diff `main.c` вийшов на весь файл (240/240 рядків). Правило — редагувати файли
`Core/` зі збереженням CRLF.

## 6. Composition root

Об'єкти шини й пристроїв створюються в `main.c`, у секціях USER CODE (разом із маркерами — 486 з 595 рядків файлу):

- **Статичні об'єкти:** `i2c1_ctx`, `i2c1_bus`, `imu`, `lcd`, `cli`.
- **Зв'язування:** `i2c_hal_bus_init(…)`, далі `selftest_run()`, яка викликає `mpu6050_init`, `lcd1602_probe`,
  `lcd1602_init`, `adc_hal_read`, `servo_hal_set_angle`.
- **Головний цикл:** `uart_hal_getc` → `cli_feed` → `cmd_run`, плюс періодичний `telemetry_print`.

**Що поки лежить у `main.c`, хоча за змістом це прикладна логіка:**
- класифікація результатів самоперевірки (OK/WARN/FAIL) і рядок summary;
- формат телеметрії;
- виконання команд CLI, зокрема діагностичні `cap …` для записів аналізатора;
- I²C-скан, `_write` для `printf`.

Причина: ці функції напряму користуються HAL і CubeMX — `HAL_GetTick`, `HAL_GPIO_WritePin` (маркер
`LED_MARK`), `HAL_UART_Transmit`, `HAL_I2C_IsDeviceReady`, `HAL_Delay` — і `printf`. Щоб перенести їх в App/,
потрібні інтерфейси часу, журналу й маркерів (як `platform_delay_ms` для LCD). Їх ще немає, тому App/ поки
містить лише `cli`, яку можна тестувати на хості.

Кнопка E-STOP налаштована в CubeMX (EXTI, `EXTI4_15_IRQHandler` → `HAL_GPIO_EXTI_IRQHandler`), але колбек
`HAL_GPIO_EXTI_Falling_Callback` не перевизначено: натискання поки нічого не робить.

## 7. Модулі HAL (`Core/Inc/stm32c0xx_hal_conf.h`)

Розмір — сума функцій модуля в `firmware.elf` за `arm-none-eabi-nm` (збірка `-Os`, `--gc-sections`).
Колонка «Без модуля» — результат збірки копії дерева з цим модулем, закоментованим у `hal_conf.h`.

| Модуль | Навіщо / хто використовує | У ELF | Без модуля |
|---|---|---|---|
| `HAL_MODULE_ENABLED` | `HAL_Init`, SysTick 1 мс: `HAL_Delay`, `HAL_GetTick` (`main.c`, `platform.c`) | 224 B | — |
| `HAL_ADC_MODULE_ENABLED` | `MX_ADC1_Init`; `adc_hal.c` (`ConfigChannel`, `Start`, `PollForConversion`, `GetValue`); калібрування в `main.c` | 2310 B | — |
| `HAL_I2C_MODULE_ENABLED` | `MX_I2C1_Init`; `i2c_hal.c` (`Master_Transmit/Receive`, `DeInit`); I²C-скан (`IsDeviceReady`) | 2304 B | — |
| `HAL_TIM_MODULE_ENABLED` | `MX_TIM3_Init`; `servo_hal.c` (`PWM_Start`, CCR) | 1642 B | — |
| `HAL_UART_MODULE_ENABLED` | `MX_USART2_UART_Init`; `printf` → `HAL_UART_Transmit`; прапорці RXNE/ORE в `uart_hal.c` | 1462 B | — |
| `HAL_GPIO_MODULE_ENABLED` | `MX_GPIO_Init`; маркери `HAL_GPIO_WritePin`; IRQ кнопки `HAL_GPIO_EXTI_IRQHandler` | 668 B | — |
| `HAL_RCC_MODULE_ENABLED` | `SystemClock_Config` (HSE 48 МГц), такти периферії в MSP | 1398 B | — |
| `HAL_CORTEX_MODULE_ENABLED` | `HAL_NVIC_SetPriority/EnableIRQ` (EXTI4_15), `HAL_SYSTICK_Config` | 160 B | — |
| `HAL_FLASH_MODULE_ENABLED` | `FLASH_LATENCY_1` і `__HAL_FLASH_SET_LATENCY` у `SystemClock_Config` | 0 B (макроси) | помилка: `'FLASH_LATENCY_1' undeclared` у `main.c` |
| `HAL_DMA_MODULE_ENABLED` | DMA не використовується; заголовок потрібен, бо хендли ADC/TIM/UART/I2C містять `DMA_HandleTypeDef *` | 0 B | помилка: `unknown type name 'DMA_HandleTypeDef'` у `stm32c0xx_hal_adc.h:467`, `_tim.h:359` … |
| `HAL_EXTI_MODULE_ENABLED` | API `HAL_EXTI_*` не використовується (кнопка йде через GPIO); тип потрібен заголовку BSP, який включає `main.h` | 0 B | помилка: `unknown type name 'EXTI_HandleTypeDef'` у `stm32c0xx_nucleo.h:258` |
| `HAL_PWR_MODULE_ENABLED` | не використовується (`__HAL_RCC_PWR_CLK_ENABLE` у MSP — макрос RCC) | 0 B | збирається, text той самий: 26 496 B |

Решта модулів (CRC, I2S, IWDG, SPI, USART, WWDG …) вимкнені.

**Чому не вимкнено PWR.** Він не потрібен: збірка без нього проходить, у прошивку не потрапляє жодна його
функція. Але `PWR`, `DMA`, `EXTI`, `FLASH`, `GPIO` не є IP у `.ioc` (там лише `ADC1`, `CORTEX_M0+`, `I2C1`,
`NUCLEO-C031C6`, `NVIC`, `RCC`, `SYS`, `TIM3`, `USART2`), тож вимкнути їх у CubeMX немає де. CubeMX вписує їх
у `hal_conf.h` сам, а ручна правка поза USER CODE зникне при наступній регенерації. Тому `PWR` лишається
увімкненим: це заголовки без коду в ELF, і `.ioc` з `hal_conf.h` не розходяться.

## 8. Версії

| Компонент | Версія | Де зафіксовано |
|---|---|---|
| STM32CubeMX | 6.18.1 (DB 6.0.181) | `firmware.ioc`: `MxCube.Version`, `MxDb.Version` |
| Пакет STM32CubeC0 | FW_C0 V1.4.1 | `firmware.ioc`: `ProjectManager.FirmwarePackage` |
| STM32C0xx HAL Driver | 1.4.1 | `Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal.c`: `__STM32C0xx_HAL_VERSION_*` |
| CMSIS Device STM32C0xx | 1.4.1 | `Drivers/CMSIS/Device/ST/STM32C0xx/Include/stm32c0xx.h`: `__STM32C0_CMSIS_VERSION_*` |
| CMSIS Core(M) | 5.6 | `Drivers/CMSIS/Include/cmsis_version.h` |
| BSP STM32C0xx_Nucleo | 1.1.1 | `Drivers/BSP/STM32C0xx_Nucleo/stm32c0xx_nucleo.h` |
| Unity | v2.7.0, коміт тегу `b6763fbd9cedfacaa89e2ad9fd00d615a234e355`, архів `v2.7.0.tar.gz` sha256 `e84eb301ca7967831e68b1728f911e87fa2d345d8ddb64f897bc2f2ee24a321c` | `tests/unity/` (unity.c, unity.h, unity_internals.h, LICENSE.txt без змін); `UNITY_VERSION_*` в `unity.h` |
| Базовий образ Docker | `ubuntu:22.04` (Ubuntu 22.04.5 LTS); на 2026-10-22 тег вказує на `sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956` | `docker/Dockerfile` (тег без digest) |
| arm-none-eabi-gcc | 10.3.1 20210621, пакет `gcc-arm-none-eabi 15:10.3-2021.07-4` | apt у Docker |
| binutils-arm-none-eabi | 2.38 (`2.38-3ubuntu1+15build1`) | apt у Docker |
| newlib | `libnewlib-arm-none-eabi 3.3.0-1.3`, `libstdc++-arm-none-eabi-newlib 15:10.3-2021.07-4+17` | apt у Docker |
| make | 4.3 (`4.3-4.1build1`) | apt у Docker |
| gcc (хостові тести) | 11.4.0 (`11.4.0-1ubuntu1~22.04.3`) | apt у Docker |
| gcovr | 5.0 (`5.0-2`) | apt у Docker |
| sigrok-cli | 0.7.2 (libsigrok 0.5.2, libsigrokdecode 0.5.3) | хост, лише для аналізу записів |
| PulseView | 0.4.2 | хост, лише для скріншотів |
| Wokwi | розширення VS Code 4.0.1, `wokwi.toml` `version = 1` | хост |

**Що відтворюється через рік.** Усе, що лежить у репозиторії, лишається тим самим:
- HAL, CMSIS, BSP — vendoring від CubeMX з версіями в заголовках;
- Unity — vendoring з комітом і хешем вище.

**Що може змінитися.** У Docker зафіксовано лише тег `ubuntu:22.04` без digest, версії пакетів apt не
закріплені. `docker build` через рік візьме останні оновлення 22.04. Крос-компілятор
`gcc-arm-none-eabi 15:10.3-2021.07-4` і newlib походять з релізного набору jammy, який не оновлюється, тож
`firmware.elf` очікувано не зміниться. Але патч-версії gcc, gcovr чи make для тестів можуть змінитися.
Щоб закрити це повністю, потрібні `FROM ubuntu:22.04@sha256:…` і apt зі знімка архіву
(snapshot.ubuntu.com). Цього поки не зроблено.

## 9. Прапорці збірки

- **Усі файли прошивки** (з `Makefile` CubeMX): `-mcpu=cortex-m0plus -mthumb -Wall -fdata-sections
  -ffunction-sections -g -gdwarf-2`. Лінкування: `-specs=nano.specs -Wl,--gc-sections`.
- **Власний код** (`app.mk`): `Platform/`, `Devices/`, `App/` і `Core/Src/main.c` — додатково `-Wextra -Werror`.
  Для `main.c` прапорці додано в цій гілці: у його секціях USER CODE лежить власний код, і з `-Wextra` він
  чистий.
- **Вендорський і згенерований код** (`Drivers/`, решта `Core/`): лише `-Wall`. Нічого не придушено
  через `-Wno-*`, ці файли просто не отримують `-Wextra -Werror`. Причина у вендорському коді: з `-Wextra` HAL
  дає 3 попередження (`stm32c0xx_hal_flash_ex.c` — 1, `stm32c0xx_hal_pwr.c` — 2), а правити його не можна.
  Згенеровані файли `Core/` з `-Wextra` чисті, але їх повністю пише CubeMX.
- **Результат:** `make build` дає 0 попереджень.
- **Хостові тести** (`tests/Makefile`): gcc `-std=c11 -Wall -Wextra -Werror -O0 -g --coverage -fprofile-abs-path`,
  включно з Unity.

**`-Os` і бюджет flash** (FLASH 32 КБ = 32 768 B, RAM 12 КБ; поточний код, `arm-none-eabi-size`):

| Оптимізація | text | % flash | data + bss |
|---|---|---|---|
| `-Og` (за замовчуванням у згенерованому `Makefile`) | 28 616 B | 87,3 % | 112 + 2096 B |
| `-Os` (`app.mk`) | 26 496 B | 80,9 % | 112 + 2096 B |

`-Os` економить 2 120 B (7,4 %). У flash лягають text + data = 26 608 B, тобто запас ~6 КБ на наступні ЛР.

## 10. Відмінності від орієнтовної структури (методичка, п. 3.4)

1. **Немає `Middlewares/`.** RTOS і сторонніх бібліотек у прошивці немає. Єдина стороння бібліотека —
   Unity, вона лише в `tests/unity/`.
2. **Інтерфейс шини лежить у `Devices/`, а не в `Platform/`.** Інтерфейс належить споживачам (драйверам),
   Platform його реалізує. Так Devices не залежить від Platform ні в прошивці, ні в тестах, де ту саму
   структуру реалізує `fake_i2c_bus`.
3. **Прикладна логіка поки частково в `Core/Src/main.c`, а не в `App/`** (§6). В App/ винесено те, що не
   потребує HAL (`cli`).
4. **У `Drivers/` є BSP NUCLEO-C031C6.** Його додає CubeMX через вибір плати. У прошивку не потрапляє жоден
   символ BSP, але заголовок включає `main.h`, тому потрібен модуль EXTI (§7).
5. **Збірка через `app.mk`-надбудову,** бо згенерований `Makefile` перезаписується CubeMX (§5).
