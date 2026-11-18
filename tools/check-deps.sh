#!/bin/sh
# Перевірка залежностей шарів (методичка ЛР2, docs/architecture.md, розділ 4):
# - App/ і Devices/ не включають HAL, згенеровані CubeMX заголовки, заголовки
#   Platform/ (board.h, platform.h, *_hal.h), FreeRTOS і Tasks/: їх тестують на хості;
# - Platform/ поза Platform/rtos/ не включає FreeRTOS і Tasks/: ядро бачать лише
#   Platform/rtos/ (конфігурація, хуки, база часу) і Tasks/ (задачі);
# - Tasks/ може включати все.
# App/ → Devices/ дозволено. Порушення: файл:рядок і код виходу 1.

cd "$(dirname "$0")/.." || exit 2

# Заголовки ядра FreeRTOS - усі, що є в include/ і в порту, плюс FreeRTOSConfig.h.
# Якщо сабмодуль не завантажено - хоча б основні за іменами
kernel=firmware/Middlewares/FreeRTOS
rtos=$(for f in "$kernel"/include/*.h "$kernel"/portable/GCC/ARM_CM0/*.h; do
    [ -e "$f" ] && basename "$f" | sed 's/\./\\./'
done | paste -sd'|' -)
rtos=${rtos:-'FreeRTOS\.h|task\.h|queue\.h|semphr\.h|stream_buffer\.h|message_buffer\.h|timers\.h|event_groups\.h|list\.h|portable\.h|projdefs\.h|portmacro\.h'}
rtos="FreeRTOSConfig\\.h|$rtos"
# Заголовки шару Tasks/ (за іменами файлів)
tasks=$(for f in firmware/Tasks/*.h; do [ -e "$f" ] && basename "$f" | sed 's/\./\\./'; done | paste -sd'|' -)
tasks=${tasks:-.^}
hal='stm32[^">]*|[^">]*_hal[^">]*|board\.h|platform\.h|main\.h|i2c\.h|adc\.h|tim\.h|usart\.h|gpio\.h'

# Рядок #include "..." або <...> з одним із імен (можливо, зі шляхом попереду)
inc() {
    echo "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]([^\">]*/)?($1)[\">]"
}

fail=0

# check <що заборонено> <шаблон імен> <каталоги або файли...>
check() {
    what=$1 names=$2
    shift 2
    grep -rnE --include='*.c' --include='*.h' "$(inc "$names")" "$@"
    case $? in
    0) echo "deps FAIL: $what" >&2; fail=1 ;;
    1) ;;
    *) echo "deps: помилка grep" >&2; exit 2 ;;
    esac
}

check "HAL, CubeMX, Platform, FreeRTOS або Tasks у App/ чи Devices/" \
      "$hal|$rtos|$tasks" firmware/App firmware/Devices

platform=$(find firmware/Platform -name '*.[ch]' -not -path 'firmware/Platform/rtos/*')
# shellcheck disable=SC2086
check "FreeRTOS або Tasks у Platform/ поза Platform/rtos/" "$rtos|$tasks" $platform

[ "$fail" -eq 0 ] && echo "deps OK"
exit "$fail"
