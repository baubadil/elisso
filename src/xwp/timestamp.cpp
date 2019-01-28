/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/timestamp.h"

#include "xwp/except.h"
#include "xwp/regex.h"
#include "xwp/stringhelp.h"

#include <algorithm>

using namespace std;

namespace XWP
{

/* virtual */
string
TimeStamp::toString(bool fCompact /* = false */) const
{
    char sz[30];
    if (fCompact)
        snprintf(sz, sizeof(sz), "%04d%02d%02d%02d%02d%02d", _year, _month, _day, _hours, _minutes, _seconds);
    else
        snprintf(sz, sizeof(sz), "%04d-%02d-%02d %02d:%02d:%02d", _year, _month, _day, _hours, _minutes, _seconds);
    return string(sz);
}

/* static */
PTimeStamp
TimeStamp::Create(const string &strDate,
                  const char *pcszThrowOnError)
{
    PTimeStamp pNew;

    static const Regex REGEX_DATETIME_SPLIT(R"i____(^(\d\d\d\d)-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)$)i____");

    RegexMatches aMatches;
    if (REGEX_DATETIME_SPLIT.matches(strDate, aMatches))
        pNew = make_shared<TimeStamp>(stoi(aMatches.get(1)),
                                      stoi(aMatches.get(2)),
                                      stoi(aMatches.get(3)),
                                      stoi(aMatches.get(4)),
                                      stoi(aMatches.get(5)),
                                      stoi(aMatches.get(6)));
    else if (pcszThrowOnError)
    {
        string str2(pcszThrowOnError);
        stringReplace(str2, "%", quote(strDate));
        throw FSException(str2);
    }

    return pNew;
}

/* static */
string
TimeStamp::Implode(const string &strGlue,
                   const TimeStampSet &set)
{
    StringVector sv;
    string str;
    for (const auto &p : set)
        sv.push_back(p->toString());

    std::sort(sv.begin(), sv.end());

    for (auto &s : sv)
    {
        if (!str.empty())
            str += strGlue + s;
        else
            str += s;
    }

    return str;
}

/* static */
size_t
TimeStamp::Explode(const string &str,
                   const string &strDelimiter,
                   TimeStampSet &dtset,
                   const char *pcszThrowOnError)
{
    size_t cReturn = 0;
    StringSet sset = explodeSet(str, strDelimiter);
    for (auto &strStamp : sset)
    {
        auto pair = dtset.insert(Create(strStamp, pcszThrowOnError));
        if (pair.second)
            ++cReturn;
    }

    return cReturn;
}

} // namespace XWP
