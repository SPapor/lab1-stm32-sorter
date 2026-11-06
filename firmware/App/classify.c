#include "classify.h"

parcel_class_t classify(uint16_t raw)
{
    if (raw == 0u || raw >= CLASS_FULL_RAW)
        return PARCEL_UNKNOWN;
    if (raw <= CLASS_A_MAX_RAW)
        return PARCEL_A;
    if (raw >= CLASS_B_MIN_RAW)
        return PARCEL_B;
    return PARCEL_UNKNOWN;
}

const char *classify_name(parcel_class_t c)
{
    switch (c) {
    case PARCEL_A: return "A";
    case PARCEL_B: return "B";
    default:       return "UNKNOWN";
    }
}
