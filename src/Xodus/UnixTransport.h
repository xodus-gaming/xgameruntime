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

#ifndef XGAMERUNTIME_XODUS_UNIXTRANSPORT_H
#define XGAMERUNTIME_XODUS_UNIXTRANSPORT_H

#include "Transport.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Xodus
{
    /* Looks up a symbol by name: GetProcAddress on ntdll in production, a fake in tests. */
    using Resolver = std::function<void *( const char *name )>;

    /* Test seam: production uses the defaults (default socket path, geteuid()). */
    struct UnixTransportOptions
    {
        std::string socket_path;      /* UTF-8 host path; empty = default path */
        int64_t expected_uid = -1;    /* -1 = geteuid() */
    };

    /* Maps Transport onto the four entries of xgameruntime-unix.so. Not thread safe: XodusClient serializes. */
    class UnixTransport : public Transport
    {
    public:
        /* so_path is the Win32 path of the host library; it is loaded on first use. */
        UnixTransport( Resolver resolver, const std::wstring &so_path, UnixTransportOptions opts = {} );
        ~UnixTransport() override;

        HRESULT Connect( uint32_t timeout_ms ) override;
        HRESULT Send( const uint8_t *p, size_t n, uint32_t timeout_ms ) override;
        HRESULT Recv( uint8_t *buf, size_t cap, size_t &got, uint32_t timeout_ms ) override;
        void Close() override;

    private:
        using Dispatcher = LONG ( WINAPI * )( UINT64 handle, unsigned int entry, void *args );

        HRESULT Load();
        /* Dispatches one entry. A non-zero NTSTATUS becomes HRESULT_FROM_NT. */
        HRESULT Call( unsigned int entry, const char *name, void *args );
        /* Maps a unix-call status per design "Unix call ABI". Unknown values close the connection. */
        HRESULT MapStatus( int32_t status, int32_t err, const char *op, HRESULT error_hr );

        Resolver m_resolver;
        std::wstring m_so_path;
        UnixTransportOptions m_opts;
        UINT64 m_funcs = 0;
        Dispatcher *m_dispatcher = nullptr;
    };
}

#endif
