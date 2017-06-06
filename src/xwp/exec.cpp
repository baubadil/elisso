/*
 * libxwp -- generic helper routines for C++11. (C) 2015--2017 Baubadil GmbH.
 *
 * libxwp is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the libxwp main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/exec.h"
#include "xwp/stringhelp.h"
#include "xwp/except.h"

#include <memory>

#include <unistd.h>
#include <string.h>

using namespace std;

namespace XWP
{

string
exec(const string &cmd)
{
    char buffer[128];
    string result = "";
    shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe)
        throw std::runtime_error("popen() failed!");

    while (!feof(pipe.get()))
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;

    return result;
}

/**
 *  Attempts to return the absolute path of the executable from argv[0].
 *  This can throw if realpath or getenv fail. Works on Unixish systems only.q
 */
string
getExecutableFileName(const char *argv0)
{
    // If we have an absolute path we're done.
    if (*argv0 != '/')
    {
        char szBuf[1024];
        // If we don't then it's complicated.
        // If there is a slash in the file name, then we assume it's relative to the CWD.
        if (strchr(argv0, '/'))
        {
            if (!realpath(argv0, szBuf))
                throw FSException("Cannot resolve real path for argv[0]");
            return szBuf;
        }
        else
        {
            // No '/' in filename: then search PATH.
            string strPath(getenv("PATH"));
            string strExec(argv0);
            for (string strDir : explodeSet(strPath, ":"))
            {
                string strFile = makePath(strDir, strExec);
                if (access(strFile.c_str(), F_OK) != -1)
                    if (realpath(strFile.c_str(), szBuf))
                        return szBuf;
            }
        }
    }

    return argv0;
}

} // namespace XWPq
