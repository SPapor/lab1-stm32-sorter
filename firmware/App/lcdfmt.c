#include "lcdfmt.h"

/* Один символ на позицію pos, якщо вміщується разом з '\0' */
static size_t put(char *dst, size_t pos, size_t cap, char ch)
{
    if (pos + 1u < cap) {
        dst[pos] = ch;
        dst[pos + 1u] = '\0';
    }
    return pos + 1u;
}

size_t lcdfmt_str(char *dst, size_t pos, size_t cap, const char *s, unsigned width)
{
    size_t start = pos;

    if (pos < cap)
        dst[pos] = '\0';
    for (; *s != '\0'; s++)
        pos = put(dst, pos, cap, *s);
    while (pos - start < width)
        pos = put(dst, pos, cap, ' ');
    return pos;
}

size_t lcdfmt_u32(char *dst, size_t pos, size_t cap, uint32_t v, unsigned width)
{
    char digits[11];   /* 4294967295 + '\0' */
    char *p = &digits[sizeof digits - 1u];

    *p = '\0';
    do {
        *--p = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    return lcdfmt_str(dst, pos, cap, p, width);
}
