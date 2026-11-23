/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUser identity
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

#include "XUserIdentity.h"

#include <cstdint>
#include <cstring>
#include <xgameerr.h>

XUser *g_user = nullptr;
std::mutex g_userLock;

namespace
{
    constexpr UINT64 LOCAL_ID = 1;

    /* 0x followed by 1 to 8 hex digits and a failure HRESULT; anything else is E_FAIL. */
    HRESULT ParseErrorCode( const std::string &s )
    {
        if ( s.size() < 3 || s.size() > 10 || s[0] != '0' || ( s[1] != 'x' && s[1] != 'X' ) ) return E_FAIL;

        UINT32 v = 0;
        for ( size_t i = 2; i < s.size(); i++ )
        {
            char c = s[i];
            UINT32 d;
            if ( c >= '0' && c <= '9' ) d = c - '0';
            else if ( c >= 'a' && c <= 'f' ) d = c - 'a' + 10;
            else if ( c >= 'A' && c <= 'F' ) d = c - 'A' + 10;
            else return E_FAIL;
            v = v << 4 | d;
        }

        HRESULT hr = (HRESULT)v;
        return FAILED( hr ) ? hr : E_FAIL;
    }

    /* Decimal digits only, non-zero, fits in 64 bits. */
    bool ParseXuid( const std::string &s, UINT64 &xuid )
    {
        if ( s.empty() ) return false;

        UINT64 v = 0;
        for ( char c : s )
        {
            if ( c < '0' || c > '9' ) return false;
            UINT32 d = c - '0';
            if ( v > ( UINT64_MAX - d ) / 10 ) return false;
            v = v * 10 + d;
        }

        xuid = v;
        return v != 0;
    }

    XUserAgeGroup MapAgeGroup( Xodus::AgeGroup g )
    {
        switch ( g )
        {
            case Xodus::AgeGroup::Child: return XUserAgeGroup::Child;
            case Xodus::AgeGroup::Teen: return XUserAgeGroup::Teen;
            case Xodus::AgeGroup::Adult: return XUserAgeGroup::Adult;
            default: return XUserAgeGroup::Unknown;
        }
    }
}

UserInfoQuery BuildUserInfoQuery( HRESULT titleIdHr, UINT32 titleId, HRESULT appIdHr, const std::string &appId )
{
    UserInfoQuery q;
    q.titleId = SUCCEEDED( titleIdHr ) ? titleId : 0;
    if ( SUCCEEDED( appIdHr ) ) q.clientId = appId;
    return q;
}

HRESULT IdentityFromUserInfo( const Xodus::UserInfoResult &r, UserRecord &out )
{
    /* A service error reply arrives with signedIn false, so the error code is checked first. */
    if ( !r.errorCode.empty() ) return ParseErrorCode( r.errorCode );
    if ( !r.signedIn ) return E_GAMEUSER_NO_DEFAULT_USER;

    UINT64 xuid;
    if ( !ParseXuid( r.xuid, xuid ) ) return HRESULT_FROM_WIN32( ERROR_INVALID_DATA );

    out.xuid = xuid;
    out.gamertag = r.gamertag;
    out.modernGamertag = r.modernGamertag;
    out.modernGamertagSuffix = r.modernGamertagSuffix;
    out.uniqueModernGamertag = r.uniqueModernGamertag;
    out.ageGroup = MapAgeGroup( r.ageGroup );
    return S_OK;
}

bool UserInfoIsTransportFailure( HRESULT hr, const Xodus::UserInfoResult &r )
{
    if ( hr == S_OK ) return false;
    return !( hr == E_FAIL && !r.errorCode.empty() );
}

HRESULT GamertagComponent( const UserRecord &id, XUserGamertagComponent c, SIZE_T size, char *buf, SIZE_T *used )
{
    const std::string *s;
    switch ( c )
    {
        case XUserGamertagComponent::Classic: s = &id.gamertag; break;
        case XUserGamertagComponent::Modern: s = &id.modernGamertag; break;
        case XUserGamertagComponent::ModernSuffix: s = &id.modernGamertagSuffix; break;
        case XUserGamertagComponent::UniqueModern: s = &id.uniqueModernGamertag; break;
        default: return E_INVALIDARG;
    }

    SIZE_T need = s->size() + 1;
    *used = need;
    if ( !buf || size < need ) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );

    memcpy( buf, s->c_str(), need );
    return S_OK;
}

XUser *UserCreateOrRefresh( const UserRecord &id )
{
    std::lock_guard<std::mutex> lock( g_userLock );

    if ( !g_user )
    {
        g_user = new XUser();
        g_user->signature = XUSER_SIGNATURE;
        g_user->refs = 1; /* internal reference, never released */
        g_user->localId.value = LOCAL_ID;
    }

    g_user->xuid = id.xuid;
    g_user->ageGroup = id.ageGroup;
    g_user->gamertag = id.gamertag;
    g_user->modernGamertag = id.modernGamertag;
    g_user->modernGamertagSuffix = id.modernGamertagSuffix;
    g_user->uniqueModernGamertag = id.uniqueModernGamertag;

    UserAddRef( g_user );
    return g_user;
}

XUser *UserFindById( UINT64 xuid )
{
    std::lock_guard<std::mutex> lock( g_userLock );

    if ( !g_user || g_user->xuid != xuid ) return nullptr;
    UserAddRef( g_user );
    return g_user;
}

XUser *UserFindByLocalId( XUserLocalId id )
{
    std::lock_guard<std::mutex> lock( g_userLock );

    if ( !g_user || g_user->localId.value != id.value ) return nullptr;
    UserAddRef( g_user );
    return g_user;
}

void UserSnapshot( const XUser *u, UserRecord &out )
{
    std::lock_guard<std::mutex> lock( g_userLock );

    out.xuid = u->xuid;
    out.ageGroup = u->ageGroup;
    out.gamertag = u->gamertag;
    out.modernGamertag = u->modernGamertag;
    out.modernGamertagSuffix = u->modernGamertagSuffix;
    out.uniqueModernGamertag = u->uniqueModernGamertag;
}
