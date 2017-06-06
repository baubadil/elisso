/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_REGEX_H
#define XWP_REGEX_H

#include "xwp/basetypes.h"

#include <functional>

class Regex;

class RegexMatches
{
    friend class Regex;
    StringVector v;
    const Regex *pRE = NULL;

public:
    const string& get(uint u);

    size_t size()
    {
        return v.size() ? v.size() - 1 : 0;
    }
};

/**
 *  An easy to use class for regular expressions, using the PCRE library.
 *
 *  I tried using C++11's regexen but ditched them for two reasons:
 *
 *   -- The API is just hostile. Seriously, http://en.cppreference.com/w/cpp/regex/regex_match ?
 *
 *   -- At least with libstc++ of gcc 4.9 they're horribly slow. The perl program I used for
 *      testing, which does a lot of string processing, used to run in 2.5 seconds. The first
 *      C++ version using "regex" took 2.5 MINUTES. The version with this PCRE library now
 *      finishes in one second. Go figure.
 *
 *  A Regex instance is a compiled regular expression. The regexen themselves support the whole
 *  Perl syntax, as supported by PCRE. Three useful Perl features are supported on top of that:
 *
 *   --  You can use two variants of matches() for simple tests or to capture substrings.
 *
 *   --  You can use findReplace() for sed/perl-like s/FOO/BAR/ replacements, including
 *       $1-like backreferences.
 *
 *   --  You can use split() to split a string along a regex separator.
 *
 *  If you use a regex more than once, you can use a static or global variable, like so:
 *
 *    static const Regex re("(\\d+)");
 *    RegexMatch aMatches;
 *    if (re.matches("test123", aMatches))
 *        const string &str = aMatches.get(1); ...
 */
class Regex
{
private:
    class Impl;
    Impl    *_pImpl;

    size_t findReplaceImpl(string &strHaystack,
                           std::function<void (const StringVector &vMatches, string &strReplace)> fnMatch,
                           bool fGlobal) const;
public:
    Regex(const string &strRE);
    ~Regex();

    static int GetMaxCapture(const string &strReplace);

    bool matches(const string &strHaystack) const;
    bool matches(const string &strHaystack,
                 RegexMatches &aMatches) const;
    bool matches(const string &strHaystack,
                 RegexMatches &aMatches,
                 size_t &ofs) const;

    size_t findReplace(string &strHaystack,
                       const string &strReplace,
                       bool fGlobal) const;
    size_t findReplace(string &strHaystack,
                       std::function<void (const StringVector &vMatches, string &strReplace)> fnMatch,
                       bool fGlobal) const;

    void split(const string &strHaystack, StringVector &sv) const;

    const string& toString() const;

    bool findInFile(const string &strFilename, RegexMatches &aMatches);
};

#endif
