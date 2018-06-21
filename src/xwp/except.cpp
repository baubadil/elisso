/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/except.h"
#include "xwp/debug.h"

#include <cassert>
#include <string>
#include <string.h>

namespace XWP
{

const std::string g_strColon(": ");

FSException::FSException(const std::string &str)
    : _str(str)
{
//     assert(false);
    Debug::Log(CMD_TOP, "EXCEPTION: " + str);
}

/* virtual */
const char*
FSException::what() const throw()
{
    return _str.c_str();
}

ErrnoException::ErrnoException(const std::string &context)
    : FSException(context + g_strColon)
{
    char szTemp[200];
    this->_str += strerror_r(errno, szTemp, sizeof(szTemp));
}

}

