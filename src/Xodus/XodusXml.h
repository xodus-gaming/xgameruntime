/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> XodusXml
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

#ifndef XGAMERUNTIME_XODUS_XODUSXML_H
#define XGAMERUNTIME_XODUS_XODUSXML_H

#include <windows.h>

#include <string>

namespace Xodus
{
    struct MsaTokenResult
    {
        std::string token;
        std::string errorCode;
        std::string errorText; /* UTF-8 */
    };

    /* Request XML is at most 0xFFFF bytes; larger gives E_INVALIDARG. */
    HRESULT BuildMsaTokenRequest( const std::string &clientId, bool allowUi, bool fullTrust, std::string &xml );

    /* S_OK with token set; E_FAIL with errorCode set for a service <Error>;
     * HRESULT_FROM_WIN32(ERROR_INVALID_DATA) for empty, malformed, truncated or oversized (> 0xFFFF) input. */
    HRESULT ParseMsaTokenResponse( const std::string &xml, MsaTokenResult &out );
}

#endif
