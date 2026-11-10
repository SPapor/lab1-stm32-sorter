#include "sorter.h"
#include <stddef.h>

#define ANGLE_A       0u
#define ANGLE_B       90u
#define ANGLE_REJECT  180u

void sorter_init(sorter_t *s, int fault)
{
    s->state = fault ? SORTER_FAULT : SORTER_READY;
    s->impact = 0;
    sorter_reset_counters(s);
}

sorter_bin_t sorter_route(parcel_class_t cls, uint8_t impact)
{
    if (impact)
        return BIN_REJECT;
    switch (cls) {
    case PARCEL_A: return BIN_A;
    case PARCEL_B: return BIN_B;
    default:       return BIN_REJECT;
    }
}

uint8_t sorter_bin_angle(sorter_bin_t bin)
{
    switch (bin) {
    case BIN_A: return ANGLE_A;
    case BIN_B: return ANGLE_B;
    default:    return ANGLE_REJECT;
    }
}

int sorter_parcel_in(sorter_t *s, parcel_class_t cls, sorter_decision_t *out)
{
    if (s->state != SORTER_READY)
        return 0;

    out->impact = s->impact;
    out->bin = sorter_route(cls, s->impact);
    out->angle = sorter_bin_angle(out->bin);
    s->count[out->bin]++;
    out->n = sorter_total(s);
    s->impact = 0;
    s->state = SORTER_SORTING;
    return 1;
}

void sorter_parcel_out(sorter_t *s)
{
    if (s->state == SORTER_SORTING)
        s->state = SORTER_READY;
}

int sorter_impact(sorter_t *s)
{
    if (s->state != SORTER_READY && s->state != SORTER_SORTING)
        return 0;
    if (s->impact)
        return 0;
    s->impact = 1;
    return 1;
}

sorter_state_t sorter_estop_toggle(sorter_t *s)
{
    switch (s->state) {
    case SORTER_READY:
    case SORTER_SORTING:
        s->state = SORTER_ESTOP;
        break;
    case SORTER_ESTOP:
        s->state = SORTER_READY;
        break;
    default:
        break;   /* FAULT */
    }
    return s->state;
}

void sorter_reset_counters(sorter_t *s)
{
    for (size_t i = 0; i < BIN_COUNT; i++)
        s->count[i] = 0;
}

uint32_t sorter_total(const sorter_t *s)
{
    return s->count[BIN_A] + s->count[BIN_B] + s->count[BIN_REJECT];
}

const char *sorter_state_name(sorter_state_t st)
{
    switch (st) {
    case SORTER_READY:   return "READY";
    case SORTER_SORTING: return "SORTING";
    case SORTER_ESTOP:   return "ESTOP";
    default:             return "FAULT";
    }
}

const char *sorter_bin_name(sorter_bin_t bin)
{
    switch (bin) {
    case BIN_A: return "A";
    case BIN_B: return "B";
    default:    return "REJECT";
    }
}
