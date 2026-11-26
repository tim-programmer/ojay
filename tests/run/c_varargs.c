// C side of c_varargs.jai: variadic functions read with stdarg.
#include <stdarg.h>
#include <stdint.h>

long long sum_ints(int count, ...) {
    va_list ap;
    va_start(ap, count);
    long long total = 0;
    for (int i = 0; i < count; ++i) total += va_arg(ap, int);
    va_end(ap);
    return total;
}

// Writes each argument as text into out, by the letters of `kinds`:
// i int, u unsigned, l long long, d double, p pointer (to a value printed as int), s string.
static char* put_int(char* out, long long v) {
    char buf[24];
    int n = 0;
    unsigned long long u = v < 0 ? (unsigned long long)(-v) : (unsigned long long)v;
    do { buf[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *out++ = '-';
    while (n) *out++ = buf[--n];
    return out;
}

int describe(char* out, const char* kinds, ...) {
    va_list ap;
    va_start(ap, kinds);
    char* start = out;
    for (const char* k = kinds; *k; ++k) {
        if (k != kinds) *out++ = ' ';
        switch (*k) {
            case 'i': out = put_int(out, va_arg(ap, int)); break;
            case 'u': out = put_int(out, va_arg(ap, unsigned)); break;
            case 'l': out = put_int(out, va_arg(ap, long long)); break;
            case 'd': {
                double d = va_arg(ap, double);
                out = put_int(out, (long long)(d * 1000));
                *out++ = 'm';
                break;
            }
            case 'p': out = put_int(out, *va_arg(ap, int*)); break;
            case 's': for (const char* s = va_arg(ap, const char*); *s; ++s) *out++ = *s; break;
        }
    }
    *out = 0;
    va_end(ap);
    return (int)(out - start);
}

// Many arguments: past the argument registers on every target.
double mix(int n, ...) {
    va_list ap;
    va_start(ap, n);
    double total = 0;
    for (int i = 0; i < n; ++i) {
        total += va_arg(ap, double);
        total += va_arg(ap, int);
    }
    va_end(ap);
    return total;
}
