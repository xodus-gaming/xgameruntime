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

#include "SharedClient.h"

#include <mutex>
#include <private/logging.h>

#include "UnixTransport.h"

namespace Xodus
{
    namespace
    {
        constexpr DWORD PATH_BUFFER = 32768;

        struct Shared
        {
            UnixTransport transport;
            XodusClient client;

            explicit Shared( const std::wstring &so ) :
                transport( []( const char *name ) { return (void *)GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), name ); }, so ),
                client( transport )
            {
            }
        };

        std::mutex g_lock;
        Shared *g_shared = nullptr;
    }

    HRESULT XodusLibraryPathFromModulePath( const wchar_t *path, DWORD len, DWORD bufSize, std::wstring &soPath )
    {
        if ( !len || len >= bufSize ) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );

        std::wstring dir( path, len );
        size_t sep = dir.find_last_of( L'\\' );
        if ( sep == std::wstring::npos ) return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );

        soPath = dir.substr( 0, sep + 1 ) + L"xgameruntime-unix.so";
        return S_OK;
    }

    HRESULT XodusLibraryPath( HMODULE dll, std::wstring &soPath )
    {
        std::wstring buf( PATH_BUFFER, L'\0' );
        DWORD len = GetModuleFileNameW( dll, buf.data(), PATH_BUFFER );
        if ( !len ) return HRESULT_FROM_WIN32( GetLastError() );
        return XodusLibraryPathFromModulePath( buf.c_str(), len, PATH_BUFFER, soPath );
    }

    XodusClient *SharedClient()
    {
        std::lock_guard<std::mutex> lock( g_lock );
        if ( g_shared ) return &g_shared->client;

        HMODULE dll = nullptr;
        if ( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)&SharedClient, &dll ) )
        {
            ERR( "cannot locate the xgameruntime module, error %lu\n", GetLastError() );
            return nullptr;
        }

        std::wstring so;
        HRESULT hr = XodusLibraryPath( dll, so );
        if ( FAILED( hr ) )
        {
            ERR( "cannot locate xgameruntime-unix.so, hr %#lx\n", (unsigned long)hr );
            return nullptr;
        }

        g_shared = new Shared( so );
        return &g_shared->client;
    }
}
