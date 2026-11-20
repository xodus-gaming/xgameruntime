/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> UnixTransport
 *  Load and dispatch call shapes follow the Wine unix-call spike.
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

#include "UnixTransport.h"

#include <windows.h>
#include <private/logging.h>

#include <algorithm>
#include <cstring>

#include "../../unix/xodus_abi.h"

namespace Xodus
{
    namespace
    {
        constexpr LONG NT_STATUS_DLL_NOT_FOUND = (LONG)0xC0000135;
        constexpr LONG NT_STATUS_ENTRYPOINT_NOT_FOUND = (LONG)0xC0000139;
        constexpr int MEMORY_WINE_LOAD_UNIXLIB_BY_NAME = 1002;

        struct NtString { USHORT Length, MaximumLength; PWSTR Buffer; };
        using NtQueryVirtualMemoryFn = LONG ( WINAPI * )( HANDLE, const void *, int, void *, SIZE_T, SIZE_T * );
    }

    UnixTransport::UnixTransport( Resolver resolver, const std::wstring &so_path, UnixTransportOptions opts )
        : m_resolver( std::move( resolver ) ), m_so_path( so_path ), m_opts( std::move( opts ) )
    {
    }

    UnixTransport::~UnixTransport()
    {
        Close();
    }

    HRESULT UnixTransport::Load()
    {
        if ( m_dispatcher ) return S_OK;

        auto query = (NtQueryVirtualMemoryFn)m_resolver( "NtQueryVirtualMemory" );
        if ( !query )
        {
            ERR( "symbol NtQueryVirtualMemory not found\n" );
            return HRESULT_FROM_NT( NT_STATUS_ENTRYPOINT_NOT_FOUND );
        }

        auto dispatcher = (Dispatcher *)m_resolver( "__wine_unix_call_dispatcher" );
        if ( !dispatcher || !*dispatcher )
        {
            ERR( "symbol __wine_unix_call_dispatcher not found\n" );
            return HRESULT_FROM_NT( NT_STATUS_ENTRYPOINT_NOT_FOUND );
        }

        std::wstring nt_path = L"\\??\\" + m_so_path;
        NtString name;
        name.Buffer = nt_path.data();
        name.Length = (USHORT)( nt_path.size() * sizeof( wchar_t ) );
        name.MaximumLength = name.Length + sizeof( wchar_t );

        UINT64 res[2] = { 0, 0 };
        LONG status = query( (HANDLE)-1, &name, MEMORY_WINE_LOAD_UNIXLIB_BY_NAME, res, sizeof( res ), nullptr );
        if ( status )
        {
            ERR( "load of %ls failed (status %#lx); remedy: build and install xgameruntime-unix.so next to the DLL\n",
                 m_so_path.c_str(), (unsigned long)status );
            return HRESULT_FROM_NT( NT_STATUS_DLL_NOT_FOUND );
        }

        m_funcs = res[1];
        m_dispatcher = dispatcher;
        return S_OK;
    }

    HRESULT UnixTransport::Call( unsigned int entry, const char *name, void *args )
    {
        HRESULT hr = Load();
        if ( FAILED( hr ) ) return hr;

        LONG status = ( *m_dispatcher )( m_funcs, entry, args );
        if ( status )
        {
            ERR( "unix call %s failed, status %#lx\n", name, (unsigned long)status );
            return HRESULT_FROM_NT( status );
        }
        return S_OK;
    }

    HRESULT UnixTransport::MapStatus( int32_t status, int32_t err, const char *op, HRESULT error_hr )
    {
        switch ( status )
        {
            case XODUS_OK:
                return S_OK;
            case XODUS_TIMEOUT:
                ERR( "%s: request timed out\n", op );
                return HRESULT_FROM_WIN32( ERROR_TIMEOUT );
            case XODUS_PEER_CLOSED:
                ERR( "%s: peer closed the connection\n", op );
                return HRESULT_FROM_WIN32( ERROR_GRACEFUL_DISCONNECT );
            case XODUS_ERROR:
                if ( error_hr == HRESULT_FROM_WIN32( ERROR_CONNECTION_REFUSED ) )
                    ERR( "connect to %s failed, errno %d; remedy: start the xodus service\n",
                         m_opts.socket_path.empty() ? "<default path>" : m_opts.socket_path.c_str(), (int)err );
                else
                    ERR( "%s failed, errno %d\n", op, (int)err );
                return error_hr;
            default:
                ERR( "invalid reply: invalid unix-call result, status %d\n", (int)status );
                Close();
                return HRESULT_FROM_WIN32( ERROR_INVALID_DATA );
        }
    }

    HRESULT UnixTransport::Connect( uint32_t timeout_ms )
    {
        xodus_connect_args args = {};
        args.path = m_opts.socket_path.empty() ? nullptr : m_opts.socket_path.c_str();
        args.timeout_ms = timeout_ms;
        args.expected_uid = m_opts.expected_uid;

        HRESULT hr = Call( XODUS_CALL_CONNECT, "connect", &args );
        if ( FAILED( hr ) ) return hr;
        return MapStatus( args.status, args.err, "connect", HRESULT_FROM_WIN32( ERROR_CONNECTION_REFUSED ) );
    }

    HRESULT UnixTransport::Send( const uint8_t *p, size_t n, uint32_t timeout_ms )
    {
        xodus_send_args args = {};
        args.buf = p;
        args.len = (uint32_t)n;
        args.timeout_ms = timeout_ms;

        HRESULT hr = Call( XODUS_CALL_SEND, "send", &args );
        if ( FAILED( hr ) ) return hr;
        return MapStatus( args.status, args.err, "send", HRESULT_FROM_WIN32( ERROR_CONNECTION_ABORTED ) );
    }

    HRESULT UnixTransport::Recv( uint8_t *buf, size_t cap, size_t &got, uint32_t timeout_ms )
    {
        got = 0;

        xodus_recv_args args = {};
        args.buf = buf;
        args.cap = (uint32_t)std::min<size_t>( cap, XODUS_RECV_MAX );
        args.timeout_ms = timeout_ms;

        HRESULT hr = Call( XODUS_CALL_RECV, "recv", &args );
        if ( FAILED( hr ) ) return hr;

        hr = MapStatus( args.status, args.err, "recv", HRESULT_FROM_WIN32( ERROR_CONNECTION_ABORTED ) );
        if ( FAILED( hr ) ) return hr;

        if ( args.got > args.cap )
        {
            ERR( "invalid reply: invalid unix-call result, recv returned %u bytes for a %u byte buffer\n",
                 (unsigned)args.got, (unsigned)args.cap );
            Close();
            return HRESULT_FROM_WIN32( ERROR_INVALID_DATA );
        }

        got = args.got;
        return S_OK;
    }

    void UnixTransport::Close()
    {
        if ( !m_dispatcher ) return;

        xodus_close_args args = {};
        Call( XODUS_CALL_CLOSE, "close", &args );
    }
}
