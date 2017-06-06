/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/regex.h"
#include "xwp/except.h"
#include "xwp/debug.h"
#include "xwp/stringhelp.h"

#include "pcre.h"

#include <fstream>

const string& RegexMatches::get(uint u)
{
    string strExcept;
    if (v.empty())
        strExcept = "Invalid index " + to_string(u) + " into EMPTY regex matches";
    else
    {
        if (u < v.size())
            return v[u];

        strExcept = "Invalid index " + to_string(u) + " into regex matches (0: \"" + v[0] + "\")";
    }

    if (pRE)
        strExcept += " -- size was " + to_string(v.size()) + " -- RE: \"" + pRE->toString() + "\"";
    throw FSException(strExcept);
}

/**
 *  Private class to keep the junk out of the header.
 */
class Regex::Impl
{
public:
    pcre        *_pRE = NULL;
    pcre_extra  *_pPCREExtra = NULL;
    string      _str;

    Impl(const string &str)
        : _str(str)
    {
        const char *pcszError = NULL;
        int iOffset = 0;
        if (!(_pRE = pcre_compile(str.c_str(),
                                  PCRE_UTF8 | PCRE_NO_UTF8_CHECK, // options   -- PCRE_CASELESS? PCRE_UCP PCRE_UNGREEDY
                                  &pcszError,
                                  &iOffset,
                                  NULL)))      // default character tables
            throw FSException("Error compiling regular expression \"" + str + "\": " + string(pcszError));

        _pPCREExtra = pcre_study(_pRE,
                                 PCRE_STUDY_JIT_COMPILE,        // This option brings search/replace in phoxygen from six seconds down to one.
                                 &pcszError);
    }

    ~Impl()
    {
        if (_pRE)
        {
            pcre_free(_pRE);
            _pRE = NULL;
        }
        if (_pPCREExtra)
        {
            pcre_free(_pPCREExtra);
            _pPCREExtra = NULL;
        }
    }

    int forEachMatch(const string &strHaystack,
                     std::function<void (size_t i,
                                         size_t first,
                                         size_t last)> fnMatch,
                     int ofs);
};

/**
 *  Calls fnMatch every time the implementation's PCRE matches strHaystack. With
 *  every call the callback receives the first and last offsets of the match.
 *
 *  This calls the callback at least once for the entire match. If there are
 *  captures, then this gets called again for every capture.
 *
 *  If something was found, this returns the offset of the last match. If you want
 *  to keep going as with the perl /g modifier, call the function again with the
 *  return value as a new offset.
 *
 *  If this returns 0 then nothing was found. A negative number is a PCRE error code.
 */
int Regex::Impl::forEachMatch(const string &strHaystack,
                              std::function<void (size_t i,
                                                  size_t first,
                                                  size_t last)> fnMatch,
                              int ofs)
{
    size_t cVectors = 30;
    int ovector[cVectors];

    size_t c = 0;
    int rc = pcre_exec(_pRE,
                       _pPCREExtra,
                       strHaystack.c_str(),
                       strHaystack.length(),
                       ofs,              /* start at offset 0 in the subject */
                       PCRE_NO_UTF8_CHECK,              /* default options */
                       ovector,        /* vector of integers for substring information */
                       cVectors);      /* number of elements (NOT size in bytes) */
    if (rc >= 0)
    {
        for (int i = 0;
             i < rc;
             ++i)
        {
            size_t first = ovector[i * 2];
            size_t last = ovector[i * 2 + 1];
            fnMatch(i, first, last);

            ofs = last;
            ++c;
        }

        return ofs;
    }

    return rc;
}


Regex::Regex(const string &strRE)
    : _pImpl(new Impl(strRE))
{ }

Regex::~Regex()
{
    delete _pImpl;
}

/* static */
int Regex::GetMaxCapture(const string &strReplace)
{
    static const Regex reCaptures("\\$(\\d)");
    RegexMatches aMatches;
    int maxCapture = 0;
    size_t ofs = 0;
    while (reCaptures.matches(strReplace, aMatches, ofs))
    {
        int id = atoi(aMatches.get(1).c_str());
        if (id > maxCapture)
            maxCapture = id;
    }

    return maxCapture;
}

/**
 *  Runs this regex against the given string and returns true if it
 *  matches.
 */
bool Regex::matches(const string &strHaystack) const
{
    int ovector[30];
    int rc = pcre_exec(_pImpl->_pRE,
                       _pImpl->_pPCREExtra,
                       strHaystack.c_str(),
                       strHaystack.length(),
                       0,              /* start at offset 0 in the subject */
                       PCRE_NO_UTF8_CHECK,              /* default options */
                       ovector,        /* vector of integers for substring information */
                       30);            /* number of elements (NOT size in bytes) */
    if (rc >= 0)
        return true;

    return false;
}

/**
 *  Like the first matches() variant, but in addition, this captures substrings
 *  and puts them into the given RegexMatches instance.
 *
 *  As with most regex implementation, the RegexMatches::get(0) will return
 *  the entire match, where get(1) will return the first substring, (2) the second
 *  and so on.
 *
 *  Returns true if something was matched.
 */
bool Regex::matches(const string &strHaystack,
                    RegexMatches &aMatches) const
{
    size_t ofs = 0;
    return matches(strHaystack, aMatches, ofs);
}

/**
 *  Like the second matches() variant, but allows you to specify an offset into
 *  the string, which is updated. This allows you to keep running the same
 *  regex for multiple matches to emulate perl /g modifier behavior.
 *
 *  Returns true if something was matched.
 */
bool Regex::matches(const string &strHaystack,
                    RegexMatches &aMatches,
                    size_t &ofs) const
{
    aMatches.pRE = this;

    size_t cVectors = 30;
    int ovector[cVectors];
    int rc = pcre_exec(_pImpl->_pRE,
                       _pImpl->_pPCREExtra,
                       strHaystack.c_str(),
                       strHaystack.length(),
                       ofs,
                       PCRE_NO_UTF8_CHECK,              /* default options */
                       ovector,        /* vector of integers for substring information */
                       cVectors);      /* number of elements (NOT size in bytes) */
    if (rc >= 0)
    {
        /* The value returned by pcre_exec() is one more than the highest numbered pair that has been set.
         * For example, if two substrings have been captured, the returned value is 3.
         * If there are no capturing subpatterns, the return value from a successful match is 1,
         * indicating that just the first pair of offsets has been set. */
        aMatches.v.clear();

        for (int i = 0;
             i < rc;
             ++i)
        {
            size_t first = ovector[i * 2];
            size_t last = ovector[i * 2 + 1];
            if (first < strHaystack.length())
                aMatches.v.push_back(string(strHaystack, first, last - first));
            else
                aMatches.v.push_back("");

            ofs = last;
        }

        return true;
    }

    return false;
}

size_t Regex::findReplaceImpl(string &strHaystack,
                              std::function<void (const StringVector &vMatches, string &strReplace)> fnMatch,
                              bool fGlobal) const
{
    // State data for easy access from the lambda below.
    struct
    {
        size_t first0 = 0;      // first offset of 0-index call
        size_t last0 = 0;       // last offset of 0-index call
        StringVector vMatches;
    } state;

    size_t cReplacements = 0;
    string strNew;
    size_t lastlast = 0;
    size_t ofs = 0;
    while (1)
    {
        /*
         *  Example: in
         *      FOO.FOO.FOO.REST
         *      0123456789012345
         *  replace /FOO/ with BAR.
         *  On the first call, we get first = 0, last = 3.
         *  On the second, we get     first = 4, last = 7.
         */

        int rc = _pImpl->forEachMatch(  strHaystack,
                                        [&strHaystack, fnMatch, &state](size_t i, size_t first, size_t last)
                                        {
                                            if (i == 0)
                                            {
                                                state.first0 = first;
                                                state.last0 = last;
                                            }

                                            string str(strHaystack.substr(first, last - first));
                                            state.vMatches.push_back(str);

//                                             Debug::Log(0, "Closure: i=" + to_string(i) + ", first=" + to_string(first) + ", last=" + to_string(last) + " => str=\"" + str + "\"");
                                        },
                                        ofs);

        if (rc > 0)
        {
            if (cReplacements == 0)
                // First call:
                strNew = strHaystack.substr(0, state.first0);
            else
                // Subsequent calls:
                if (state.first0 > lastlast)
                    strNew += strHaystack.substr(lastlast, state.first0 - lastlast);

            string strReplace;
            fnMatch(state.vMatches, strReplace);

            int maxCapture = GetMaxCapture(strReplace);
            if (maxCapture)
            {
                // Build a non-const replacement string only if we have backreferences.
                for (int i = 1;
                    i <= maxCapture;
                    ++i)
                {
                    stringReplace(strReplace,
                                  "$" + to_string(i),
                                  state.vMatches[i]);
                }
            }

            strNew += strReplace;

            lastlast = state.last0;

            /* Clear vMatches for the next iteration. Otherwise the lambda above will append to it and
               backreferences will go bad .*/
            if (fGlobal)
                state.vMatches.clear();

            // Search again in the haystack after the whole match.
            ofs = rc + 1;

            ++cReplacements;
        }
        else
            break;
        if (!fGlobal)
            break;
    }

    if (cReplacements)
    {
        if (lastlast)
            strNew += strHaystack.substr(lastlast);

        strHaystack = strNew;
    }

    return cReplacements;
}

size_t Regex::findReplace(string &strHaystack,
                          const string &strReplace0,
                          bool fGlobal) const
{
    return findReplaceImpl( strHaystack,
                            [&strReplace0](const StringVector &vMatches, string &strReplace)
                            {
                                strReplace = strReplace0;
                            },
                            fGlobal);
}

size_t Regex::findReplace(string &strHaystack,
                          std::function<void (const StringVector &vMatches, string &strReplace)> fnMatch,
                          bool fGlobal) const
{
    return findReplaceImpl(strHaystack,
                           fnMatch,
                           fGlobal);
}

/**
 *  Splits strHaystack along "this" regex and puts the substrings into
 *  the given StringVector.
 *
 *  If there was no match but strHaystack is not empty, then the vector
 *  receives one instance of the whole strHaystack.
 */
void Regex::split(const string &strHaystack,
                  StringVector &sv) const
{
    size_t lastlast = 0;
    //  abc||abc||abc
    // 1   | first
    // 1    | last
    // 2        | first
    // 2         | last
    _pImpl->forEachMatch(   strHaystack,
                            [&strHaystack, &sv, &lastlast](size_t i, size_t first, size_t last)
                            {
                                // Copy characters from "lastlast" (initially 0) up to first.
                                sv.push_back(string(strHaystack, lastlast, first - lastlast));
                                // Next time, copy from last + 1.
                                lastlast = last;
                            },
                            true);      // fGlobal
    if (lastlast < strHaystack.length())
        sv.push_back(string(strHaystack, lastlast));
}

const string& Regex::toString() const
{
    return _pImpl->_str;
}

bool Regex::findInFile(const string &strFilename,
                       RegexMatches &aMatches)
{
    ifstream ifs(strFilename);
    string strLine;

    while (getline(ifs, strLine))
        if (matches(strLine, aMatches))
            return true;

    return false;
}
