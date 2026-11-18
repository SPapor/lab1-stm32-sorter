#include "fmt.h"

/* Вихідний буфер: символ пишеться, лише якщо поміщається разом з '\0',
 * а pos рахує всі символи, як у vsnprintf */
typedef struct {
    char *dst;
    size_t cap;
    size_t pos;
} fmt_out_t;

static void put(fmt_out_t *o, char ch)
{
    if (o->pos + 1u < o->cap)
        o->dst[o->pos] = ch;
    o->pos++;
}

static void put_n(fmt_out_t *o, char ch, unsigned n)
{
    while (n-- > 0u)
        put(o, ch);
}

/* Поле: префікс (знак) + тіло len символів, доповнене до width.
 * Нулі йдуть між знаком і цифрами, пробіли - зліва або справа ('-') */
static void field(fmt_out_t *o, const char *sign, const char *body, unsigned len,
                  unsigned width, int left, int zero)
{
    unsigned sign_len = (sign != NULL) ? 1u : 0u;
    unsigned fill = (width > len + sign_len) ? width - len - sign_len : 0u;

    if (!left && !zero)
        put_n(o, ' ', fill);
    if (sign != NULL)
        put(o, *sign);
    if (!left && zero)
        put_n(o, '0', fill);
    for (unsigned i = 0; i < len; i++)
        put(o, body[i]);
    if (left)
        put_n(o, ' ', fill);
}

/* Цифри v у системі base з кінця буфера; повертає кількість */
static unsigned digits(char *end, unsigned long v, unsigned base, int upper)
{
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned n = 0;

    do {
        *--end = set[v % base];
        v /= base;
        n++;
    } while (v != 0u);
    return n;
}

size_t fmt_vbuf(char *dst, size_t cap, const char *fmt, va_list ap)
{
    fmt_out_t o = { dst, cap, 0u };

    for (; *fmt != '\0'; fmt++) {
        int left = 0;
        int zero = 0;
        int is_long = 0;
        unsigned width = 0;
        const char *spec = fmt;

        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                zero = 1;
            else
                break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10u + (unsigned)(*fmt++ - '0');
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        char num[24];   /* 64-бітний long у десятковому вигляді - 20 цифр */
        char *end = &num[sizeof num];
        unsigned long v;
        unsigned n;

        switch (*fmt) {
        case 'd':
        case 'i': {
            long sv = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            /* -LONG_MIN не вміщується в long, тому модуль - у беззнаковому */
            v = (sv < 0) ? 0u - (unsigned long)sv : (unsigned long)sv;
            n = digits(end, v, 10u, 0);
            field(&o, (sv < 0) ? "-" : NULL, end - n, n, width, left, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
            v = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            n = digits(end, v, (*fmt == 'u') ? 10u : 16u, *fmt == 'X');
            field(&o, NULL, end - n, n, width, left, zero);
            break;
        case 'c':
            num[0] = (char)va_arg(ap, int);
            field(&o, NULL, num, 1u, width, left, 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            unsigned len = 0;

            if (s == NULL)
                s = "(null)";
            while (s[len] != '\0')
                len++;
            field(&o, NULL, s, len, width, left, 0);
            break;
        }
        case '%':
            put(&o, '%');
            break;
        default:
            /* Невідомий специфікатор (або кінець рядка після '%') - як є */
            for (; spec < fmt; spec++)
                put(&o, *spec);
            if (*fmt == '\0')
                fmt--;
            else
                put(&o, *fmt);
            break;
        }
    }
    if (cap > 0u)
        dst[(o.pos < cap) ? o.pos : cap - 1u] = '\0';
    return o.pos;
}

size_t fmt_buf(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    n = fmt_vbuf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}
