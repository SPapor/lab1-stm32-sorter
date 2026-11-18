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

/* Звідки беруться аргументи: va_list або масив fmt_arg_t (запис черги журналу).
 * rec - якщо не NULL, кожен узятий з va_list аргумент ще й пишеться туди (fmt_vargs) */
typedef struct {
    va_list *ap;
    const fmt_arg_t *arr;
    unsigned n;
    unsigned i;
    fmt_arg_t *rec;
    unsigned rec_max;
} fmt_src_t;

static fmt_arg_t src_next(fmt_src_t *s, fmt_arg_t v)
{
    if (s->rec != NULL && s->i < s->rec_max)
        s->rec[s->i] = v;
    s->i++;
    return v;
}

/* У масиві відсутній аргумент - 0 (для %s - "(null)") */
static fmt_arg_t src_arr(fmt_src_t *s)
{
    fmt_arg_t v = (s->i < s->n) ? s->arr[s->i] : 0u;

    s->i++;
    return v;
}

static long arg_signed(fmt_src_t *s, int is_long)
{
    if (s->ap == NULL) {
        fmt_arg_t v = src_arr(s);
        return is_long ? (long)(intptr_t)v : (long)(int)(intptr_t)v;
    }
    long v = is_long ? va_arg(*s->ap, long) : (long)va_arg(*s->ap, int);
    src_next(s, (fmt_arg_t)(intptr_t)v);
    return v;
}

static unsigned long arg_unsigned(fmt_src_t *s, int is_long)
{
    if (s->ap == NULL) {
        fmt_arg_t v = src_arr(s);
        return is_long ? (unsigned long)v : (unsigned long)(unsigned)v;
    }
    unsigned long v = is_long ? va_arg(*s->ap, unsigned long) : (unsigned long)va_arg(*s->ap, unsigned);
    src_next(s, (fmt_arg_t)v);
    return v;
}

static const char *arg_str(fmt_src_t *s)
{
    if (s->ap == NULL)
        return (const char *)src_arr(s);
    const char *v = va_arg(*s->ap, const char *);
    src_next(s, (fmt_arg_t)v);
    return v;
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

static size_t fmt_core(char *dst, size_t cap, const char *fmt, fmt_src_t *src)
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
            long sv = arg_signed(src, is_long);
            /* -LONG_MIN не вміщується в long, тому модуль - у беззнаковому */
            v = (sv < 0) ? 0u - (unsigned long)sv : (unsigned long)sv;
            n = digits(end, v, 10u, 0);
            field(&o, (sv < 0) ? "-" : NULL, end - n, n, width, left, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
            v = arg_unsigned(src, is_long);
            n = digits(end, v, (*fmt == 'u') ? 10u : 16u, *fmt == 'X');
            field(&o, NULL, end - n, n, width, left, zero);
            break;
        case 'c':
            num[0] = (char)arg_signed(src, 0);
            field(&o, NULL, num, 1u, width, left, 0);
            break;
        case 's': {
            const char *s = arg_str(src);
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

size_t fmt_vbuf(char *dst, size_t cap, const char *fmt, va_list ap)
{
    va_list cp;
    size_t n;

    /* копія: адресу параметра va_list брати не можна (на x86-64 це вказівник) */
    va_copy(cp, ap);
    fmt_src_t src = { &cp, NULL, 0u, 0u, NULL, 0u };
    n = fmt_core(dst, cap, fmt, &src);
    va_end(cp);
    return n;
}

size_t fmt_abuf(char *dst, size_t cap, const char *fmt, const fmt_arg_t *args, unsigned n)
{
    fmt_src_t src = { NULL, args, n, 0u, NULL, 0u };

    return fmt_core(dst, cap, fmt, &src);
}

unsigned fmt_vargs(const char *fmt, va_list ap, fmt_arg_t *out, unsigned max)
{
    va_list cp;
    char none;

    va_copy(cp, ap);
    fmt_src_t src = { &cp, NULL, 0u, 0u, out, max };
    (void)fmt_core(&none, 0u, fmt, &src);
    va_end(cp);
    return src.i;
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
