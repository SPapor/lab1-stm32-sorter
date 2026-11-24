#!/bin/sh
set -e
LTO=$1; EXTRA="$2"; OUT=build$LTO$(echo "$EXTRA" | tr -dc "A-Za-z0-9_")
cd "$(dirname "$0")"; rm -rf $OUT; mkdir -p $OUT
CF="-mcpu=cortex-m0plus -mthumb -Os -Wall -fdata-sections -ffunction-sections -fstack-usage -DSTM32C031xx -DTICK_HZ=1000 -DPERF=1 -DSIM_CRIT=1 -DRTOS_ASSERT=0 -DARCH_RTOS=1 $EXTRA"
[ "$LTO" = 1 ] && CF="$CF -flto"
INC="-I. -Iqpc/include -Iqpc/ports/freertos -Ifw/FreeRTOS/include -Ifw/port_sim -Ifw/FreeRTOS/portable/GCC/ARM_CM0 -Ifw/cmsis/dev -Ifw/cmsis/core"
QP="qep_hsm qep_msm qf_act qf_defer qf_dyn qf_mem qf_ps qf_qact qf_qeq qf_qmact qf_time"
OBJS=""
for f in $QP; do arm-none-eabi-gcc -c $CF $INC qpc/src/qf/$f.c -o $OUT/$f.o; OBJS="$OBJS $OUT/$f.o"; done
arm-none-eabi-gcc -c $CF $INC qpc/ports/freertos/qf_port.c -o $OUT/qf_port.o; OBJS="$OBJS $OUT/qf_port.o"
for f in tasks queue list; do arm-none-eabi-gcc -c $CF $INC fw/FreeRTOS/$f.c -o $OUT/$f.o; OBJS="$OBJS $OUT/$f.o"; done
arm-none-eabi-gcc -c $CF $INC -DSysTick_Handler=xPortSysTickHandler fw/port_sim/port.c -o $OUT/port.o
arm-none-eabi-gcc -c $CF $INC fw/port_sim/portasm.c -o $OUT/portasm.o
for f in ao bsp work; do arm-none-eabi-gcc -c $CF $INC $f.c -o $OUT/$f.o; OBJS="$OBJS $OUT/$f.o"; done
arm-none-eabi-gcc -c -mcpu=cortex-m0plus -mthumb fw/startup_stm32c031xx.s -o $OUT/startup.o
LF="-mcpu=cortex-m0plus -mthumb -specs=nano.specs -Tfw/STM32C031xx_FLASH.ld -lc -lm -lnosys -Wl,-Map=$OUT/probe.map,--cref -Wl,--gc-sections -Wl,--print-memory-usage"
[ "$LTO" = 1 ] && LF="$LF -flto -Os -ffunction-sections -fdata-sections"
arm-none-eabi-gcc $OBJS $OUT/port.o $OUT/portasm.o $OUT/startup.o $LF -o $OUT/probe.elf
arm-none-eabi-size $OUT/probe.elf
