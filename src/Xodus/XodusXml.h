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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Xodus
{
    struct MsaTokenResult
    {
        std::string token;
        std::string errorCode;
    };

    /* Request XML is at most 0xFFFF bytes; larger gives E_INVALIDARG. */
    HRESULT BuildMsaTokenRequest( const std::string &clientId, bool allowUi, bool fullTrust, std::string &xml );

    /* S_OK with token set; E_FAIL with errorCode set for a service <Error>;
     * HRESULT_FROM_WIN32(ERROR_INVALID_DATA) for empty, malformed, truncated or oversized (> 0xFFFF) input. */
    HRESULT ParseMsaTokenResponse( const std::string &xml, MsaTokenResult &out );

    enum class AgeGroup { Unknown, Child, Teen, Adult };

    /* Identity claims of the signed-in user; an empty string means the claim is absent. */
    struct UserIdentity
    {
        std::string xuid, gamertag, modernGamertag, modernGamertagSuffix, uniqueModernGamertag, uhs;
        AgeGroup ageGroup = AgeGroup::Unknown;
    };

    struct UserInfoResult : UserIdentity
    {
        bool signedIn = false;
        std::string errorCode, message;
    };

    /* token is the full Authorization value (XBL3.0 x=<uhs>;<token>). */
    struct TokenAndSignatureResult : UserIdentity
    {
        std::string token, signature, errorCode, message;
        int64_t expiry = 0;
    };

    using HeaderList = std::vector<std::pair<std::string, std::string>>;

    /* Optional elements (ClientId, TitleId, Method, Headers, Body) are omitted when empty / 0.
     * E_INVALIDARG for an empty url or a request over 0xFFFF bytes; xml is then untouched. */
    HRESULT BuildUserInfoRequest( const std::string &clientId, uint32_t titleId, bool forceRefresh, std::string &xml );
    HRESULT BuildTokenAndSignatureRequest( const std::string &method, const std::string &url, const HeaderList &headers,
                                           const std::vector<uint8_t> &body, const std::string &clientId, uint32_t titleId,
                                           bool forceRefresh, std::string &xml );

    /* S_OK; E_FAIL with errorCode and message set for a service <Error> (all else empty);
     * HRESULT_FROM_WIN32(ERROR_INVALID_DATA) for empty, malformed, oversized (> 0xFFFF) or out-of-grammar input
     * (out is then default). */
    HRESULT ParseUserInfoResponse( const std::string &xml, UserInfoResult &out );
    HRESULT ParseTokenAndSignatureResponse( const std::string &xml, TokenAndSignatureResult &out );
}

#endif
