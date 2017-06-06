/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/thread.h"

#include <thread>
#include <atomic>
#include <cassert>

std::atomic<unsigned int> g_uThreadID(0);

/* static  */
unsigned int
Thread::Create(std::function<void ()> &&fn)
{
    auto pThread = new std::thread([fn]()
    {
        try
        {
            fn();
        }
        catch (...)
        {
//             assert(false);
        }
    });
    pThread->detach();
    return ++g_uThreadID;;
}

/* static */
unsigned int
Thread::getHardwareConcurrency()
{
    return std::thread::hardware_concurrency();
}

/* static */
void Thread::Sleep(uint64_t ms)
{
    this_thread::sleep_for(chrono::milliseconds(50));
}
