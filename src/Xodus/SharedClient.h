/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> SharedClient
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef XGAMERUNTIME_XODUS_SHAREDCLIENT_H
#define XGAMERUNTIME_XODUS_SHAREDCLIENT_H

#include <windows.h>
#include <string>

#include "XodusClient.h"

namespace Xodus
{
    /* The process-wide client, created on first use and never destroyed. Null when the directory of
     * xgameruntime.dll cannot be found; that failure is not cached. */
    XodusClient *SharedClient();

    /* path is the module path of len characters in a buffer of bufSize. soPath becomes
     * xgameruntime-unix.so in the same directory; len 0, len == bufSize (truncated) and a path
     * without a backslash are errors. */
    HRESULT XodusLibraryPathFromModulePath( const wchar_t *path, DWORD len, DWORD bufSize, std::wstring &soPath );

    HRESULT XodusLibraryPath( HMODULE dll, std::wstring &soPath );
}

#endif
