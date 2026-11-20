/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> XodusClient
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

#include "XodusClient.h"

#include <windows.h>
#include <private/logging.h>

namespace Xodus
{
    namespace
    {
        constexpr uint32_t TIMEOUT_MS = 5000;
        constexpr uint32_t TIMEOUT_UI_MS = 620000;
        /* One recv returns at most one maximum frame (8 + 0xFFFF). */
        constexpr size_t RECV_CHUNK = FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD;
    }

    XodusClient::XodusClient( Transport &t, Clock now )
        : m_transport( t ), m_now( now ? std::move( now ) : Clock( GetTickCount64 ) )
    {
    }

    XodusClient::~XodusClient()
    {
        Close();
    }

    void XodusClient::Close()
    {
        std::lock_guard<std::mutex> lock( m_lock );
        CloseLocked();
    }

    void XodusClient::CloseLocked()
    {
        m_decoder = FrameDecoder();
        if ( !m_connected ) return;
        m_connected = false;
        m_transport.Close();
    }

    HRESULT XodusClient::Ping()
    {
        std::lock_guard<std::mutex> lock( m_lock );

        Frame reply;
        return Exchange( FRAME_PING, Encode( FRAME_PING, nullptr, 0 ), FRAME_PONG, TIMEOUT_MS, reply );
    }

    HRESULT XodusClient::MsaTokenRequest( const std::string &clientId, bool allowUi, bool fullTrust, MsaTokenResult &out )
    {
        out = MsaTokenResult();

        std::string xml;
        HRESULT hr = BuildMsaTokenRequest( clientId, allowUi, fullTrust, xml );
        if ( FAILED( hr ) )
        {
            ERR( "request too large or invalid, hr %#lx\n", (unsigned long)hr );
            return hr;
        }

        std::vector<uint8_t> request = Encode( FRAME_MSA_TOKEN_REQUEST, (const uint8_t *)xml.data(), xml.size() );

        std::lock_guard<std::mutex> lock( m_lock );

        Frame reply;
        hr = Exchange( FRAME_MSA_TOKEN_REQUEST, request, FRAME_MSA_TOKEN_RESPONSE, allowUi ? TIMEOUT_UI_MS : TIMEOUT_MS, reply );
        if ( FAILED( hr ) ) return hr;

        hr = ParseMsaTokenResponse( std::string( reply.payload.begin(), reply.payload.end() ), out );
        if ( hr == E_FAIL )
            ERR( "service returned error code %s\n", out.errorCode.c_str() );
        else if ( FAILED( hr ) )
            ERR( "invalid reply: unparsable MSA token response, hr %#lx\n", (unsigned long)hr );
        if ( hr != S_OK ) CloseLocked();
        return hr;
    }

    HRESULT XodusClient::Exchange( uint16_t type, const std::vector<uint8_t> &request, uint16_t reply_type,
                                   uint32_t timeout_ms, Frame &reply )
    {
        const uint64_t deadline = m_now() + timeout_ms;
        auto remaining = [&]() -> uint32_t
        {
            uint64_t now = m_now();
            return now >= deadline ? 0 : (uint32_t)( deadline - now );
        };
        auto fail = [&]( HRESULT hr ) -> HRESULT
        {
            CloseLocked();
            return hr;
        };
        auto timed_out = [&]() -> HRESULT
        {
            ERR( "request timed out\n" );
            return fail( HRESULT_FROM_WIN32( ERROR_TIMEOUT ) );
        };
        auto invalid = [&]( const char *why ) -> HRESULT
        {
            ERR( "invalid reply: %s\n", why );
            return fail( HRESULT_FROM_WIN32( ERROR_INVALID_DATA ) );
        };

        if ( !m_connected )
        {
            HRESULT hr = m_transport.Connect( remaining() );
            if ( FAILED( hr ) )
            {
                ERR( "connect failed, hr %#lx\n", (unsigned long)hr );
                return hr;
            }
            m_connected = true;
        }

        uint32_t left = remaining();
        if ( !left ) return timed_out();
        HRESULT hr = m_transport.Send( request.data(), request.size(), left );
        if ( FAILED( hr ) )
        {
            ERR( "send of frame type %u failed, hr %#lx\n", (unsigned)type, (unsigned long)hr );
            return fail( hr );
        }

        std::vector<uint8_t> buf( RECV_CHUNK );
        for ( ;; )
        {
            left = remaining();
            if ( !left ) return timed_out();

            size_t got = 0;
            hr = m_transport.Recv( buf.data(), buf.size(), got, left );
            if ( FAILED( hr ) )
            {
                ERR( "recv failed, hr %#lx\n", (unsigned long)hr );
                return fail( hr );
            }
            if ( got == 0 || got > buf.size() ) return invalid( "empty or oversized recv" );

            m_decoder.Feed( buf.data(), got );
            hr = m_decoder.Next( reply );
            if ( hr == S_FALSE ) continue;
            if ( FAILED( hr ) ) return invalid( "bad frame" );
            break;
        }

        if ( reply.type != reply_type ) return invalid( "wrong type" );
        if ( m_decoder.Pending() ) return invalid( "extra bytes" );
        return S_OK;
    }
}
