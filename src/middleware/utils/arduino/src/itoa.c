/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  itoa.c - Standard Arduino integer-to-ASCII conversion functions.

  Semantics (Arduino-standard):
    - base 2..16, digits 0-9a-f (lowercase); other bases yield "".
    - itoa()/ltoa(): for base 10, negative values get a leading '-'; for other
      bases the value is treated as unsigned (no sign), matching Arduino.
    - utoa()/ultoa(): unsigned input, never sign.
    - INT_MIN / LONG_MIN are handled correctly by negating as unsigned long.
    - Each function returns the destination pointer.
*/

#include "itoa.h"

/* Build an unsigned magnitude into str (radix 2..16). */
static char *u2a(unsigned long value, char *str, int radix)
{
    char tmp[32];
    char *tp = tmp;
    char *sp = str;
    unsigned int i;

    if (radix < 2 || radix > 16) {
        str[0] = '\0';
        return str;
    }
    do {
        i = (unsigned int)(value % (unsigned long)radix);
        value /= (unsigned long)radix;
        *tp++ = (char)(i < 10 ? ('0' + i) : ('a' - 10 + i));
    } while (value);
    while (tp > tmp) {
        *sp++ = *--tp;
    }
    *sp = '\0';
    return str;
}

char *itoa(int value, char *str, int radix)
{
    if (radix == 10 && value < 0) {
        /* Negate as unsigned so INT_MIN is correct: -(unsigned long)value
         * yields the true magnitude even where -value would overflow. */
        str[0] = '-';
        (void)u2a(-(unsigned long)value, str + 1, radix);
        return str;
    }
    return u2a((unsigned long)(unsigned int)value, str, radix);
}

char *ltoa(long value, char *str, int radix)
{
    if (radix == 10 && value < 0) {
        str[0] = '-';
        (void)u2a(-(unsigned long)value, str + 1, radix);
        return str;
    }
    return u2a((unsigned long)value, str, radix);
}

char *utoa(unsigned int value, char *str, int radix)
{
    return u2a((unsigned long)value, str, radix);
}

char *ultoa(unsigned long value, char *str, int radix)
{
    return u2a(value, str, radix);
}
