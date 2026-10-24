/* Decimal-mode ADC/SBC shared by the oracle (tools/cpu816/cpu816.c) and the translated code, so both do exactly the same thing.
   n = number of BCD digits (2 for 8-bit, 4 for 16-bit). Flags: C = decimal carry/no-borrow, V from the binary operation, N/Z from the result. */
#ifndef BCD_H
#define BCD_H
#include <stdint.h>

static inline uint32_t bcd_add(uint32_t a, uint32_t v, int cin, int n, int *cout, int *vout)
{
    /* the 65816 takes V from the sum after the lower digits are decimal-adjusted but before the top digit is */
    uint32_t res = 0, sign = 1u << (4 * n - 1), mask = (1u << (4 * n)) - 1, pre = 0;
    int c = cin, i;
    for (i = 0; i < n; i++) {
        int d = (int)((a >> (4 * i)) & 15) + (int)((v >> (4 * i)) & 15) + c;
        if (i == n - 1) pre = (pre + ((uint32_t)d << (4 * i))) & mask;
        if (d > 9) { d = (d + 6) & 15; c = 1; } else c = 0;
        res |= (uint32_t)d << (4 * i);
        if (i < n - 1) pre |= (uint32_t)d << (4 * i);
    }
    *cout = c; *vout = (~(a ^ v) & (a ^ pre) & sign) != 0;
    return res & mask;
}

static inline uint32_t bcd_sub(uint32_t a, uint32_t v, int cin, int n, int *cout, int *vout)
{
    uint32_t res = 0, sign = 1u << (4 * n - 1), mask = (1u << (4 * n)) - 1, bin = (a - v - (1 - cin)) & mask;
    int c = cin, i;
    for (i = 0; i < n; i++) {
        int d = (int)((a >> (4 * i)) & 15) - (int)((v >> (4 * i)) & 15) - (1 - c);
        if (d < 0) { d = (d - 6) & 15; c = 0; } else c = 1;
        res |= (uint32_t)d << (4 * i);
    }
    *cout = c; *vout = ((a ^ v) & (a ^ bin) & sign) != 0;
    return res & mask;
}
#endif
