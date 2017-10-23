/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_LOCK_H
#define XWP_LOCK_H

#include <thread>
#include <mutex>
#include <atomic>

#include "xwp/basetypes.h"

namespace XWP
{

/***************************************************************************
 *
 *  Thread
 *
 **************************************************************************/

/**
 *
 */
class Thread : public ProhibitCopy
{
public:
    static std::thread* Create(std::function<void ()> &&fn,
                               bool fDetach = true);

    static unsigned int getHardwareConcurrency();

    static void Sleep(uint64_t ms);
};


/***************************************************************************
 *
 *  Lock
 *
 **************************************************************************/

/**
 *  Extension of the std::recursive_mutex class with some bookkeeping.
 */
class Mutex : public std::recursive_mutex
{
    friend class Lock;
private:
    uint                    cLocked = 0;
};

/**
 *  Replacement of std::lock_guard with bookkeeping.
 */
class Lock : public ProhibitCopy
{
public:
    Lock(Mutex &m)
        : _m(m)
    {
        request();
    }

    ~Lock()
    {
        release2();
    }

    void request()
    {
        _m.lock();
        ++_m.cLocked;
    }

    void release2()
    {
        --_m.cLocked;
        _m.unlock();
    }

    uint releaseAll()
    {
        auto c = _m.cLocked;
        while (_m.cLocked)
            release2();
        return c;
    }

    bool isLocked()
    {
        return (_m.cLocked > 0);
    }

protected:
    Mutex   &_m;
};


/***************************************************************************
 *
 *  StopFlag
 *
 **************************************************************************/

class StopFlag
{
public:
    StopFlag()
    {
        f = ATOMIC_VAR_INIT(false);
    }

    operator bool()
    {
        return f.load();
    }

    void set()
    {
        f = true;
    }

private:
    std::atomic_bool    f;
};


} // namespace XWP

#endif // XWP_LOCK_H
