/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_DEBUG_H
#define XWP_DEBUG_H

#include "xwp/basetypes.h"

namespace XWP
{

extern int g_flDebugSet;

enum class AnsiColor
{
    BRIGHT_WHITE,
    RED,
    BRIGHT_RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN
};


/***************************************************************************
 *
 *  Debug
 *
 **************************************************************************/

typedef uint DebugFlag;
const DebugFlag DEBUG_ALWAYS          = 0;

    // low-level
const DebugFlag XWPTAGS         = (1 <<  1);
const DebugFlag FILE_LOW        = (1 <<  3);

    // mid-level
const DebugFlag FILE_MID        = (1 << 15);
const DebugFlag XICONVIEW       = (1 << 16);

    // high-level
const DebugFlag DEBUG_C         = (1 << 17);
const DebugFlag FSEXCEPTION     = (1 << 18);
const DebugFlag FILE_HIGH       = (1 << 19);
const DebugFlag CMD_TOP         = (1 << 20);

// #define DFL(a) (a)
// const DebugFlag g_dflLevel1 =   DFL(CMD_TOP);
// #undef DFL

const uint8_t NO_ECHO_NEWLINE           = 0x01;
const uint8_t CONTINUE_FROM_PREVIOUS    = 0x02;

class Debug
{
    string _strExit;
public:
    Debug(DebugFlag fl,
          const string &strFuncName,
          const string &strExtra = "")
    {
        Enter2(fl, strFuncName, strExtra);
    }

    ~Debug()
    {
        Leave2(_strExit);
    }

    void setExit(const string &str)
    {
        _strExit = str;
    }

    static void Enter2(DebugFlag fl,
                      const string &strFuncName,
                      const string &strExtra = "");
    static void Leave2(const string &strExtra = "");
    static void Log(DebugFlag fl,
                    const string &str,
                    uint8_t flMessage = 0);
    static void Message(const string &str,
                        uint8_t flMessage = 0);
    static void Warning(const string &str);

    static string MakeColor(AnsiColor c, string str);
};

} // namespace XWP

#endif // XWP_STRINGHELP_H
