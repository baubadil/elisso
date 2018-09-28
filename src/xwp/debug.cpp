/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/debug.h"
#include "xwp/thread.h"
#include "xwp/debug_c.h"

#include <list>
#include <iostream>
#include <chrono>
#include <cstdarg>
#include <cstring>

namespace XWP {

int g_flDebugSet = 0;
string g_strDebugProgramName;

struct FuncItem
{
    DebugFlag fl;
    string strFuncName;
    chrono::steady_clock::time_point t1;

    FuncItem(DebugFlag fl_, const string &strFuncName_)
        : fl(fl_),
          strFuncName(strFuncName_),
          t1(chrono::steady_clock::now())
    { }
};

Mutex g_mutexDebug;

class DebugLock : public XWP::Lock
{
public:
    DebugLock()
        : Lock(g_mutexDebug)
    { }
};

list<FuncItem> g_llFuncs2;
uint g_iIndent2 = 0;
bool g_fNeedsNewline2 = false;

/* static */
void Debug::Enter2(DebugFlag fl,
                   const string &strFuncName,
                   const string &strExtra /* = "" */ )
{
    DebugLock lock;
    if ( (fl == DEBUG_ALWAYS) || (g_flDebugSet & (uint)fl) )
    {
        string str2("Entering " + strFuncName);
        if (strExtra.length())
            str2.append(": " + strExtra);
        Debug::Log(fl, str2);
        ++g_iIndent2;
    }
    g_llFuncs2.push_back({fl, strFuncName});
}

/* static */
void Debug::Leave2(const string &strExtra /* = "" */)
{
    DebugLock lock;
    if (!g_llFuncs2.empty())
    {
        FuncItem f = g_llFuncs2.back();      // Make a copy.
        g_llFuncs2.pop_back();

        if ( (f.fl == DEBUG_ALWAYS) || (g_flDebugSet & (uint)f.fl) )
        {
            --g_iIndent2;
            string s = "Leaving " + f.strFuncName;
            if (!strExtra.empty())
                s += " (" + strExtra + ")";
            chrono::milliseconds time_span = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - f.t1);
            s += " -- took " + to_string(time_span.count()) + "ms";
            Debug::Log(f.fl, s);
        }
    }
}

/* static */
void Debug::Log(DebugFlag fl,
                const string &str,
                uint8_t flMessage /* = 0 */)
{
    DebugLock lock;
    bool fAlways;
    if (    (fAlways = (fl == DEBUG_ALWAYS))
         || (g_flDebugSet & (uint)fl)
       )
    {
        bool fContinue = !!(flMessage & CONTINUE_FROM_PREVIOUS);

        if (g_fNeedsNewline2)
        {
            if (!fContinue)
                cout << "\n";
            g_fNeedsNewline2 = false;
        }

        // Do not print the program name if we're continuing from the previous line.
        const string &strProgName = (fContinue) ? "" : g_strDebugProgramName;

        string strIndent;
        if (fAlways && (g_iIndent2 > 0))
            cout << strProgName << MakeColor(AnsiColor::BRIGHT_WHITE, ">") << string(g_iIndent2 * 2 - 1, ' ');
        else
            cout << strProgName << string(g_iIndent2 * 2, ' ');
        cout << str;
        if ( (!fAlways) || (0 == (flMessage & NO_ECHO_NEWLINE)) )
            cout << "\n";
        else
        {
            g_fNeedsNewline2 = true;     // for next message
        }
        cout.flush();
    }
}

/* static */
void Debug::Message(const string &str,
                    uint8_t flMessage /* = 0 */ )
{
    Debug::Log(DEBUG_ALWAYS,
               str,
               flMessage);
}

// https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
#define ANSI_COLOR_RED                  "\x1b[31m"
#define ANSI_COLOR_RED_BRIGHT           "\x1b[31;1m"
#define ANSI_COLOR_GREEN                "\x1b[32m"
#define ANSI_COLOR_YELLOW               "\x1b[33m"
#define ANSI_COLOR_BLUE                 "\x1b[34m"
#define ANSI_COLOR_BLUE_BRIGHT_BOLD     "\x1b[34;1m"
#define ANSI_COLOR_MAGENTA              "\x1b[35m"
#define ANSI_COLOR_CYAN                 "\x1b[36m"
#define ANSI_COLOR_RESET                "\x1b[0m"
#define ANSI_COLOR_WHITE_BRIGHT         "\x1b[37;1m"

string
Debug::MakeColor(AnsiColor c, string str)
{
    switch (c)
    {
        case AnsiColor::BRIGHT_WHITE:
            return ANSI_COLOR_WHITE_BRIGHT + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::RED:
            return ANSI_COLOR_RED + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::BRIGHT_RED:
            return ANSI_COLOR_RED_BRIGHT + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::GREEN:
            return ANSI_COLOR_GREEN + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::YELLOW:
            return ANSI_COLOR_YELLOW + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::BLUE:
            return ANSI_COLOR_BLUE + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::BRIGHT_BLUE:
            return ANSI_COLOR_BLUE_BRIGHT_BOLD + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::MAGENTA:
            return ANSI_COLOR_MAGENTA + str + ANSI_COLOR_RESET;
        break;

        case AnsiColor::CYAN:
            return ANSI_COLOR_CYAN + str + ANSI_COLOR_RESET;
        break;
    }

    return str;
}

/* static */
void
Debug::SetProgramName(const char *pcsz)
{
    g_strDebugProgramName = "[" + string(pcsz) + "] ";
}

void
Debug::Warning(const string& str)
{
    Log(DEBUG_ALWAYS, MakeColor(AnsiColor::YELLOW, "WARNING: " + str));
}

} // namespace

void DebugEnter(const char *pcszFormat, ...)
{
    std::string str;
    if (pcszFormat && *pcszFormat)
    {
        va_list ap;
        va_start(ap, pcszFormat);
        char *p;
        if (vasprintf(&p, pcszFormat, ap) > 0)
        {
            str = p;
            free(p);
        }
    }

    Debug::Enter2(DEBUG_C, str);
}

void DebugLeave(const char *pcszFormat, ...)
{
    std::string str;
    if (pcszFormat && *pcszFormat)
    {
        va_list ap;
        va_start(ap, pcszFormat);
        char *p;
        if (vasprintf(&p, pcszFormat, ap) > 0)
        {
            str = p;
            free(p);
        }
    }

    Debug::Leave2(str);
}

void DebugLog(const char *pcszFormat, ...)
{
    std::string str;
    if (pcszFormat && *pcszFormat)
    {
        va_list ap;
        va_start(ap, pcszFormat);
        char *p;
        if (vasprintf(&p, pcszFormat, ap) > 0)
        {
            str = p;
            free(p);
        }
    }

    Debug::Log(DEBUG_C, str);
}
