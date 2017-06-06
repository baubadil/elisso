/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_STRINGHELP_H
#define XWP_STRINGHELP_H

#include "xwp/basetypes.h"

DEF_STRING(EMPTY_STRING, "");

DEF_STRING(MDASH, u8"—");
DEF_STRING(LDQUO, u8"“");
DEF_STRING(RDQUO, u8"”");
DEF_STRING(HELLIP, u8"…");

namespace XWP
{

void trim(string &s);
string trimmed(const string &s);

StringSet explodeSet(const string &str,
                     const string &strDelimiter,
                     bool fTrim = false,
                     bool fKeepEmpty = false);
StringVector explodeVector(const string &str,
                           const string &strDelimiter,
                           bool fTrim = false,
                           bool fKeepEmpty = false);

string implode(const string &strGlue, const StringSet &v);
string implode(const string &strGlue, const StringVector &v);
string implode(const string &strGlue, const StringRefVector &v);

void toHTML(string &str);
string toHTML2(const string &str);
void toLaTeX(string &ls, bool fInPRE);
string toLaTeX2(const string &ls, bool fInPRE);

void stringReplace(string &subject,
                   const string &search,
                   const string &replace);

string strToLower(const string &str);
string strToUpper(const string &str);

bool startsWith(const string &strHaystack,
                const string &strStart);
bool endsWith(const string &strHaystack,
              const string &strEnd);

string quote(const string &str1);

string makePath(const string &str1,
                const string &str2);

string getDirnameString(const string& str);
string getBasenameString(const string &str);
string getExtensionString(const string &str);

string formatNumber(int z);
string formatBytes(uint64_t u);

} // namespace XWP

#endif // XWP_STRINGHELP_H
