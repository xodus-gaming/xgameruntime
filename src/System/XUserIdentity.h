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

#ifndef XGAMERUNTIME_XUSER_IDENTITY_H
#define XGAMERUNTIME_XUSER_IDENTITY_H

#include <windows.h>
#include <mutex>
#include <string>
#include <xuser.h>

#include "../Xodus/XodusXml.h"
#include "XUserHandle.h"

struct UserRecord
{
    UINT64 xuid;
    std::string gamertag, modernGamertag, modernGamertagSuffix, uniqueModernGamertag;
    XUserAgeGroup ageGroup;
};

struct UserInfoQuery
{
    std::string clientId;
    UINT32 titleId;
};

/* A GameConfig value that could not be read is left out of the request (empty clientId, titleId 0). */
UserInfoQuery BuildUserInfoQuery( HRESULT titleIdHr, UINT32 titleId, HRESULT appIdHr, const std::string &appId );

/* E_GAMEUSER_NO_DEFAULT_USER when signed out, the service's HRESULT for an error reply (E_FAIL when
 * it is not a parsable failure code), ERROR_INVALID_DATA for a bad xuid, else S_OK. */
HRESULT IdentityFromUserInfo( const Xodus::UserInfoResult &r, UserRecord &out );

/* The service could not be reached or sent an unusable reply; callers see "no default user". */
bool UserInfoIsTransportFailure( HRESULT hr, const Xodus::UserInfoResult &r );

/* Copies the component with its terminator and writes the size used to *used. When buf is null or
 * too small, writes the required size to *used and returns ERROR_INSUFFICIENT_BUFFER. */
HRESULT GamertagComponent( const UserRecord &id, XUserGamertagComponent c, SIZE_T size, char *buf, SIZE_T *used );

/* The one user object per process and the mutex guarding its identity fields. */
extern XUser *g_user;
extern std::mutex g_userLock;

/* Creates g_user on first use, stores the identity in it and returns it after one UserAddRef. */
XUser *UserCreateOrRefresh( const UserRecord &id );

/* Null when no user matches, else the user after one UserAddRef. */
XUser *UserFindById( UINT64 xuid );
XUser *UserFindByLocalId( XUserLocalId id );

/* Locked read of the identity fields of u. */
void UserSnapshot( const XUser *u, UserRecord &out );

#endif
