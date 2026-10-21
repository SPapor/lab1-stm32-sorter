#!/bin/sh
# Перевірка залежностей шарів (методичка ЛР2): firmware/App і firmware/Devices
# не включають HAL, згенеровані CubeMX заголовки чи заголовки Platform/
# (board.h, platform.h, *_hal.h).
# App/ → Devices/ дозволено. Порушення: файл:рядок і код виходу 1.

cd "$(dirname "$0")/.." || exit 2

# Ім'я файлу в #include "..." або <...>, можливо з шляхом попереду
forbidden='stm32[^">]*|[^">]*_hal[^">]*|board\.h|platform\.h|main\.h|i2c\.h|adc\.h|tim\.h|usart\.h|gpio\.h'
pattern="^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"]([^\">]*/)?($forbidden)[\">]"

grep -rnE --include='*.c' --include='*.h' "$pattern" firmware/App firmware/Devices
case $? in
0) echo "deps FAIL: заборонені #include у App/ або Devices/" >&2; exit 1 ;;
1) echo "deps OK" ;;
*) echo "deps: помилка grep" >&2; exit 2 ;;
esac
