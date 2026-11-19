#ifndef RTOS_OSM_H
#define RTOS_OSM_H


#include <stdint.h>
#include "perf.h"

typedef enum {
    OSM_TICK = 0,
    OSM_SELECT,
    OSM_CTXSW,
    OSM_ISR_ESTOP,
    OSM_ISR_CLI,
    OSM_QUEUE,
    OSM_MUTEX,
    OSM_COUNT
} rtos_osm_id_t;

typedef struct {
    uint32_t cyc;
    uint32_t sim;
} rtos_osm_mark_t;

void rtos_osm_init(void);
void rtos_osm_stamp(rtos_osm_mark_t *m);
void rtos_osm_add(rtos_osm_id_t id, const rtos_osm_mark_t *m);

void rtos_osm_set(rtos_osm_id_t id);
int rtos_osm_take(rtos_osm_id_t id);
void rtos_osm_clear(rtos_osm_id_t id);
void rtos_osm_switch_out(void);

void rtos_osm_stats(rtos_osm_id_t id, const perf_stat_t **raw, const perf_stat_t **nosim);
void rtos_osm_reset(void);

#endif
