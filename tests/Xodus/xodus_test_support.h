/*
 * Xodus test support: log capture through the production logger and the
 * test-only host server, shared by the UnixTransport and XodusClient tests.
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

#ifndef XGAMERUNTIME_XODUS_TEST_SUPPORT_H
#define XGAMERUNTIME_XODUS_TEST_SUPPORT_H

#include <windows.h>
#include <private/logging.h>

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "tests/unix/xodus_test_server.h"

namespace XodusTest
{
    /* ---- log capture through the production logger (design Reuse) ---- */

    inline std::string &LogPath()
    {
        static std::string path;
        return path;
    }

    inline void EnsureLogging()
    {
        static bool done = false;
        if ( done ) return;
        char tmp[MAX_PATH];
        GetTempPathA( sizeof( tmp ), tmp );
        LogPath() = std::string( tmp ) + "xodus-test-" + std::to_string( GetCurrentProcessId() ) + ".log";
        SetEnvironmentVariableA( "XGAMERUNTIME_LOG_LEVEL", "trace" );
        SetEnvironmentVariableA( "XGAMERUNTIME_LOG_FILE", LogPath().c_str() );
        InitializeLogging();
        done = true;
    }

    inline std::string ReadLog()
    {
        std::ifstream f( LogPath(), std::ios::binary );
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    /* Log text written after construction. */
    class LogMark
    {
    public:
        LogMark() : m_start( ReadLog().size() ) {}
        std::string Text() const { return ReadLog().substr( m_start ); }
    private:
        size_t m_start;
    };

    /* ---- real unix libs under Wine ---- */

    inline void *RealResolve( const char *name )
    {
        return (void *)GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), name );
    }

    inline std::wstring BinDir()
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW( nullptr, path, MAX_PATH );
        std::wstring p( path );
        return p.substr( 0, p.find_last_of( L'\\' ) );
    }

    inline std::wstring RealSoPath() { return BinDir() + L"\\xgameruntime-unix.so"; }

    inline std::string UniqueSocketPath()
    {
        static int counter = 0;
        return "/tmp/xodus-ut-" + std::to_string( GetCurrentProcessId() ) + "-" + std::to_string( ++counter ) + ".sock";
    }

    /* The test-only host library with a server thread; entries per xodus_test_server.h. */
    class TestServer
    {
    public:
        TestServer( int mode, const std::string &path, const std::string &resp = "" ) : m_path( path )
        {
            using Query = LONG ( WINAPI * )( HANDLE, const void *, int, void *, SIZE_T, SIZE_T * );
            auto query = (Query)RealResolve( "NtQueryVirtualMemory" );
            m_dispatch = *(Dispatch *)RealResolve( "__wine_unix_call_dispatcher" );
            std::wstring nt = L"\\??\\" + BinDir() + L"\\xodus_test_server.so";
            struct { USHORT len, max; PWSTR buf; } us = { (USHORT)( nt.size() * 2 ), (USHORT)( nt.size() * 2 + 2 ), nt.data() };
            UINT64 res[2] = {};
            LONG st = query( (HANDLE)-1, &us, 1002, res, sizeof( res ), nullptr );
            EXPECT_EQ( st, 0 ) << "loading xodus_test_server.so";
            m_funcs = res[1];

            xts_start_args a = {};
            a.path = m_path.c_str();
            a.mode = mode;
            a.resp = (const uint8_t *)resp.data();
            a.resp_len = (uint32_t)resp.size();
            EXPECT_EQ( m_dispatch( m_funcs, XTS_CALL_START, &a ), 0 );
            EXPECT_EQ( a.status, 0 ) << "server start errno " << a.err;
        }

        ~TestServer()
        {
            xts_stop_args a = {};
            m_dispatch( m_funcs, XTS_CALL_STOP, &a );
        }

        xts_stats_args Stats()
        {
            xts_stats_args a = {};
            m_dispatch( m_funcs, XTS_CALL_STATS, &a );
            return a;
        }

        const std::string &Path() const { return m_path; }

    private:
        using Dispatch = LONG ( WINAPI * )( UINT64, unsigned int, void * );
        Dispatch m_dispatch;
        UINT64 m_funcs = 0;
        std::string m_path;
    };
}

#endif
