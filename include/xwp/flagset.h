/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_BITSET_H
#define XWP_BITSET_H

#include <atomic>

//

/**
 *  A class template for typesafe bit enumerations.
 *  See http://stackoverflow.com/questions/1448396/how-to-use-enums-as-flags-in-c#.
 *
 *  Usage:
 *
 *   1. Declare an enum class with bit values, like enum class MyEnum { a = 1, b = 2, c = 3 }.
 *
 *   2. Define a variable with FlagSet<MyEnum>.
 *
 *   3. Use operators, test(), set(), reset() to your liking.
 */

template<typename E>
class FlagSet
{
public:
    FlagSet() = default;
    FlagSet(E v)
      : value(static_cast<uint>(v))
    { }

    bool test(E rhs) const
    {
        return !!(value & static_cast<uint>(rhs));
    }

    void set(E rhs)
    {
        value |= static_cast<uint>(rhs);
    }

    void clear(E rhs)
    {
        value &= ~(static_cast<uint>(rhs));
    }

private:
    uint value = 0;
};

template<typename E>
FlagSet<E> operator|(E lhs, E rhs)
{
    FlagSet<E> s(lhs);
    s.set(rhs);
    return s;
}

/*
#define DEFINE_FLAGSET(T) \
inline T operator~ (T a) { return (T)~(int)(a); } \
inline T operator| (T a, T b) { return (T)((int)(a) | (int)(b)); } \
inline int operator| (int a, T b) { return ((a) | (int)(b)); } \
inline T operator& (T a, T b) { return (T)((int)(a) & (int)(b)); } \
inline int operator& (int a, T b) { return ((a) & (int)(b)); } \
inline T operator^ (T a, T b) { return (T)((int)(a) ^ (int)(b)); } \
inline T& operator|= (T& a, T b) { return (T&)((int&)(a) |= (int)(b)); } \
inline int operator|= (int a, T b) { return ((a) |= (int)(b)); } \
inline T& operator&= (T& a, T b) { return (T&)((int&)(a) &= (int)(b)); } \
inline int operator&= (int a, T b) { return ((a) &= (int)(b)); } \
inline T& operator^= (T& a, T b) { return (T&)((int&)(a) ^= (int)(b)); }
*/

#endif // XWP_BITSET_H
