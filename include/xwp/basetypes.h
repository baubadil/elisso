/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_BASETYPES_H
#define XWP_BASETYPES_H

#include <string>
#include <vector>
#include <set>

using namespace std;

namespace XWP
{

typedef set<string>                         StringSet;
typedef vector<string>                      StringVector;
typedef vector<reference_wrapper<string>>   StringRefVector;

class ProhibitCopy
{
public:
    ProhibitCopy() {};
    ProhibitCopy(const ProhibitCopy&) = delete;
    ProhibitCopy& operator=(const ProhibitCopy&) = delete;
};

} // namespace XWP

#define NO_WARN_UNUSED __attribute__((unused))

// Can be used with all STL containers that have "find" and "end" methods.
#define STL_EXISTS(container, item) (container.find(item) != container.end())

#ifdef DEF_STRING_IMPLEMENTATION
#define DEF_STRING(name, value) extern const string name = value
#else
#define DEF_STRING(name, value) extern const string name
#endif

#ifdef DEF_STRING_IMPLEMENTATION
#define DEF_REGEX(name, value) extern const Regex name(value)
#else
#define DEF_REGEX(name, value) extern const Regex name
#endif

using namespace XWP;

#endif // XWP_BASETYPES_H

