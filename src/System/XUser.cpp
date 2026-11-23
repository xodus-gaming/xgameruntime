/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUser
 *
 * Copyright (C) 2026 Olivia Ryan
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

#include "../private.h"

#include "../GameConfig.h"
#include "../Xodus/SharedClient.h"
#include "XUserHandle.h"
#include "XUserIdentity.h"

#include <atomic>
#include <mutex>
#include <set>

static std::mutex g_change_lock;
static std::set<UINT64> g_change_tokens;
static UINT64 g_change_next = 1;

/* Holds the user between DoWork and the result call; owns one reference until handed out. */
struct XUserAddState
{
    XUser *user = nullptr;
};

static HRESULT XUserAddWork( XUserAddState *state )
{
    UINT32 titleId = 0;
    std::string appId;
    HRESULT titleIdHr = GameConfig::GetTitleId( &titleId );
    HRESULT appIdHr = GameConfig::GetMsaAppId( appId );
    UserInfoQuery query = BuildUserInfoQuery( titleIdHr, titleId, appIdHr, appId );

    Xodus::XodusClient *client = Xodus::SharedClient();
    if (!client)
    {
        WARN( "xodus client unavailable, no user can be added; check that xgameruntime-unix.so is next to xgameruntime.dll.\n" );
        return E_GAMEUSER_NO_DEFAULT_USER;
    }

    Xodus::UserInfoResult info;
    HRESULT hr = client->UserInfo( query.clientId, query.titleId, false, info );
    if (UserInfoIsTransportFailure( hr, info ))
    {
        WARN( "xodus-service unreachable (hr %#lx), no user can be added; start xodus-service.\n", (unsigned long)hr );
        return E_GAMEUSER_NO_DEFAULT_USER;
    }

    UserRecord record;
    hr = IdentityFromUserInfo( info, record );
    if (hr == E_GAMEUSER_NO_DEFAULT_USER)
    {
        TRACE( "xodus-service reports no signed-in user; sign in to Xbox in xodus-service.\n" );
        return hr;
    }
    if (FAILED( hr ))
    {
        TRACE( "xodus-service identity rejected, hr %#lx.\n", (unsigned long)hr );
        return hr;
    }

    state->user = UserCreateOrRefresh( record );
    TRACE( "added user xuid %llu, gamertag %s.\n", (unsigned long long)record.xuid, record.gamertag.c_str() );
    return S_OK;
}

static HRESULT CALLBACK XUserAddProvider( XAsyncOp op, const XAsyncProviderData *data )
{
    XUserAddState *state = static_cast<XUserAddState *>( data->context );

    switch (op)
    {
        case XAsyncOp::Begin:
            return XAsyncSchedule( data->async, 0 );

        case XAsyncOp::DoWork:
        {
            HRESULT hr = XUserAddWork( state );
            XAsyncComplete( data->async, hr, SUCCEEDED( hr ) ? sizeof(XUserHandle) : 0 );
            return hr;
        }

        case XAsyncOp::GetResult:
            *static_cast<XUserHandle *>( data->buffer ) = state->user;
            state->user = nullptr;
            return S_OK;

        case XAsyncOp::Cleanup:
            if (state->user) UserRelease( state->user );
            delete state;
            return S_OK;

        default:
            return S_OK;
    }
}

class XUserImpl :
    public IXUserImpl6, public IXUserGamertagImpl
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXUserImpl ) ||
             iid == __uuidof( IXUserImpl2 ) ||
             iid == __uuidof( IXUserImpl3 ) ||
             iid == __uuidof( IXUserImpl4 ) ||
             iid == __uuidof( IXUserImpl5 ) ||
             iid == __uuidof( IXUserImpl6 ) )
        {
            AddRef();
            *out = static_cast<IXUserImpl6 *>(this);
            return S_OK;
        }

        if ( iid == __uuidof( IXUserGamertagImpl ) )
        {
            AddRef();
            *out = static_cast<IXUserGamertagImpl *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>(++ref);
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );
        return curr;
    }

    HRESULT WINAPI XUserDuplicateHandle( XUserHandle handle, XUserHandle *duplicatedHandle ) override
    {
        TRACE( "handle %p, duplicatedHandle %p.\n", handle, duplicatedHandle );

        if (!duplicatedHandle) return E_INVALIDARG;
        *duplicatedHandle = nullptr;
        if (!UserIsValid( handle )) return E_INVALIDARG;

        UserAddRef( handle );
        *duplicatedHandle = handle;
        return S_OK;
    }

    void WINAPI XUserCloseHandle( XUserHandle user ) override
    {
        TRACE( "user %p.\n", user );

        if (!UserIsValid( user ))
        {
            WARN( "invalid user handle %p, ignoring.\n", user );
            return;
        }
        UserRelease( user );
    }

    INT32 WINAPI XUserCompare( XUserHandle user1, XUserHandle user2 ) override
    {
        TRACE( "user1 %p, user2 %p.\n", user1, user2 );
        return UserCompare( user1, user2 );
    }

    HRESULT WINAPI XUserGetMaxUsers( UINT32 *maxUsers ) override
    {
        TRACE( "maxUsers %p.\n", maxUsers );

        if (!maxUsers) return E_INVALIDARG;
        *maxUsers = 1;
        return S_OK;
    }

    HRESULT WINAPI XUserAddAsync( XUserAddOptions options, XAsyncBlock *async ) override
    {
        TRACE( "options %#x, async %p.\n", (UINT32)options, async );

        if (!async) return E_INVALIDARG;

        XUserAddState *state = new XUserAddState();
        HRESULT hr = XAsyncBegin( async, state, reinterpret_cast<const void *>( XUserAddProvider ), "XUserAddAsync", XUserAddProvider );
        if (FAILED( hr )) delete state;
        return hr;
    }

    HRESULT WINAPI XUserAddResult( XAsyncBlock *async, XUserHandle *newUser ) override
    {
        TRACE( "async %p, newUser %p.\n", async, newUser );

        if (!async || !newUser) return E_INVALIDARG;
        *newUser = nullptr;

        return XAsyncGetResult( async, reinterpret_cast<const void *>( XUserAddProvider ), sizeof(XUserHandle), newUser, nullptr );
    }

    HRESULT WINAPI XUserGetLocalId( XUserHandle user, XUserLocalId *userLocalId ) override
    {
        TRACE( "user %p, userLocalId %p.\n", user, userLocalId );

        if (!userLocalId || !UserIsValid( user )) return E_INVALIDARG;
        *userLocalId = user->localId;
        return S_OK;
    }

    HRESULT WINAPI XUserFindUserByLocalId( XUserLocalId userLocalId, XUserHandle *handle ) override
    {
        TRACE( "userLocalId %llu, handle %p.\n", userLocalId.value, handle );

        if (!handle) return E_INVALIDARG;
        *handle = UserFindByLocalId( userLocalId );
        return *handle ? S_OK : E_GAMEUSER_USER_NOT_FOUND;
    }

    HRESULT WINAPI XUserGetId( XUserHandle user, UINT64 *userId ) override
    {
        TRACE( "user %p, userId %p.\n", user, userId );

        if (!userId || !UserIsValid( user )) return E_INVALIDARG;
        UserRecord record;
        UserSnapshot( user, record );
        *userId = record.xuid;
        return S_OK;
    }

    HRESULT WINAPI XUserFindUserById( UINT64 userId, XUserHandle *handle ) override
    {
        TRACE( "userId %llu, handle %p.\n", userId, handle );

        if (!handle) return E_INVALIDARG;
        *handle = UserFindById( userId );
        return *handle ? S_OK : E_GAMEUSER_USER_NOT_FOUND;
    }

    HRESULT WINAPI XUserGetIsGuest( XUserHandle user, BOOLEAN *isGuest ) override
    {
        TRACE( "user %p, isGuest %p.\n", user, isGuest );

        if (!isGuest || !UserIsValid( user )) return E_INVALIDARG;
        *isGuest = FALSE;
        return S_OK;
    }

    HRESULT WINAPI XUserGetState( XUserHandle user, XUserState *state ) override
    {
        TRACE( "user %p, state %p.\n", user, state );

        if (!state || !UserIsValid( user )) return E_INVALIDARG;
        *state = XUserState::SignedIn;
        return S_OK;
    }

    HRESULT WINAPI __PADDING__() override
    {
        WARN("padding function called!\n");
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetGamerPictureAsync( XUserHandle user, XUserGamerPictureSize pictureSize, XAsyncBlock *async ) override
    {
        FIXME( "user %p, pictureSize %u, async %p; no Xbox Live identity source, returning E_NOTIMPL.\n", user, (UINT32)pictureSize, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetGamerPictureResultSize( XAsyncBlock *async, SIZE_T *bufferSize ) override
    {
        FIXME( "async %p, bufferSize %p; no Xbox Live identity source, returning E_NOTIMPL.\n", async, bufferSize );
        if (bufferSize) *bufferSize = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetGamerPictureResult( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed ) override
    {
        FIXME( "async %p, bufferSize %zu, buffer %p, bufferUsed %p; no Xbox Live identity source, returning E_NOTIMPL.\n", async, bufferSize, buffer, bufferUsed );
        if (bufferUsed) *bufferUsed = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetAgeGroup( XUserHandle user, XUserAgeGroup *ageGroup ) override
    {
        TRACE( "user %p, ageGroup %p.\n", user, ageGroup );

        if (!ageGroup || !UserIsValid( user )) return E_INVALIDARG;
        UserRecord record;
        UserSnapshot( user, record );
        *ageGroup = record.ageGroup;
        return S_OK;
    }

    HRESULT WINAPI XUserCheckPrivilege( XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, BOOLEAN *hasPrivilege, XUserPrivilegeDenyReason *reason ) override
    {
        TRACE( "user %p, options %u, privilege %u, hasPrivilege %p, reason %p.\n", user, (UINT32)options, (UINT32)privilege, hasPrivilege, reason );

        if (!hasPrivilege || !UserIsValid( user )) return E_INVALIDARG;
        *hasPrivilege = TRUE;
        if (reason) *reason = XUserPrivilegeDenyReason::None;
        return S_OK;
    }

    HRESULT WINAPI XUserResolvePrivilegeWithUiAsync( XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, XAsyncBlock *async ) override
    {
        FIXME( "user %p, options %u, privilege %u, async %p; no system UI, returning E_NOTIMPL.\n", user, (UINT32)options, (UINT32)privilege, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserResolvePrivilegeWithUiResult( XAsyncBlock *async ) override
    {
        FIXME( "async %p; no system UI, returning E_NOTIMPL.\n", async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureAsync( XUserHandle user, XUserGetTokenAndSignatureOptions options, const char *method, const char *url, SIZE_T headerCount, const XUserGetTokenAndSignatureHttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async ) override
    {
        FIXME( "user %p, options %u, method %p, url %p, headerCount %zu, headers %p, bodySize %zu, bodyBuffer %p, async %p; no token source, returning E_NOTIMPL.\n", user, (UINT32)options, method, url, headerCount, headers, bodySize, bodyBuffer, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureResultSize( XAsyncBlock *async, SIZE_T *bufferSize ) override
    {
        FIXME( "async %p, bufferSize %p; no token source, returning E_NOTIMPL.\n", async, bufferSize );
        if (bufferSize) *bufferSize = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureResult( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureData **ptrToBuffer, SIZE_T *bufferUsed ) override
    {
        FIXME( "async %p, bufferSize %zu, buffer %p, ptrToBuffer %p, bufferUsed %p; no token source, returning E_NOTIMPL.\n", async, bufferSize, buffer, ptrToBuffer, bufferUsed );
        if (ptrToBuffer) *ptrToBuffer = nullptr;
        if (bufferUsed) *bufferUsed = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureUtf16Async( XUserHandle user, XUserGetTokenAndSignatureOptions options, const WCHAR *method, const WCHAR *url, SIZE_T headerCount, const XUserGetTokenAndSignatureUtf16HttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async ) override
    {
        FIXME( "user %p, options %u, method %p, url %p, headerCount %zu, headers %p, bodySize %zu, bodyBuffer %p, async %p; no token source, returning E_NOTIMPL.\n", user, (UINT32)options, method, url, headerCount, headers, bodySize, bodyBuffer, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureUtf16ResultSize( XAsyncBlock *async, SIZE_T *bufferSize ) override
    {
        FIXME( "async %p, bufferSize %p; no token source, returning E_NOTIMPL.\n", async, bufferSize );
        if (bufferSize) *bufferSize = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetTokenAndSignatureUtf16Result( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureUtf16Data **ptrToBuffer, SIZE_T *bufferUsed ) override
    {
        FIXME( "async %p, bufferSize %zu, buffer %p, ptrToBuffer %p, bufferUsed %p; no token source, returning E_NOTIMPL.\n", async, bufferSize, buffer, ptrToBuffer, bufferUsed );
        if (ptrToBuffer) *ptrToBuffer = nullptr;
        if (bufferUsed) *bufferUsed = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserResolveIssueWithUiAsync( XUserHandle user, const char *url, XAsyncBlock *async ) override
    {
        FIXME( "user %p, url %p, async %p; no system UI, returning E_NOTIMPL.\n", user, url, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserResolveIssueWithUiResult( XAsyncBlock *async ) override
    {
        FIXME( "async %p; no system UI, returning E_NOTIMPL.\n", async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserResolveIssueWithUiUtf16Async( XUserHandle user, const WCHAR *url, XAsyncBlock *async ) override
    {
        FIXME( "user %p, url %p, async %p; no system UI, returning E_NOTIMPL.\n", user, url, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserResolveIssueWithUiUtf16Result( XAsyncBlock *async ) override
    {
        FIXME( "async %p; no system UI, returning E_NOTIMPL.\n", async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserRegisterForChangeEvent( XTaskQueueHandle queue, void *context, XUserChangeEventCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        TRACE( "queue %p, context %p, callback %p, token %p.\n", queue, context, callback, token );

        if (!callback || !token) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( g_change_lock );
        token->token = g_change_next++;
        g_change_tokens.insert( token->token );
        return S_OK;
    }

    BOOLEAN WINAPI XUserUnregisterForChangeEvent( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        TRACE( "token %llu, wait %d.\n", token.token, wait );

        std::lock_guard<std::mutex> lock( g_change_lock );
        return g_change_tokens.erase( token.token ) ? TRUE : FALSE;
    }

    HRESULT WINAPI XUserGetSignOutDeferral( XUserSignOutDeferralHandle *deferral ) override
    {
        TRACE( "deferral %p.\n", deferral );

        if (!deferral) return E_INVALIDARG;
        *deferral = nullptr;
        return E_GAMEUSER_DEFERRAL_NOT_AVAILABLE;
    }

    void WINAPI XUserCloseSignOutDeferralHandle( XUserSignOutDeferralHandle deferral ) override
    {
        TRACE( "deferral %p.\n", deferral );
    }

    HRESULT WINAPI XUserAddByIdWithUiAsync( UINT64 userId, XAsyncBlock *async ) override
    {
        FIXME( "userId %llu, async %p; no system UI, returning E_NOTIMPL.\n", userId, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserAddByIdWithUiResult( XAsyncBlock *async, XUserHandle *newUser ) override
    {
        FIXME( "async %p, newUser %p; no system UI, returning E_NOTIMPL.\n", async, newUser );
        if (newUser) *newUser = nullptr;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetMsaTokenSilentlyAsync( XUserHandle user, XUserGetMsaTokenSilentlyOptions options, const char *scope, XAsyncBlock *async ) override
    {
        FIXME( "user %p, options %u, scope %p, async %p; no token source, returning E_NOTIMPL.\n", user, (UINT32)options, scope, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetMsaTokenSilentlyResult( XAsyncBlock *async, SIZE_T resultTokenSize, char *resultToken, SIZE_T *resultTokenUsed ) override
    {
        FIXME( "async %p, resultTokenSize %zu, resultToken %p, resultTokenUsed %p; no token source, returning E_NOTIMPL.\n", async, resultTokenSize, resultToken, resultTokenUsed );
        if (resultTokenUsed) *resultTokenUsed = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetMsaTokenSilentlyResultSize( XAsyncBlock *async, SIZE_T *tokenSize ) override
    {
        FIXME( "async %p, tokenSize %p; no token source, returning E_NOTIMPL.\n", async, tokenSize );
        if (tokenSize) *tokenSize = 0;
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XUserIsStoreUser( XUserHandle user ) override
    {
        TRACE( "user %p.\n", user );
        return UserIsValid( user ) ? TRUE : FALSE;
    }

    HRESULT WINAPI XUserPlatformRemoteConnectSetEventHandlers( XTaskQueueHandle queue, XUserPlatformRemoteConnectEventHandlers *handlers ) override
    {
        FIXME( "queue %p, handlers %p; no platform sign-in prompts, returning E_NOTIMPL.\n", queue, handlers );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserPlatformRemoteConnectCancelPrompt( XUserPlatformOperation operation ) override
    {
        FIXME( "operation %p; no platform sign-in prompts, returning E_NOTIMPL.\n", operation );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserPlatformSpopPromptSetEventHandlers( XTaskQueueHandle queue, XUserPlatformSpopPromptEventHandler *handler, void *context ) override
    {
        FIXME( "queue %p, handler %p, context %p; no platform sign-in prompts, returning E_NOTIMPL.\n", queue, handler, context );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserPlatformSpopPromptComplete( XUserPlatformOperation operation, XUserPlatformOperationResult result ) override
    {
        FIXME( "operation %p, result %u; no platform sign-in prompts, returning E_NOTIMPL.\n", operation, (UINT32)result );
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XUserIsSignOutPresent() override
    {
        TRACE( "returning FALSE.\n" );
        return FALSE;
    }

    HRESULT WINAPI XUserSignOutAsync( XUserHandle user, XAsyncBlock *async ) override
    {
        FIXME( "user %p, async %p; no user can be signed in, returning E_NOTIMPL.\n", user, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserSignOutResult( XAsyncBlock *async ) override
    {
        FIXME( "async %p; no user can be signed in, returning E_NOTIMPL.\n", async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserGetGamertag( XUserHandle user, XUserGamertagComponent gamertagComponent, SIZE_T gamertagSize, char *gamertag, SIZE_T *gamertagUsed ) override
    {
        TRACE( "user %p, gamertagComponent %u, gamertagSize %zu, gamertag %p, gamertagUsed %p.\n", user, (UINT32)gamertagComponent, gamertagSize, gamertag, gamertagUsed );

        if (!UserIsValid( user )) return E_INVALIDARG;
        if (!gamertag && gamertagSize) return E_INVALIDARG;

        UserRecord record;
        UserSnapshot( user, record );
        SIZE_T used = 0;
        HRESULT hr = GamertagComponent( record, gamertagComponent, gamertagSize, gamertag, &used );
        if (gamertagUsed && (hr == S_OK || hr == HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ))) *gamertagUsed = used;
        return hr;
    }

private:
    std::atomic_long ref{ 1 };
};

static XUserImpl g_x_user;

IXUserImpl *x_user_impl = static_cast<IXUserImpl *>(&g_x_user);
