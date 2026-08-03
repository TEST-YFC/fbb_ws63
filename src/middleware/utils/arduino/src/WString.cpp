/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/*
  WString.cpp - String class for Arduino
  Copyright (c) 2009-10 Hernando Barragan. All rights reserved.

  Hisilicon Adaptation: Direct SDK call implementation with LiteOS heap
*/

#include "WString.h"
#include <stdio.h>
#include <math.h>
#include <securec.h>

// Default capacity for new strings
#define STRING_DEFAULT_CAPACITY 16
#define STRING_EXPAND_FACTOR 2
#define STRING_ITOA_BUF_SIZE_CHAR 2
#define STRING_ITOA_BUF_SIZE_INT 12
#define STRING_ITOA_BUF_SIZE_UINT 11
#define STRING_ITOA_BUF_SIZE_LONG 34
#define STRING_ITOA_BUF_SIZE_ULONG 33
#define STRING_FLOAT_BUF_SIZE 32
#define STRING_FLOAT_PRECISION 6
#define STRING_FLOAT_MIN_ABS 0.000001f
#define STRING_FLOAT_SCALE 1000000
#define STRING_DECIMAL_BASE 10
#define STRING_FLOAT_ZERO_STR "0.000000"
#define STRING_DIGITS "0123456789abcdef"
#define BIN_LEN 2
#define HEX_LEN 16

// Helper function to convert numbers to strings
static char *itoa_long(long num, char *str, int base)
{
    if (base < BIN_LEN || base > HEX_LEN) {
        str[0] = '\0';
        return str;
    }
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    unsigned long abs_num;
    int negative = 0;

    if (num < 0 && base == STRING_DECIMAL_BASE) {
        negative = 1;
        abs_num = -num;
    } else {
        abs_num = num;
    }

    do {
        *ptr++ = STRING_DIGITS[abs_num % base];
        abs_num /= base;
    } while (abs_num > 0);

    if (negative != 0) {
        *ptr++ = '-';
    }
    *ptr-- = '\0';

    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

// Constructors ///////////////////////////////////////////////////////////////

String::String()
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
}

String::String(const char *cstr)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    if (cstr) {
        _len = strlen(cstr);
        _capacity = _len + 1;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            memcpy_s(_buffer, _capacity, cstr, _len + 1);
        } else {
            invalidate();
        }
    }
}

String::String(const char *cstr, unsigned int length)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    if (cstr && length > 0) {
        _len = length;
        _capacity = _len + 1;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            (void)memcpy_s(_buffer, _capacity, cstr, _len);
            _buffer[_len] = '\0';
        } else {
            invalidate();
        }
    }
}

String::String(const String &str)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    if (str._buffer) {
        _len = str._len;
        _capacity = str._capacity;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            (void)memcpy_s(_buffer, _capacity, str._buffer, _len + 1);
        } else {
            invalidate();
        }
    }
}

String::String(String &&rval)
{
    // Steal the heap buffer from rval, leaving rval in a valid empty state.
    _buffer = rval._buffer;
    _capacity = rval._capacity;
    _len = rval._len;
    rval._buffer = NULL;
    rval._capacity = 0;
    rval._len = 0;
}

String::String(char c)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    _len = 1;
    _capacity = STRING_ITOA_BUF_SIZE_CHAR;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        _buffer[0] = c;
        _buffer[1] = '\0';
    } else {
        invalidate();
    }
}

String::String(int num, int base)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;

    // Validate base (must be between 2 and 16), use decimal as default
    if (base < BIN_LEN || base > HEX_LEN) {
        base = STRING_DECIMAL_BASE;
    }

    char buf[STRING_ITOA_BUF_SIZE_INT];
    itoa_long(num, buf, base);

    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::String(unsigned int num, int base)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;

    // Validate base (must be between 2 and 16), use decimal as default
    if (base < BIN_LEN || base > HEX_LEN) {
        base = STRING_DECIMAL_BASE;
    }

    char buf[STRING_ITOA_BUF_SIZE_UINT];
    itoa_long((long)num, buf, base);

    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::String(long num, int base)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;

    // Validate base (must be between 2 and 16), use decimal as default
    if (base < BIN_LEN || base > HEX_LEN) {
        base = STRING_DECIMAL_BASE;
    }

    char buf[STRING_ITOA_BUF_SIZE_LONG];
    itoa_long(num, buf, base);

    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::String(unsigned long num, int base)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;

    // Validate base (must be between 2 and 16), use decimal as default
    if (base < BIN_LEN || base > HEX_LEN) {
        base = STRING_DECIMAL_BASE;
    }

    char buf[STRING_ITOA_BUF_SIZE_ULONG];
    itoa_long((unsigned long)num, buf, base);

    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::String(float num, int digits)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    char buf[STRING_FLOAT_BUF_SIZE];
    (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%.*f", digits, (double)num);
    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::String(double num, int digits)
{
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
    char buf[STRING_FLOAT_BUF_SIZE];
    (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%.*f", digits, num);
    _len = strlen(buf);
    _capacity = _len + 1;
    _buffer = (char *)malloc(_capacity);
    if (_buffer) {
        (void)memcpy_s(_buffer, _capacity, buf, _len + 1);
    } else {
        invalidate();
    }
}

String::~String()
{
    if (_buffer) {
        free(_buffer);
    }
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
}

// Memory Management //////////////////////////////////////////////////////////

void String::invalidate()
{
    if (_buffer) {
        free(_buffer);
    }
    _buffer = NULL;
    _len = 0;
    _capacity = 0;
}

bool String::reserve(unsigned int size)
{
    if (_capacity >= size)
        return true;

    char *newBuffer = (char *)malloc(size);
    if (newBuffer) {
        if (_buffer) {
            (void)memcpy_s(newBuffer, size, _buffer, _len + 1);
            free(_buffer);
        } else {
            newBuffer[0] = '\0';
        }
        _buffer = newBuffer;
        _capacity = size;
        return true;
    }
    return false;
}

unsigned int String::capacity() const
{
    return _capacity;
}

// Basic Operations ///////////////////////////////////////////////////////////

unsigned int String::length() const
{
    return _len;
}

char String::charAt(unsigned int index) const
{
    if (index >= _len || !_buffer)
        return '\0';
    return _buffer[index];
}

void String::setCharAt(unsigned int index, char c)
{
    if (index < _len && _buffer) {
        _buffer[index] = c;
    }
}

const char *String::c_str() const
{
    if (!_buffer)
        return "";
    return _buffer;
}

char *String::buffer() const
{
    return _buffer;
}

void String::getBytes(unsigned char *buf, unsigned int bufsize, unsigned int index) const
{
    if (buf == NULL || bufsize == 0) {
        return;
    }
    // Arduino contract: output is always NUL-terminated.
    buf[0] = '\0';
    if (_buffer == NULL || index >= _len) {
        return;
    }
    unsigned int avail = _len - index;
    unsigned int toCopy = (avail < bufsize - 1) ? avail : (bufsize - 1);
    (void)memcpy_s(buf, bufsize, _buffer + index, toCopy);
    buf[toCopy] = '\0';
}

// Concatenation //////////////////////////////////////////////////////////////

String &String::concat(const String &str)
{
    if (!str._buffer)
        return *this;
    return concat(str._buffer);
}

String &String::concat(const char *cstr)
{
    if (!cstr)
        return *this;

    unsigned int slen = strlen(cstr);
    if (slen == 0)
        return *this;

    if (!_buffer) {
        _len = slen;
        _capacity = slen + 1;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            (void)memcpy_s(_buffer, _capacity, cstr, slen + 1);
        } else {
            invalidate();
        }
        return *this;
    }

    if (_len + slen + 1 > _capacity) {
        unsigned int newCapacity = _capacity;
        while (newCapacity < _len + slen + 1) {
            newCapacity *= STRING_EXPAND_FACTOR;
        }
        if (!reserve(newCapacity)) {
            return *this; // Allocation failed
        }
    }

    if (memcpy_s(_buffer + _len, _capacity - _len, cstr, slen + 1) != EOK) {
        return *this;
    }
    _len += slen;
    return *this;
}

String &String::concat(const char *cstr, unsigned int length)
{
    if (!cstr || length == 0)
        return *this;

    if (!_buffer) {
        _len = length;
        _capacity = _len + 1;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            (void)memcpy_s(_buffer, _capacity, cstr, _len);
            _buffer[_len] = '\0';
        } else {
            invalidate();
        }
        return *this;
    }

    if (_len + length + 1 > _capacity) {
        unsigned int newCapacity = _capacity;
        while (newCapacity < _len + length + 1) {
            newCapacity *= STRING_EXPAND_FACTOR;
        }
        if (!reserve(newCapacity)) {
            return *this; // Allocation failed
        }
    }

    if (memcpy_s(_buffer + _len, _capacity - _len, cstr, length) != EOK) {
        return *this;
    }
    _len += length;
    _buffer[_len] = '\0';
    return *this;
}

String &String::concat(char c)
{
    char buf[STRING_ITOA_BUF_SIZE_CHAR] = {c, '\0'};
    return concat(buf);
}

String &String::concat(unsigned char num)
{
    return concat((int)num);
}

String &String::concat(int num)
{
    char buf[STRING_ITOA_BUF_SIZE_INT];
    itoa_long(num, buf, STRING_DECIMAL_BASE);
    return concat(buf);
}

String &String::concat(unsigned int num)
{
    char buf[STRING_ITOA_BUF_SIZE_UINT];
    unsigned long n = num;
    char *ptr = buf;
    do {
        *ptr++ = STRING_DIGITS[n % STRING_DECIMAL_BASE];
        n /= STRING_DECIMAL_BASE;
    } while (n);
    *ptr = '\0';

    // Reverse
    char *start = buf;
    char *end = ptr - 1;
    while (start < end) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }

    return concat(buf);
}

String &String::concat(long num)
{
    char buf[STRING_ITOA_BUF_SIZE_LONG];
    itoa_long(num, buf, STRING_DECIMAL_BASE);
    return concat(buf);
}

String &String::concat(unsigned long num)
{
    char buf[STRING_ITOA_BUF_SIZE_ULONG];
    unsigned long n = num;
    char *ptr = buf;
    do {
        *ptr++ = STRING_DIGITS[n % STRING_DECIMAL_BASE];
        n /= STRING_DECIMAL_BASE;
    } while (n);
    *ptr = '\0';

    // Reverse
    char *start = buf;
    char *end = ptr - 1;
    while (start < end) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }

    return concat(buf);
}

String &String::concat(float num)
{
    char buf[STRING_FLOAT_BUF_SIZE];
    // Use integer formatting to avoid %f issues with embedded printf
    // Convert to integer representation with 6 decimal places
    // Avoid floating point equality comparison by checking absolute value
    if (fabs(num) < STRING_FLOAT_MIN_ABS) {
        (void)strcpy_s(buf, sizeof(buf), STRING_FLOAT_ZERO_STR);
    } else {
        int int_part = (int)num;
        int frac_part = (int)((num - int_part) * STRING_FLOAT_SCALE);
        if (frac_part < 0)
            frac_part = -frac_part; // Handle negative numbers
        if (num < 0 && int_part == 0) {
            // Handle -0.xxx case
            (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "-%d.%0*d", int_part, STRING_FLOAT_PRECISION,
                             frac_part);
        } else {
            (void)snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "%d.%0*d", int_part, STRING_FLOAT_PRECISION, frac_part);
        }
    }
    return concat(buf);
}

String &String::concat(double num)
{
    // Use the same implementation as float
    return concat((float)num);
}

String &String::operator=(const String &rhs)
{
    if (this == &rhs)
        return *this;

    if (rhs._buffer) {
        if (!_buffer || _capacity < rhs._capacity) {
            if (_buffer)
                free(_buffer);
            _buffer = (char *)malloc(rhs._capacity);
            if (_buffer) {
                _capacity = rhs._capacity;
            } else {
                invalidate();
                return *this;
            }
        }
        (void)memcpy_s(_buffer, _capacity, rhs._buffer, rhs._len + 1);
        _len = rhs._len;
    } else {
        if (_buffer) {
            _buffer[0] = '\0';
        }
        _len = 0;
    }
    return *this;
}

String &String::operator=(const char *cstr)
{
    if (_buffer) {
        free(_buffer);
    }
    _buffer = NULL;
    _len = 0;
    _capacity = 0;

    if (cstr) {
        _len = strlen(cstr);
        _capacity = _len + 1;
        _buffer = (char *)malloc(_capacity);
        if (_buffer) {
            (void)memcpy_s(_buffer, _capacity, cstr, _len + 1);
        } else {
            invalidate();
        }
    }
    return *this;
}

String &String::operator=(String &&rval)
{
    if (this != &rval) {
        if (_buffer) {
            free(_buffer);
        }
        _buffer = rval._buffer;
        _capacity = rval._capacity;
        _len = rval._len;
        rval._buffer = NULL;
        rval._capacity = 0;
        rval._len = 0;
    }
    return *this;
}

String &String::operator+=(const String &rhs)
{
    return concat(rhs);
}

String &String::operator+=(const char *cstr)
{
    return concat(cstr);
}

String &String::operator+=(char c)
{
    return concat(c);
}

String String::operator+(const String &rhs) const
{
    String result = *this;
    result.concat(rhs);
    return result;
}

String String::operator+(const char *cstr) const
{
    String result = *this;
    result.concat(cstr);
    return result;
}

String String::operator+(char c) const
{
    String result = *this;
    result.concat(c);
    return result;
}

// Comparison /////////////////////////////////////////////////////////////////

int String::compareTo(const String &s) const
{
    if (!_buffer)
        return s._buffer ? -1 : 0;
    if (!s._buffer)
        return 1;
    return strcmp(_buffer, s._buffer);
}

bool String::equals(const String &s) const
{
    if (_len != s._len)
        return false;
    if (_len == 0)
        return true;  // Both empty strings are equal regardless of buffer state
    if (!_buffer || !s._buffer)
        return false;
    return strcmp(_buffer, s._buffer) == 0;
}

int String::compareTo(const char *cstr) const
{
    if (!_buffer) {
        bool otherNonEmpty = (cstr != NULL && cstr[0] != '\0');
        return otherNonEmpty ? -1 : 0;
    }
    if (cstr == NULL) {
        return 1; // this non-empty, other null
    }
    return strcmp(_buffer, cstr);
}

bool String::equals(const char *cstr) const
{
    if (_len == 0) {
        // Empty/invalid string: equal only to an empty C-string.
        return (cstr == NULL) || (cstr[0] == '\0');
    }
    if (cstr == NULL) {
        return false;
    }
    return strcmp(_buffer, cstr) == 0;
}

bool String::equalsIgnoreCase(const String &s) const
{
    if (_len != s._len)
        return false;
    if (!_buffer && !s._buffer)
        return true;
    if (!_buffer || !s._buffer)
        return false;

    for (unsigned int i = 0; i < _len; i++) {
        char c1 = tolower(_buffer[i]);
        char c2 = tolower(s._buffer[i]);
        if (c1 != c2)
            return false;
    }
    return true;
}

bool String::startsWith(const String &prefix) const
{
    return startsWith(prefix, 0);
}

bool String::startsWith(const String &prefix, unsigned int offset) const
{
    if (!_buffer || !prefix._buffer)
        return false;
    if (offset >= _len)
        return false;
    if (prefix._len > _len - offset)
        return false;

    return strncmp(_buffer + offset, prefix._buffer, prefix._len) == 0;
}

bool String::endsWith(const String &suffix) const
{
    if (!_buffer || !suffix._buffer)
        return false;
    if (suffix._len > _len)
        return false;

    return strcmp(_buffer + _len - suffix._len, suffix._buffer) == 0;
}

// Search /////////////////////////////////////////////////////////////////////

int String::indexOf(char ch) const
{
    return indexOf(ch, 0);
}

int String::indexOf(char ch, unsigned int from) const
{
    if (!_buffer || from >= _len)
        return -1;

    for (unsigned int i = from; i < _len; i++) {
        if (_buffer[i] == ch)
            return i;
    }
    return -1;
}

int String::indexOf(const String &s) const
{
    return indexOf(s, 0);
}

int String::indexOf(const String &s, unsigned int from) const
{
    if (!_buffer || !s._buffer || from >= _len)
        return -1;
    if (s._len == 0)
        return from;
    if (s._len > _len - from)
        return -1;

    char *found = strstr(_buffer + from, s._buffer);
    if (found) {
        return found - _buffer;
    }
    return -1;
}

int String::lastIndexOf(char ch) const
{
    return lastIndexOf(ch, _len);
}

int String::lastIndexOf(char ch, unsigned int from) const
{
    if (!_buffer || from == 0)
        return -1;

    for (int i = from - 1; i >= 0; i--) {
        if (_buffer[i] == ch)
            return i;
    }
    return -1;
}

int String::lastIndexOf(const String &s) const
{
    return lastIndexOf(s, _len);
}

int String::lastIndexOf(const String &s, unsigned int from) const
{
    if (!_buffer || !s._buffer || s._len == 0)
        return -1;
    if (from > _len)
        from = _len;
    if (s._len > from)
        return -1;

    for (int i = from - s._len; i >= 0; i--) {
        if (strncmp(_buffer + i, s._buffer, s._len) == 0) {
            return i;
        }
    }
    return -1;
}

// Substring //////////////////////////////////////////////////////////////////

String String::substring(unsigned int begin) const
{
    return substring(begin, _len);
}

String String::substring(unsigned int begin, unsigned int end) const
{
    if (!_buffer || begin >= _len || begin >= end) {
        return String();
    }
    if (end > _len)
        end = _len;

    unsigned int len = end - begin;
    String result;
    result.reserve(len + 1);
    if (result._buffer) {
        (void)memcpy_s(result._buffer, result._capacity, _buffer + begin, len);
        result._buffer[len] = '\0';
        result._len = len;
    }
    return result;
}

// Conversion /////////////////////////////////////////////////////////////////

long String::toInt() const
{
    if (!_buffer)
        return 0;
    return atol(_buffer);
}

float String::toFloat() const
{
    if (!_buffer)
        return 0.0f;
    return (float)atof(_buffer);
}

double String::toDouble() const
{
    if (!_buffer)
        return 0.0;
    return atof(_buffer);
}

// Modification ///////////////////////////////////////////////////////////////

void String::toLowerCase()
{
    if (!_buffer)
        return;
    for (unsigned int i = 0; i < _len; i++) {
        _buffer[i] = tolower(_buffer[i]);
    }
}

void String::toUpperCase()
{
    if (!_buffer)
        return;
    for (unsigned int i = 0; i < _len; i++) {
        _buffer[i] = toupper(_buffer[i]);
    }
}

void String::trim()
{
    if (!_buffer || _len == 0)
        return;

    // Trim leading whitespace
    unsigned int begin = 0;
    while (begin < _len && isspace(_buffer[begin])) {
        begin++;
    }

    // Trim trailing whitespace
    unsigned int end = _len;
    while (end > begin && isspace(_buffer[end - 1])) {
        end--;
    }

    if (begin == 0 && end == _len)
        return; // No trimming needed

    // Shift content
    unsigned int newLen = end - begin;
    if (newLen == 0) {
        _buffer[0] = '\0';
        _len = 0;
    } else {
        (void)memmove_s(_buffer, _capacity, _buffer + begin, newLen);
        _buffer[newLen] = '\0';
        _len = newLen;
    }
}

void String::replace(char find, char replace)
{
    if (!_buffer) return;
    for (unsigned int i = 0; i < _len; i++) {
        if (_buffer[i] == find) {
            _buffer[i] = replace;
        }
    }
}

void String::replace(const String &find, const String &replace)
{
    // Self-alias guard (#2): if this == &find or this == &replace, the diff>0
    // branch's reserve() reallocs this->_buffer and would dangle find/replace's
    // _buffer pointers (use-after-free). Operate via fp/rp that point to
    // independent defensive copies in the aliased case; normal (non-aliased)
    // calls just rebind the pointers, no copy.
    String findCopy, replaceCopy;
    const String *fp = &find;
    const String *rp = &replace;
    if (fp == this) { findCopy = *fp; fp = &findCopy; }
    if (rp == this) { replaceCopy = *rp; rp = &replaceCopy; }

    if (_len == 0 || fp->_len == 0) return;
    if (!_buffer || !fp->_buffer) return;
    // #5: a default-constructed empty replace (buffer NULL, len 0) is a valid
    // "delete all occurrences" request, not an error. Only require replace to
    // hold a buffer when it is non-empty.
    if (rp->_len > 0 && !rp->_buffer) return;
    if (fp->_len > _len) return;

    int diff = (int)rp->_len - (int)fp->_len;
    char *readFrom = _buffer;
    char *foundAt;

    if (diff == 0) {
        while ((foundAt = strstr(readFrom, fp->_buffer)) != NULL) {
            (void)memcpy_s(foundAt, _len - (foundAt - _buffer), rp->_buffer, rp->_len);
            readFrom = foundAt + rp->_len;
        }
    } else if (diff < 0) {
        char *writeTo = _buffer;
        while ((foundAt = strstr(readFrom, fp->_buffer)) != NULL) {
            unsigned int n = foundAt - readFrom;
            (void)memmove_s(writeTo, _len - (writeTo - _buffer), readFrom, n);
            writeTo += n;
            if (rp->_len > 0) {   // #5: skip the (0-byte, possibly NULL-src) copy when replace is empty
                (void)memcpy_s(writeTo, _len - (writeTo - _buffer), rp->_buffer, rp->_len);
            }
            writeTo += rp->_len;
            readFrom = foundAt + fp->_len;
            _len += diff;
        }
        // #4: copy the remaining tail. writeTo < readFrom (overlapping), so use
        // memmove_s (strcpy_s on overlapping buffers is UB). tailChars is the
        // remaining DEST length (_len is the post-shrink final length); the
        // source at readFrom holds exactly that many chars + the NUL, so copy
        // tailChars+1 bytes to include the terminator.
        unsigned int tailChars = _len - (unsigned int)(writeTo - _buffer);
        (void)memmove_s(writeTo, _capacity - (unsigned int)(writeTo - _buffer), readFrom, tailChars + 1);
    } else {
        unsigned int size = _len;
        char *scan = _buffer;
        while ((foundAt = strstr(scan, fp->_buffer)) != NULL) {
            scan = foundAt + fp->_len;
            size += diff;
        }
        if (size == _len) return;
        if (!reserve(size + 1)) return;   // reallocs this->_buffer; fp/rp are safe
                                          // (aliased copies own their buffers)

        int index = (int)_len - 1;
        while (index >= 0 && (index = lastIndexOf(*fp, index)) >= 0) {
            readFrom = _buffer + index + fp->_len;
            unsigned int tailLen = _len - (readFrom - _buffer);
            (void)memmove_s(readFrom + diff, _capacity - (readFrom + diff - _buffer), readFrom, tailLen);
            _len += diff;
            _buffer[_len] = '\0';
            (void)memcpy_s(_buffer + index, _capacity - index, rp->_buffer, rp->_len);
            index--;
        }
    }
}

void String::remove(unsigned int index)
{
    remove(index, (unsigned int)-1);
}

void String::remove(unsigned int index, unsigned int count)
{
    if (!_buffer) return;
    if (index >= _len) return;
    if (count == 0) return;
    if (count > _len - index) {
        count = _len - index;
    }
    unsigned int newLen = _len - count;
    (void)memmove_s(_buffer + index, _capacity - index, _buffer + index + count, _len - index - count);
    _len = newLen;
    _buffer[_len] = '\0';
}

// Operators //////////////////////////////////////////////////////////////////

bool String::operator==(const String &rhs) const
{
    return equals(rhs);
}

bool String::operator!=(const String &rhs) const
{
    return !equals(rhs);
}

bool String::operator<(const String &rhs) const
{
    if (!_buffer && rhs._buffer)
        return true;
    if (!_buffer || !rhs._buffer)
        return false;
    return strcmp(_buffer, rhs._buffer) < 0;
}

bool String::operator>(const String &rhs) const
{
    if (!_buffer && rhs._buffer)
        return false;
    if (_buffer && !rhs._buffer)
        return true;  // Non-null buffer is greater than null
    if (!_buffer || !rhs._buffer)
        return false;
    return strcmp(_buffer, rhs._buffer) > 0;
}

bool String::operator<=(const String &rhs) const
{
    return !operator>(rhs);
}

bool String::operator>=(const String &rhs) const
{
    return !operator<(rhs);
}

char String::operator[](unsigned int index) const
{
    return charAt(index);
}

char &String::operator[](unsigned int index)
{
    static char dummy = '\0';
    if (!_buffer || index >= _len)
        return dummy;
    return _buffer[index];
}

// True if the string holds a valid (non-null) buffer. Standard Arduino API;
// lets a String be used in a bool context (e.g. ArduinoJson ArduinoStringWriter).
String::operator bool() const
{
    return (_buffer != NULL);
}

String operator+(const char *cstr, const String &rhs)
{
    String result(cstr);
    result.concat(rhs);
    return result;
}
