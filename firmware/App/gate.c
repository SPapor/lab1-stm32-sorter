#include "gate.h"

void gate_init(gate_t *g, uint16_t on_raw, uint16_t off_raw)
{
    g->on_raw = on_raw;
    g->off_raw = off_raw;
    g->present = 0;
}

gate_event_t gate_update(gate_t *g, uint16_t raw)
{
    if (!g->present && raw >= g->on_raw) {
        g->present = 1;
        return GATE_EV_IN;
    }
    if (g->present && raw <= g->off_raw) {
        g->present = 0;
        return GATE_EV_OUT;
    }
    return GATE_EV_NONE;
}
