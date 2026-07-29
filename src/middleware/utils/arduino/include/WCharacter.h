/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  WCharacter.h - Character classification functions for Wiring & Arduino
  Copyright (c) 2010 David A. Mellis.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef WCHARACTER_H
#define WCHARACTER_H

#include <ctype.h>

// Standard Arduino character classification wrappers (thin ctype.h shims).
// All are inline — no .cpp needed.

#ifdef isAlpha
#undef isAlpha
#endif
#define isAlpha(c)              isalpha((int)(unsigned char)(c))

#ifdef isAlphaNumeric
#undef isAlphaNumeric
#endif
#define isAlphaNumeric(c)       isalnum((int)(unsigned char)(c))

#ifdef isAscii
#undef isAscii
#endif
#define isAscii(c)              (((unsigned char)(c)) <= 127)

#ifdef isWhitespace
#undef isWhitespace
#endif
#define isWhitespace(c)         (((c) == ' ') || ((c) == '\t'))

#ifdef isControl
#undef isControl
#endif
#define isControl(c)            iscntrl((int)(unsigned char)(c))

#ifdef isDigit
#undef isDigit
#endif
#define isDigit(c)              isdigit((int)(unsigned char)(c))

#ifdef isGraph
#undef isGraph
#endif
#define isGraph(c)              isgraph((int)(unsigned char)(c))

#ifdef isHexadecimalDigit
#undef isHexadecimalDigit
#endif
#define isHexadecimalDigit(c)   isxdigit((int)(unsigned char)(c))

// Alias: historical / non-official name, kept so code already written against
// isHexDigit still compiles. New code should use the official isHexadecimalDigit.
#ifndef isHexDigit
#define isHexDigit(c)           isHexadecimalDigit(c)
#endif

#ifdef isLowerCase
#undef isLowerCase
#endif
#define isLowerCase(c)          islower((int)(unsigned char)(c))

#ifdef isPrintable
#undef isPrintable
#endif
#define isPrintable(c)          isprint((int)(unsigned char)(c))

#ifdef isPunct
#undef isPunct
#endif
#define isPunct(c)              ispunct((int)(unsigned char)(c))

#ifdef isSpace
#undef isSpace
#endif
#define isSpace(c)              isspace((int)(unsigned char)(c))

#ifdef isUpperCase
#undef isUpperCase
#endif
#define isUpperCase(c)          isupper((int)(unsigned char)(c))

#ifdef isXDigit
#undef isXDigit
#endif
#define isXDigit(c)             isxdigit((int)(unsigned char)(c))

#ifdef toAscii
#undef toAscii
#endif
#define toAscii(c)              ((c) & 0x7F)

#ifdef toLowerCase
#undef toLowerCase
#endif
#define toLowerCase(c)          tolower((int)(unsigned char)(c))

#ifdef toUpperCase
#undef toUpperCase
#endif
#define toUpperCase(c)          toupper((int)(unsigned char)(c))

#endif // WCHARACTER_H
