/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * HiSilicon Adaptation: Direct SDK call implementation with LiteOS heap
 */
/*
  WString.h - String library for Wiring & Arduino
  ...mostly rewritten by Paul Stoffregen...
  Copyright (c) 2009-10 Hernando Barragan.  All right reserved.
  Copyright 2011, Paul Stoffregen, paul@pjrc.com

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

#ifndef WSTRING_H
#define WSTRING_H

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef F
#define F(string_literal) (string_literal)
#endif

class String {
private:
    char *_buffer;
    unsigned int _len;
    unsigned int _capacity;

    void invalidate();
    bool changeBuffer(unsigned int maxStrLen);

public:
    // Constructors
    String();
    String(const char *cstr);
    String(const char *cstr, unsigned int length);
    String(const String &str);
    String(String &&rval);   // move constructor (standard Arduino API)
    String(char c);
    String(int num, int base = 10);
    String(unsigned int num, int base = 10);
    String(long num, int base = 10);
    String(unsigned long num, int base = 10);
    String(float num, int digits = 2);
    String(double num, int digits = 2);
    ~String();

    // Memory management
    bool reserve(unsigned int size);
    unsigned int capacity() const;

    // Basic operations
    unsigned int length() const;
    char charAt(unsigned int index) const;
    void setCharAt(unsigned int index, char c);
    const char *c_str() const;
    char *buffer() const;

    // Byte/char buffer extraction (standard Arduino API).
    void getBytes(unsigned char *buf, unsigned int bufsize, unsigned int index = 0) const;
    void toCharArray(char *buf, unsigned int bufsize, unsigned int index = 0) const
    {
        getBytes((unsigned char *)buf, bufsize, index);
    }

    bool isEmpty() const { return _len == 0; }

    // STL-style iteration (standard Arduino API).
    char *begin() { return _buffer; }
    char *end() { return _buffer + _len; }
    const char *begin() const { return c_str(); }
    const char *end() const { return c_str() + _len; }

    // Concatenation
    String &concat(const String &str);
    String &concat(const char *cstr);
    String &concat(const char *cstr, unsigned int length);
    String &concat(char c);
    String &concat(unsigned char num);
    String &concat(int num);
    String &concat(unsigned int num);
    String &concat(long num);
    String &concat(unsigned long num);
    String &concat(float num);
    String &concat(double num);

    String &operator = (const String &rhs);
    String &operator = (const char *cstr);
    String &operator = (String &&rval);   // move assignment
    String &operator += (const String &rhs);
    String &operator += (const char *cstr);
    String &operator += (char c);
    String &operator += (unsigned char num) { return concat(num); }
    String &operator += (int num) { return concat(num); }
    String &operator += (unsigned int num) { return concat(num); }
    String &operator += (long num) { return concat(num); }
    String &operator += (unsigned long num) { return concat(num); }
    String &operator += (float num) { return concat(num); }
    String &operator += (double num) { return concat(num); }

    String operator + (const String &rhs) const;
    String operator + (const char *cstr) const;
    String operator + (char c) const;

    // Comparison
    int compareTo(const String &s) const;
    int compareTo(const char *cstr) const;
    bool equals(const String &s) const;
    bool equals(const char *cstr) const;
    bool equalsIgnoreCase(const String &s) const;
    bool startsWith(const String &prefix) const;
    bool startsWith(const String &prefix, unsigned int offset) const;
    bool endsWith(const String &suffix) const;

    // Search
    int indexOf(char ch) const;
    int indexOf(char ch, unsigned int from) const;
    int indexOf(const String &s) const;
    int indexOf(const String &s, unsigned int from) const;
    int lastIndexOf(char ch) const;
    int lastIndexOf(char ch, unsigned int from) const;
    int lastIndexOf(const String &s) const;
    int lastIndexOf(const String &s, unsigned int from) const;

    // Substring
    String substring(unsigned int begin) const;
    String substring(unsigned int begin, unsigned int end) const;

    // Conversion
    long toInt() const;
    float toFloat() const;
    double toDouble() const;

    // Modification
    void toLowerCase();
    void toUpperCase();
    void trim();
    void replace(char find, char replace);
    void replace(const String &find, const String &replace);
    void remove(unsigned int index);
    void remove(unsigned int index, unsigned int count);

    // Operators
    bool operator == (const String &rhs) const;
    bool operator != (const String &rhs) const;
    bool operator < (const String &rhs) const;
    bool operator > (const String &rhs) const;
    bool operator <= (const String &rhs) const;
    bool operator >= (const String &rhs) const;
    // const char* comparison operators (String on LHS) — standard Arduino API.
    bool operator == (const char *cstr) const { return equals(cstr); }
    bool operator != (const char *cstr) const { return !equals(cstr); }
    bool operator <  (const char *cstr) const { return compareTo(cstr) < 0; }
    bool operator >  (const char *cstr) const { return compareTo(cstr) > 0; }
    bool operator <= (const char *cstr) const { return compareTo(cstr) <= 0; }
    bool operator >= (const char *cstr) const { return compareTo(cstr) >= 0; }
    char operator[](unsigned int index) const;
    char &operator[](unsigned int index);

    // Truthiness (standard Arduino API) — true if the string holds a valid
    // buffer. Libraries (e.g. ArduinoJson ArduinoStringWriter) test a String
    // in a bool context, so the Arduino layer must provide this conversion.
    operator bool() const;

    friend String operator + (const char *cstr, const String &rhs);

    // const char* comparison operators (const char* on LHS) — reverse direction,
    // so `"abc" == myString` compiles (the String-on-LHS overloads above only
    // cover `myString == "abc"`).
    friend bool operator == (const char *cstr, const String &rhs) { return rhs.equals(cstr); }
    friend bool operator != (const char *cstr, const String &rhs) { return !rhs.equals(cstr); }
    friend bool operator <  (const char *cstr, const String &rhs) { return rhs.compareTo(cstr) > 0; }
    friend bool operator >  (const char *cstr, const String &rhs) { return rhs.compareTo(cstr) < 0; }
    friend bool operator <= (const char *cstr, const String &rhs) { return rhs.compareTo(cstr) >= 0; }
    friend bool operator >= (const char *cstr, const String &rhs) { return rhs.compareTo(cstr) <= 0; }
};

// External functions
String toString(const String &val);

#endif // WSTRING_H
