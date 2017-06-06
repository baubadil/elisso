/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_EXCEPT_H
#define XWP_EXCEPT_H

#include <exception>
#include <string>

using namespace std;

namespace XWP
{

/***************************************************************************
 *
 *  FSException
 *
 **************************************************************************/

class FSException : public exception
{
    string _str;
public:
    FSException(const std::string &str);
    FSException(const char *pcsz)
        : FSException(std::string(pcsz)) {}
    ~FSException() {};
    virtual const char* what() const throw();
};

class FSCancelledException : public FSException
{
public:
    FSCancelledException() : FSException("Cancelled")
    { };
};

} // namespace XWP

#endif // XWP_EXCEPT_H
