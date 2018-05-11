/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/stringhelp.h"

#include <functional>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include <string.h>

using namespace std;

namespace XWP
{

// trim from start (in place)
void ltrim(string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            std::not1(std::ptr_fun<int, int>(std::isspace))));
}

// trim from end (in place)
void rtrim(string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
            std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
}

// trim from both ends (in place)
void trim(string &s)
{
    ltrim(s);
    rtrim(s);
}

string trimmed(const string &s)
{
    string s2(s);
    trim(s2);
    return s2;
}


void ForEachSubstring(const string &str,
                      const string &strDelimiter,
                      std::function<void (const string&)> fnParticle)
{
    size_t p1 = 0;
    size_t p2;
    while ((p2 = str.find(strDelimiter, p1)) != string::npos)
    {
        int len = p2 - p1;
        if (len > 0)
            fnParticle(str.substr(p1, len));
        p1 = p2 + 1;
    }

    fnParticle(str.substr(p1));
}

StringSet explodeSet(const string &str,
                     const string &strDelimiter,
                     bool fTrim, /* = false */
                     bool fKeepEmpty /* = false */)
{
    set<string> s;

    ForEachSubstring(str,
                     strDelimiter,
                     [&s, fTrim, fKeepEmpty](const string &strParticle)
                     {
                         if (fKeepEmpty || (!strParticle.empty()))
                         {
                             if (fTrim)
                                 s.insert(trimmed(strParticle));
                             else
                                 s.insert(strParticle);
                         }
                     });

    return s;
}

StringVector explodeVector(const string &str,
                           const string &strDelimiter,
                           bool fTrim, /* = false */
                           bool fKeepEmpty /* = false */)
{
    vector<string> v;

    ForEachSubstring(str,
                     strDelimiter,
                     [&v, fTrim, fKeepEmpty](const string &strParticle)
                     {
                         if (fKeepEmpty || (!strParticle.empty()))
                         {
                             if (fTrim)
                                 v.push_back(trimmed(strParticle));
                             else
                                 v.push_back(strParticle);
                         }
                     });

    return v;
}

string implode(const string &strGlue, const StringSet &sset)
{
    string str;
    for (const auto &s : sset)
        if (!s.empty())
        {
            if (!str.empty())
                str += strGlue + s;
            else
                str += s;
        }

    return str;
}

string implode(const string &strGlue, const StringVector &v)
{
    string str;
    for (const auto &s : v)
        if (!s.empty())
        {
            if (!str.empty())
                str += strGlue + s;
            else
                str += s;
        }

    return str;
}

string implode(const string &strGlue, const StringRefVector &v)
{
    string str;
    for (const auto &s : v)
        if (!s.get().empty())
        {
            if (!str.empty())
                str += strGlue + s.get();
            else
                str += s;
        }

    return str;
}

void toHTML(string &str)
{
    stringReplace(str, "&", "&amp;");
    stringReplace(str, "<", "&lt;");
    stringReplace(str, ">", "&gt;");
}

string toHTML2(const string &str)
{
    string strCopy(str);
    toHTML(strCopy);
    return strCopy;
}

void toLaTeX(string &ls,
             bool fInPRE)           //!< in: true if you're in a alltt environment (for PRE formatting)
{
    stringReplace(ls, "\\", "\\\\");
    stringReplace(ls, "{", "\\{");
    stringReplace(ls, "}", "\\}");
    if (!fInPRE)
    {
        stringReplace(ls, "$", "\\$");
        stringReplace(ls, "#", "\\#");
        stringReplace(ls, "&", "\\&");
        stringReplace(ls, "_", "\\_");
        stringReplace(ls, "%", "\\%");
        stringReplace(ls, "€", "\\texteuro{}");
    }
}

string toLaTeX2(const string &ls,
                bool fInPRE)        //!< in: true if you're in a alltt environment (for PRE formatting)
{
    string strCopy(ls);
    toLaTeX(strCopy, fInPRE);
    return strCopy;
}

void stringReplace(string &subject,
                   const string &search,
                   const string &replace)
{
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != string::npos)
    {
         subject.replace(pos, search.length(), replace);
         pos += replace.length();
    }
}

string strToLower(const string &str0)
{
    string str = str0;
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

string strToUpper(const string &str0)
{
    string str = str0;
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

bool startsWith(const string &strHaystack,
                const string &strStart)
{
    if (strHaystack.length() >= strStart.length())
        return (0 == strHaystack.compare(0,
                                         strStart.length(),
                                         strStart));

    return false;
}

bool endsWith(const string &strHaystack,
              const string &strEnd)
{
    if (strHaystack.length() >= strEnd.length())
        return (0 == strHaystack.compare(strHaystack.length() - strEnd.length(),
                                         strEnd.length(),
                                         strEnd));

    return false;
}

string quote(const string &str)
{
    return LDQUO + str + RDQUO;
}

std::string STR_TRUE("true");
std::string STR_FALSE("false");

const string& trueFalse(bool f)
{
    return (f) ? STR_TRUE : STR_FALSE;
}

string makePath(const string &str1,
                const string &str2)
{
    string s = str1;
    if (    (!s.empty())
         && (!endsWith(s, "/"))
       )
        s += "/";
    return s + str2;
}

string getDirnameString(const string& str)
{
    const char *p1 = str.c_str();
    const char *p2 = strrchr(p1, '/');
    if (p2)
        return str.substr(0, p2 - p1);
    return "";
}

string getBasenameString(const string &str)
{
    const char *p1 = str.c_str();
    const char *p2 = strrchr(p1, '/');
    if (p2)
        return p2 + 1;
    return str;
}

string getExtensionString(const string &str)
{
    const char *p1 = str.c_str();
    const char *p2 = strrchr(p1, '/');
    const char *pStart;
    if (p2)
        pStart = p2 + 1;
    else
        pStart = p1;
    const char *pDot = strrchr(pStart, '.');
    if (pDot)
        return pDot + 1;

    return "";
}

/**
 *  Formats the given number according to the current locale.
 */
string formatNumber(int z)
{
    stringstream ss;
    ss.imbue(locale(""));
    ss << fixed << z;
    return ss.str();
}

const string
    strSuffixEB = " EB",
    strSuffixPB = " PB",
    strSuffixTB = " TB",
    strSuffixGB = " GB",
    strSuffixMB = " MB",
    strSuffixKB = " KB",
    strSuffixB = " bytes";

string formatBytes(uint64_t u)
{
    const string *pSuffix = &strSuffixB;
    double readable;
    if (u >= 0x1000000000000000) // Exabyte
    {
        pSuffix = &strSuffixEB;
        readable = (u >> 50);
    }
    else if (u >= 0x4000000000000) // Petabyte
    {
        pSuffix = &strSuffixPB;
        readable = (u >> 40);
    }
    else if (u >= 0x10000000000) // Terabyte
    {
        pSuffix = &strSuffixTB;
        readable = (u >> 30);
    }
    else if (u >= 0x40000000) // Gigabyte
    {
        pSuffix = &strSuffixGB;
        readable = (u >> 20);
    }
    else if (u >= 0x100000) // Megabyte
    {
        pSuffix = &strSuffixMB;
        readable = (u >> 10);
    }
    else if (u >= 0x400) // Kilobyte
    {
        pSuffix = &strSuffixKB;
        readable = u;
    }
    else
    {
        return to_string(u) + strSuffixB;
    }
    // Divide by 1024 to get fractional value.
    readable = (readable / 1024);
    // Return formatted number with suffix
    std::stringstream ss;
    ss.imbue(locale(""));
    ss << fixed << setprecision(2) << readable;
    return ss.str() + *pSuffix;
}


} // namespace XWP
