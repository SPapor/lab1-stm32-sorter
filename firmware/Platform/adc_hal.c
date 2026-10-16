#include "adc_hal.h"
#include "board.h"

uint16_t adc_hal_read(uint32_t channel)
{
    /* ScanConvMode = ADC_SCAN_SEQ_FIXED: ConfigChannel лише додає канал (OR) у
     * CHSELR (MX_ADC1_Init уже ввімкнув CH0|CH1), тому всі інші канали явно
     * прибираємо через ADC_RANK_NONE, щоб у послідовності лишився рівно один. */
    static const uint32_t used[] = { BOARD_ADC_CH_GATE, BOARD_ADC_CH_CLASS };
    ADC_ChannelConfTypeDef c = {0};

    c.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    for (unsigned i = 0; i < sizeof used / sizeof used[0]; i++) {
        c.Channel = used[i];
        c.Rank = (used[i] == channel) ? ADC_RANK_CHANNEL_NUMBER : ADC_RANK_NONE;
        HAL_ADC_ConfigChannel(BOARD_ADC, &c);
    }

    /* Маркер для логічного аналізатора (методичка 4.3, рядок ADC): ширина
     * імпульсу на D5 = старт + перетворення + читання результату. */
    HAL_GPIO_WritePin(BOARD_ADC_MARK_PORT, BOARD_ADC_MARK_PIN, GPIO_PIN_SET);
    HAL_ADC_Start(BOARD_ADC);
    HAL_ADC_PollForConversion(BOARD_ADC, ADC_HAL_TIMEOUT_MS);
    uint16_t v = (uint16_t)HAL_ADC_GetValue(BOARD_ADC);
    HAL_GPIO_WritePin(BOARD_ADC_MARK_PORT, BOARD_ADC_MARK_PIN, GPIO_PIN_RESET);

    HAL_ADC_Stop(BOARD_ADC);
    return v;
}
