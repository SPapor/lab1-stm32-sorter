#ifndef PERF_H
#define PERF_H

/* Арифметика вимірювань (docs/perf.md, методичка 5.1). Лише логіка: такти
 * приходять ззовні (Platform/perf_hal), тож модуль збирається й тестується на хості.
 *
 * Лічильник тактів 32-бітний і при 48 МГц переповнюється раз на ≈ 89,5 с.
 * Інтервал — беззнакова різниця end - start: вона правильна і через одне
 * переповнення, поки сам інтервал коротший за 2^32 тактів. Довший інтервал
 * «загортається» і стає меншим на 2^32 — такі тут не міряються. */

#include <stdint.h>

/* Статистика одного вимірюваного інтервалу, як у методичці */
typedef struct {
    const char *name;
    uint32_t count;
    uint32_t min, max;   /* такти */
    uint64_t sum;        /* такти; 64 біти — не переповнюється */
} perf_stat_t;

/* Тактів від start до end з урахуванням переповнення 32 біт. */
uint32_t perf_elapsed(uint32_t start, uint32_t end);

/* Такти → мікросекунди з округленням до найближчого; cyc_per_us > 0. */
uint32_t perf_cyc_to_us(uint32_t cyc, uint32_t cyc_per_us);

/* Один вимір cyc тактів. */
void perf_add(perf_stat_t *s, uint32_t cyc);

/* Обнуляє лічильники, назва лишається. */
void perf_stat_reset(perf_stat_t *s);

/* Середнє в тактах з округленням; 0, якщо вимірів не було. */
uint32_t perf_avg(const perf_stat_t *s);

/* num / den і остача без 64-бітного ділення з libgcc (__aeabi_uldivmod +
 * __udivmoddi4 ≈ 470 Б флешу): двійкове ділення зсувом. Частка має
 * вміститися в 32 біти (інакше повертається 0xFFFFFFFF); den > 0; rem може бути NULL. */
uint32_t perf_udiv64(uint64_t num, uint32_t den, uint32_t *rem);

/* ---- Періодична задача (методичка 5.1, приклад 3) --------------------------
 * Джитер — фактичний інтервал між стартами мінус період, мкс (буває від'ємним:
 * HAL_GetTick крокує по 1 мс). Пропуск — інтервал понад 2 періоди.
 * Гістограма джитера з кроком 100 мкс: кошик 0 — нижче за PERF_HIST_LO_US,
 * 1..30 — [-1000, 2000) по 100 мкс, 31 — від 2000 і вище. */
#define PERF_HIST_STEP_US 100
#define PERF_HIST_LO_US   (-1000)
#define PERF_HIST_BINS    32u

typedef struct {
    uint32_t period_us;
    uint32_t t_prev;      /* такти попереднього старту */
    uint8_t has_prev;
    uint32_t n;           /* виміряних інтервалів */
    int32_t jit_min_us;
    int32_t jit_max_us;
    uint32_t missed;
    uint32_t hist[PERF_HIST_BINS];
} perf_period_t;

void perf_period_init(perf_period_t *p, uint32_t period_us);

/* Обнуляє статистику; наступний старт знову перший (без інтервалу). */
void perf_period_reset(perf_period_t *p);

/* Старт задачі в момент now (такти). */
void perf_period_start(perf_period_t *p, uint32_t now, uint32_t cyc_per_us);

/* Номер кошика гістограми для джитера jitter_us. */
unsigned perf_hist_bin(int32_t jitter_us);

/* Нижня межа кошика bin, мкс (для кошика 0 — INT32_MIN). */
int32_t perf_hist_lo_us(unsigned bin);

/* ---- Завантаження CPU (методичка 5.1, приклад 4) ---------------------------
 * Сума тактів, коли задачі справді працювали, у вікні фіксованої довжини.
 * Задача, що лише перевірила свій час, сюди не додається. */
typedef struct {
    uint32_t win_cyc;     /* мінімальна довжина вікна, тактів (1 с = SystemCoreClock) */
    uint32_t start;
    uint8_t open;
    uint32_t busy;        /* такти роботи в поточному вікні */
    uint32_t windows;     /* закритих вікон */
    uint32_t last_win;    /* фактична довжина останнього вікна, тактів */
    uint32_t last_busy;
    uint32_t last_pct;    /* busy / вікно, %, з округленням */
    uint32_t last_pm;     /* те саме, ‰ */
} perf_load_t;

void perf_load_init(perf_load_t *l, uint32_t win_cyc);

/* cyc тактів роботи задачі. */
void perf_load_busy(perf_load_t *l, uint32_t cyc);

/* Викликати щопроходу циклу. Перший виклик відкриває вікно; коли від його
 * початку минуло ≥ win_cyc, вікно закривається (last_*) і повертається 1. */
int perf_load_poll(perf_load_t *l, uint32_t now);

#endif /* PERF_H */
