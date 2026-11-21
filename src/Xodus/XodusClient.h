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

#ifndef XGAMERUNTIME_XODUS_XODUSCLIENT_H
#define XGAMERUNTIME_XODUS_XODUSCLIENT_H

#include <windows.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "IpcFraming.h"
#include "Transport.h"
#include "XodusXml.h"

namespace Xodus
{
    /* Milliseconds from a monotonic clock; a test seam so deadlines are never slept. */
    using Clock = std::function<uint64_t()>;

    /* Synchronous client. One request is outstanding at a time (a mutex serializes callers);
     * every failure closes the connection and the next call reconnects. */
    class XodusClient
    {
    public:
        explicit XodusClient( Transport &t, Clock now = nullptr );
        ~XodusClient();

        XodusClient( const XodusClient & ) = delete;
        XodusClient &operator=( const XodusClient & ) = delete;

        HRESULT Ping();
        HRESULT MsaTokenRequest( const std::string &clientId, bool allowUi, bool fullTrust, MsaTokenResult &out );

        /* Same Exchange, locking, 5 s timeout and close-on-failure rules as MsaTokenRequest. */
        HRESULT UserInfo( const std::string &clientId, uint32_t titleId, bool forceRefresh, UserInfoResult &out );
        HRESULT TokenAndSignature( const std::string &method, const std::string &url, const HeaderList &headers,
                                   const std::vector<uint8_t> &body, const std::string &clientId, uint32_t titleId,
                                   bool forceRefresh, TokenAndSignatureResult &out );

        /* Idempotent. */
        void Close();

    private:
        HRESULT Exchange( uint16_t type, const std::vector<uint8_t> &request, uint16_t reply_type,
                          uint32_t timeout_ms, Frame &reply );
        void CloseLocked();

        Transport &m_transport;
        Clock m_now;
        std::mutex m_lock;
        FrameDecoder m_decoder;
        bool m_connected = false;
    };
}

#endif
