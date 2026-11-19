#include "rtos_osm.h"
#ifdef OSMETRICS
#include "perf_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32c0xx_hal.h"
#if SIM_CRIT
#include "port_sim.h"
#endif

#define SIM_TASK_PAIR_CYC  694u
#define SIM_ISR_PAIR_CYC   689u

static perf_stat_t st_raw[OSM_COUNT];
static perf_stat_t st_nosim[OSM_COUNT];
static rtos_osm_mark_t slot[OSM_COUNT];
static volatile uint8_t slot_on[OSM_COUNT];

static const char *const name_raw[OSM_COUNT] = {
    [OSM_TICK] = "os_tick", [OSM_SELECT] = "os_select", [OSM_CTXSW] = "os_ctxsw",
    [OSM_ISR_ESTOP] = "os_isr_estop", [OSM_ISR_CLI] = "os_isr_cli",
    [OSM_QUEUE] = "os_queue", [OSM_MUTEX] = "os_mutex",
};
static const char *const name_nosim[OSM_COUNT] = {
    [OSM_TICK] = "os_tick_nosim", [OSM_SELECT] = "",
    [OSM_CTXSW] = "os_ctxsw_nosim", [OSM_ISR_ESTOP] = "os_isr_estop_nosim",
    [OSM_ISR_CLI] = "os_isr_cli_nosim", [OSM_QUEUE] = "os_queue_nosim",
    [OSM_MUTEX] = "os_mutex_nosim",
};

static uint32_t sim_cost(void)
{
#if SIM_CRIT
    port_sim_counters_t c;

    port_sim_counters(&c);
    return c.crit_task_n * SIM_TASK_PAIR_CYC + c.crit_isr_n * SIM_ISR_PAIR_CYC;
#else
    return 0u;
#endif
}

void rtos_osm_init(void)
{
    for (unsigned i = 0; i < OSM_COUNT; i++) {
        st_raw[i].name = name_raw[i];
        st_nosim[i].name = name_nosim[i];
    }
    rtos_osm_reset();
}

void rtos_osm_stamp(rtos_osm_mark_t *m)
{
    m->sim = sim_cost();
    m->cyc = perf_now();
}

void rtos_osm_add(rtos_osm_id_t id, const rtos_osm_mark_t *m)
{
    uint32_t cyc = perf_now() - m->cyc;
    uint32_t sim = sim_cost() - m->sim;

    perf_add(&st_raw[id], cyc);
    perf_add(&st_nosim[id], cyc > sim ? cyc - sim : 0u);
}

void rtos_osm_set(rtos_osm_id_t id)
{
    if (!slot_on[id]) {
        rtos_osm_stamp(&slot[id]);
        slot_on[id] = 1;
    }
}

int rtos_osm_take(rtos_osm_id_t id)
{
    if (!slot_on[id])
        return 0;
    rtos_osm_add(id, &slot[id]);
    slot_on[id] = 0;
    return 1;
}

void rtos_osm_clear(rtos_osm_id_t id)
{
    slot_on[id] = 0;
}

static uint32_t sel_val;
static volatile uint8_t sel_on;

void rtos_osm_switch_out(void)
{
    sel_val = SysTick->VAL;
    sel_on = 1;
}

void rtos_osm_switch_in(void)
{
    uint32_t v;

    if (!sel_on)
        return;
    v = SysTick->VAL;
    sel_on = 0;
    perf_add(&st_raw[OSM_SELECT], sel_val >= v ? sel_val - v : sel_val + SysTick->LOAD + 1u - v);
}

void rtos_osm_stats(rtos_osm_id_t id, const perf_stat_t **raw, const perf_stat_t **nosim)
{
    *raw = &st_raw[id];
    *nosim = (id == OSM_SELECT) ? NULL : &st_nosim[id];
}

void rtos_osm_reset(void)
{
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < OSM_COUNT; i++) {
        perf_stat_reset(&st_raw[i]);
        perf_stat_reset(&st_nosim[i]);
        slot_on[i] = 0;
    }
    taskEXIT_CRITICAL();
}
#endif
