/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUser
 *
 * Copyright 2026 Olivia Ryan
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

#include "private.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

struct x_user
{
    IXUserImpl6 IXUserImpl6_iface;
    IXUserGamertagImpl IXUserGamertagImpl_iface;
    IXUserDeviceImpl2 IXUserDeviceImpl2_iface;
    LONG ref;
};

/*
 * The signed-in user.
 *
 * The identity comes from xodus-service, which holds the account: signing in
 * asks it for an XSTS token for http://xboxlive.com and takes the XUID and
 * gamertag out of the token's xui claim, so what the title displays is what the
 * tokens it sends actually say.
 *
 * The fallback values below are used only when the service cannot be reached.
 * They keep single-player titles running -- refusing to produce a user at all
 * stops them before they reach their menu -- but nothing derived from a real
 * account is invented: XUserGetTokenAndSignature fails outright in that case.
 */
struct user_object
{
    LONG refcount;
    UINT64 xuid;
    XUserLocalId local_id;
    char gamertag[64];
};

static struct user_object *user_singleton;
static CRITICAL_SECTION user_cs;
static CRITICAL_SECTION_DEBUG user_cs_debug =
{
    0, 0, &user_cs,
    { &user_cs_debug.ProcessLocksList, &user_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": user_cs") }
};
static CRITICAL_SECTION user_cs = { &user_cs_debug, -1, 0, 0, 0, 0 };

/*
 * A handle is its own object, not the user it names.
 *
 * Every XUserAddAsync and XUserDuplicateHandle hands back a DISTINCT handle for
 * the same user -- which is precisely why XUserCompare exists, since comparing
 * handle values tells a title nothing. Returning one shared pointer instead
 * makes every add look like the handle the title already holds; Asphalt Legends
 * reads that as "no new user" and re-adds forever without ever signing in.
 */
struct user_handle
{
    struct user_object *user;
};

static inline struct user_object *user_from_handle( XUserHandle handle )
{
    struct user_handle *impl = (struct user_handle *)handle;
    return impl ? impl->user : NULL;
}

/* One process-wide user, however many handles name it. */
static struct user_object *user_acquire(void)
{
    struct user_object *user;

    EnterCriticalSection( &user_cs );
    if (!(user = user_singleton) && (user = calloc( 1, sizeof(*user) )))
    {
        user->refcount = 0;
        user->xuid = 0;                     /* replaced at sign-in */
        user->local_id.value = 1;
        strcpy( user->gamertag, "XodusPlayer" );
        user_singleton = user;
    }
    if (user) InterlockedIncrement( &user->refcount );
    LeaveCriticalSection( &user_cs );

    return user;
}

/* Takes the reference user_acquire() returned; releases it if it cannot. */
static XUserHandle handle_from_user( struct user_object *user )
{
    struct user_handle *impl;

    if (!user) return NULL;
    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        InterlockedDecrement( &user->refcount );
        return NULL;
    }
    impl->user = user;
    return (XUserHandle)impl;
}

static inline struct x_user *impl_from_IXUserImpl6( IXUserImpl6 *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_QueryInterface( IXUserImpl6 *iface, REFIID iid, void **out )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown    ) ||
        IsEqualGUID( iid, &IID_IXUserImpl  ) ||
        IsEqualGUID( iid, &IID_IXUserImpl2 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl3 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl4 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl5 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl6 ))
    {
        IXUserImpl6_AddRef( *out = &impl->IXUserImpl6_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXUserGamertagImpl ))
    {
        IXUserGamertagImpl_AddRef( *out = &impl->IXUserGamertagImpl_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_user_AddRef( IXUserImpl6 *iface )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_user_Release( IXUserImpl6 *iface )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI x_user_XUserDuplicateHandle( IXUserImpl6 *iface, XUserHandle handle, XUserHandle *duplicatedHandle )
{
    struct user_object *user = user_from_handle( handle );

    TRACE( "iface %p, handle %p, duplicatedHandle %p.\n", iface, handle, duplicatedHandle );

    if (!user || !duplicatedHandle) return E_INVALIDARG;

    InterlockedIncrement( &user->refcount );
    if (!(*duplicatedHandle = handle_from_user( user ))) return E_OUTOFMEMORY;
    return S_OK;
}

static void WINAPI x_user_XUserCloseHandle( IXUserImpl6 *iface, XUserHandle user_handle )
{
    struct user_object *user = user_from_handle( user_handle );

    TRACE( "iface %p, user %p.\n", iface, user_handle );

    /* The user outlives its handles: it is the process's signed-in user, and
     * titles routinely close a handle and ask for the user again. */
    if (user) InterlockedDecrement( &user->refcount );
    free( user_handle );
}

static INT32 WINAPI x_user_XUserCompare( IXUserImpl6 *iface, XUserHandle user1, XUserHandle user2 )
{
    struct user_object *a = user_from_handle( user1 ), *b = user_from_handle( user2 );

    TRACE( "iface %p, user1 %p, user2 %p.\n", iface, user1, user2 );


    /* Ordering by user, not by handle: two handles for one user are equal. */
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

static HRESULT WINAPI x_user_XUserGetMaxUsers( IXUserImpl6 *iface, UINT32 *maxUsers )
{
    TRACE( "iface %p, maxUsers %p.\n", iface, maxUsers );
    *maxUsers = 1;
    return S_OK;
}

/*
 * An XSTS token for http://xboxlive.com carries the account's XUID and gamertag
 * in its xui claim -- the same round trip that proves the account is reachable
 * also tells us who it is, so sign-in is one call.
 */
static void user_refresh_identity( struct user_object *user )
{
    static const char request[] = "<XstsTokenRequest><RelyingParty>http://xboxlive.com</RelyingParty>"
                                  "<ForceRefresh>false</ForceRefresh></XstsTokenRequest>";
    char *reply = NULL, *xuid, *gamertag;

    if (FAILED(xodus_service_call( XODUS_MSG_XSTS_TOKEN, request, &reply )))
    {
        FIXME( "no account from xodus-service; signing in as %s with no XUID.\n",
               debugstr_a( user->gamertag ) );
        return;
    }

    xuid = xodus_xml_element( reply, "Xuid" );
    gamertag = xodus_xml_element( reply, "Gamertag" );
    free( reply );

    /* An empty element means Xbox Live issued the token without profile claims;
     * keep whatever we already had rather than blanking a known good value. */
    if (xuid && *xuid) user->xuid = _strtoui64( xuid, NULL, 10 );
    if (gamertag && *gamertag) lstrcpynA( user->gamertag, gamertag, sizeof(user->gamertag) );

    TRACE( "signed in as %s (xuid %I64u).\n", debugstr_a( user->gamertag ), user->xuid );
    free( xuid );
    free( gamertag );
}

static HRESULT CALLBACK user_add_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    switch (op)
    {
    case XAsyncOp_Begin:
        /* Off the calling thread: reaching the service means an HTTPS round trip. */
        return IXThreadingImpl_XAsyncSchedule( x_threading_impl, data->async, 0 );

    case XAsyncOp_DoWork:
    {
        struct user_object *user = user_acquire();

        if (!user) return E_OUTOFMEMORY;
        user_refresh_identity( user );
        /* Balance the acquire: XUserAddResult takes the caller's reference. */
        InterlockedDecrement( &user->refcount );
        return S_OK;
    }

    default:
        return S_OK;
    }
}

static HRESULT WINAPI x_user_XUserAddAsync( IXUserImpl6 *iface, XUserAddOptions options, XAsyncBlock *async )
{
    TRACE( "iface %p, options %d, async %p.\n", iface, options, async );

    return IXThreadingImpl_XAsyncBegin( x_threading_impl, async, NULL, x_user_XUserAddAsync,
                                        "XUserAddAsync", user_add_provider );
}

static HRESULT WINAPI x_user_XUserAddResult( IXUserImpl6 *iface, XAsyncBlock *async, XUserHandle *newUser )
{
    HRESULT hr = xasync_peek_status( async );
    struct user_object *user;

    TRACE( "iface %p, async %p, newUser %p.\n", iface, async, newUser );

    if (!newUser) return E_POINTER;
    *newUser = NULL;
    if (FAILED(hr)) return hr;

    if (!(user = user_acquire())) return E_OUTOFMEMORY;
    *newUser = handle_from_user( user );
    return S_OK;
}

static HRESULT WINAPI x_user_XUserGetLocalId( IXUserImpl6 *iface, XUserHandle user_handle, XUserLocalId *userLocalId )
{
    struct user_object *user = user_from_handle( user_handle );

    TRACE( "iface %p, user %p, userLocalId %p.\n", iface, user_handle, userLocalId );

    if (!user || !userLocalId) return E_INVALIDARG;
    *userLocalId = user->local_id;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserFindUserByLocalId( IXUserImpl6 *iface, XUserLocalId userLocalId, XUserHandle *handle )
{
    struct user_object *user;

    TRACE( "iface %p, userLocalId %I64u, handle %p.\n", iface, userLocalId.value, handle );

    if (!handle) return E_POINTER;
    *handle = NULL;
    if (!(user = user_acquire())) return E_OUTOFMEMORY;
    if (user->local_id.value != userLocalId.value)
    {
        InterlockedDecrement( &user->refcount );
        return E_GAMEUSER_USER_NOT_FOUND;
    }
    *handle = handle_from_user( user );
    return S_OK;
}

static HRESULT WINAPI x_user_XUserGetId( IXUserImpl6 *iface, XUserHandle user_handle, UINT64 *userId )
{
    struct user_object *user = user_from_handle( user_handle );

    TRACE( "iface %p, user %p, userId %p.\n", iface, user_handle, userId );

    if (!user || !userId) return E_INVALIDARG;
    *userId = user->xuid;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserFindUserById( IXUserImpl6 *iface, UINT64 userId, XUserHandle *handle )
{
    struct user_object *user;

    TRACE( "iface %p, userId %I64u, handle %p.\n", iface, userId, handle );

    if (!handle) return E_POINTER;
    *handle = NULL;
    if (!(user = user_acquire())) return E_OUTOFMEMORY;
    if (user->xuid != userId)
    {
        InterlockedDecrement( &user->refcount );
        return E_GAMEUSER_USER_NOT_FOUND;
    }
    *handle = handle_from_user( user );
    return S_OK;
}

static HRESULT WINAPI x_user_XUserGetIsGuest( IXUserImpl6 *iface, XUserHandle user_handle, BOOLEAN *isGuest )
{
    TRACE( "iface %p, user %p, isGuest %p.\n", iface, user_handle, isGuest );

    if (!user_handle || !isGuest) return E_INVALIDARG;
    *isGuest = FALSE;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserGetState( IXUserImpl6 *iface, XUserHandle user_handle, XUserState *state )
{
    TRACE( "iface %p, user %p, state %p.\n", iface, user_handle, state );

    if (!user_handle || !state) return E_INVALIDARG;
    *state = XUserState_SignedIn;
    return S_OK;
}

static HRESULT WINAPI __PADDING__( IXUserImpl6 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureAsync( IXUserImpl6 *iface, XUserHandle user, XUserGamerPictureSize pictureSize, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, pictureSize %d, async %p stub!\n", iface, user, pictureSize, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    FIXME( "iface %p, async %p, bufferSize %p stub!\n", iface, async, bufferSize );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed )
{
    FIXME( "iface %p, async %p, bufferSize %Iu, buffer %p, bufferUsed %p stub!\n", iface, async, bufferSize, buffer, bufferUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetAgeGroup( IXUserImpl6 *iface, XUserHandle user_handle, XUserAgeGroup *ageGroup )
{
    TRACE( "iface %p, user %p, ageGroup %p.\n", iface, user_handle, ageGroup );

    if (!user_handle || !ageGroup) return E_INVALIDARG;
    /* Adult: the narrower groups gate features behind parental controls we
     * have no way to evaluate. */
    *ageGroup = XUserAgeGroup_Adult;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserCheckPrivilege( IXUserImpl6 *iface, XUserHandle user_handle, XUserPrivilegeOptions options, XUserPrivilege privilege, BOOLEAN *hasPrivilege, XUserPrivilegeDenyReason *reason )
{
    FIXME( "iface %p, user %p, options %d, privilege %d: granting.\n", iface, user_handle, options, privilege );

    if (!user_handle || !hasPrivilege) return E_INVALIDARG;
    /* Privileges are an Xbox Live account property. Nothing here can evaluate
     * them, and denying would gate multiplayer and social features that may
     * otherwise work, so grant and let the service be the authority. */
    *hasPrivilege = TRUE;
    if (reason) *reason = XUserPrivilegeDenyReason_None;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserResolvePrivilegeWithUiAsync( IXUserImpl6 *iface, XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, options %d, privilege %d, async %p stub!\n", iface, user, options, privilege, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolvePrivilegeWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

/*
 * XUserGetTokenAndSignature
 * -------------------------
 *
 * The title hands us the HTTP request it is about to make and expects back the
 * Authorization header for it, plus a proof-of-possession signature for the
 * Xbox Live endpoints that demand one.
 *
 * What comes back here is the token only. xodus-service holds the account and
 * mints an XSTS token for the relying party derived from the URL; Minecraft
 * Bedrock asks for https://b980a380.minecraft.playfabapi.com/ and gets a token
 * good for PlayFab, which does not check a signature. Requests to
 * *.xboxlive.com do check one, and producing it needs an ECDSA device key that
 * is registered during device authentication -- not implemented, so those
 * requests will be rejected by the service rather than silently mis-signed.
 */
struct token_request
{
    char *relying_party;
    char *url;
    /* Signing covers the request itself, so the method and the path with its
     * query have to survive alongside the trimmed url used for the lookup. */
    char *method;
    char *path_and_query;
    BOOL utf16;
    /* filled in by DoWork */
    char *token;
    char *signature;
};

static void token_request_free( struct token_request *req )
{
    free( req->relying_party );
    free( req->url );
    free( req->method );
    free( req->path_and_query );
    free( req->token );
    free( req->signature );
    free( req );
}

/*
 * XSTS tokens are issued to a relying party, not to a URL. Everything after the
 * host is request detail the token does not depend on, and folding it in would
 * defeat the service's cache and mint a token per request.
 */
static char *relying_party_from_url( const char *url )
{
    const char *host, *end;
    char *party;
    SIZE_T len;

    if (!url) return NULL;
    if (!(host = strstr( url, "://" ))) return NULL;
    host += 3;
    if (!(end = strchr( host, '/' ))) end = host + strlen( host );

    len = end - url;
    if (!(party = malloc( len + 2 ))) return NULL;
    memcpy( party, url, len );
    /* The trailing slash is part of the relying party string Xbox Live expects. */
    party[len] = '/';
    party[len + 1] = 0;
    return party;
}

/* Escape a value for the XML the service parses.
 *
 * A path carries the query with it, and a query is full of '&'. Dropped in
 * raw that starts an XML entity, and the service rejects the whole request as
 * ill-formed -- which cost every token for a URL that had a query, while ones
 * without a query kept working and hid it. */
static char *xml_escape( const char *value )
{
    static const char *entities[] = { "&amp;", "&lt;", "&gt;", "&quot;", "&apos;" };
    static const char specials[] = "&<>\"'";
    const char *p;
    char *out, *dst;
    SIZE_T len = 1;

    if (!value) return NULL;
    for (p = value; *p; p++)
    {
        const char *special = strchr( specials, *p );
        len += special && *p ? strlen( entities[special - specials] ) : 1;
    }
    if (!(out = malloc( len ))) return NULL;

    for (p = value, dst = out; *p; p++)
    {
        const char *special = strchr( specials, *p );

        if (special && *p)
        {
            const char *entity = entities[special - specials];

            memcpy( dst, entity, strlen( entity ) );
            dst += strlen( entity );
        }
        else *dst++ = *p;
    }
    *dst = 0;
    return out;
}

/* Everything from the path onwards, which is what the signature covers. */
static char *path_and_query_from_url( const char *url )
{
    const char *host, *path;

    if (!url) return NULL;
    if (!(host = strstr( url, "://" ))) return strdup( url );
    host += 3;
    if (!(path = strchr( host, '/' ))) return strdup( "/" );
    return strdup( path );
}

static char *utf16_to_utf8( const WCHAR *str )
{
    char *out;
    int len;

    if (!str) return NULL;
    len = WideCharToMultiByte( CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL );
    if (len <= 0 || !(out = malloc( len ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, str, -1, out, len, NULL, NULL );
    return out;
}

/* Layout for the caller-owned buffer: the struct, then the strings it points at. */
static SIZE_T token_result_size( const struct token_request *req )
{
    SIZE_T token_len = req->token ? strlen( req->token ) + 1 : 1;
    SIZE_T sig_len = req->signature ? strlen( req->signature ) + 1 : 1;

    if (req->utf16)
        return sizeof(XUserGetTokenAndSignatureUtf16Data) + (token_len + sig_len) * sizeof(WCHAR);
    return sizeof(XUserGetTokenAndSignatureData) + token_len + sig_len;
}

static HRESULT token_write_result( const struct token_request *req, void *buffer, SIZE_T size )
{
    const char *token = req->token ? req->token : "";
    const char *signature = req->signature ? req->signature : "";

    if (size < token_result_size( req )) return E_NOT_SUFFICIENT_BUFFER;

    if (req->utf16)
    {
        XUserGetTokenAndSignatureUtf16Data *data = buffer;
        WCHAR *strings = (WCHAR *)(data + 1);
        int token_len, sig_len;

        token_len = MultiByteToWideChar( CP_UTF8, 0, token, -1, strings, strlen( token ) + 1 );
        data->token = strings;
        data->tokenCount = token_len ? token_len - 1 : 0;
        strings += token_len;

        sig_len = MultiByteToWideChar( CP_UTF8, 0, signature, -1, strings, strlen( signature ) + 1 );
        data->signature = strings;
        data->signatureCount = sig_len ? sig_len - 1 : 0;
    }
    else
    {
        XUserGetTokenAndSignatureData *data = buffer;
        char *strings = (char *)(data + 1);

        data->tokenSize = strlen( token );
        data->token = strings;
        memcpy( strings, token, data->tokenSize + 1 );
        strings += data->tokenSize + 1;

        data->signatureSize = strlen( signature );
        data->signature = strings;
        memcpy( strings, signature, data->signatureSize + 1 );
    }
    return S_OK;
}

static HRESULT token_fetch( struct token_request *req, BOOL force_refresh )
{
    static const char format[] = "<XstsTokenRequest><RelyingParty>%s</RelyingParty>"
                                 "<ForceRefresh>%s</ForceRefresh>"
                                 "<AppId>%s</AppId><Url>%s</Url>"
                                 "<Method>%s</Method>"
                                 "<PathAndQuery>%s</PathAndQuery></XstsTokenRequest>";
    const char *force = force_refresh ? "true" : "false";
    char *request, *reply = NULL, *app_id, *path;
    HRESULT hr;
    int len;

    /* The title's own MSA app id. Without it the service can only mint a token
     * that says who the user is; services that key off the title -- PlayFab,
     * the in-game marketplace -- refuse those. */
    if (!(app_id = xodus_game_config_value( "MSAAppId" )))
        WARN( "no MSAAppId in MicrosoftGame.Config; the token will have no title claim.\n" );

    path = xml_escape( req->path_and_query ? req->path_and_query : "/" );

    len = _scprintf( format, req->relying_party, force, app_id ? app_id : "", req->url,
                     req->method ? req->method : "GET", path ? path : "/" );
    if (len < 0 || !(request = malloc( len + 1 )))
    {
        free( app_id );
        free( path );
        return E_OUTOFMEMORY;
    }
    sprintf( request, format, req->relying_party, force, app_id ? app_id : "", req->url,
             req->method ? req->method : "GET", path ? path : "/" );
    free( app_id );
    free( path );

    hr = xodus_service_call( XODUS_MSG_XSTS_TOKEN, request, &reply );
    free( request );
    if (FAILED(hr)) return hr;

    req->token = xodus_xml_element( reply, "Token" );
    req->signature = xodus_xml_element( reply, "Signature" );
    free( reply );

    if (!req->token)
    {
        WARN( "no token in the service reply for %s.\n", debugstr_a( req->relying_party ) );
        return E_GAMEUSER_FAILED_TO_GET_TOKEN;
    }
    return S_OK;
}

static HRESULT CALLBACK token_async_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    struct token_request *req = data->context;

    switch (op)
    {
    case XAsyncOp_Begin:
        /* Off the calling thread: this is an HTTPS round trip to Xbox Live. */
        return IXThreadingImpl_XAsyncSchedule( x_threading_impl, data->async, 0 );

    case XAsyncOp_DoWork:
    {
        HRESULT hr = token_fetch( req, FALSE );

        IXThreadingImpl_XAsyncComplete( x_threading_impl, data->async, hr,
                                        SUCCEEDED(hr) ? token_result_size( req ) : 0 );
        return E_PENDING;  /* completed above */
    }

    case XAsyncOp_GetResult:
        return token_write_result( req, data->buffer, data->bufferSize );

    case XAsyncOp_Cleanup:
        token_request_free( req );
        return S_OK;

    default:
        return S_OK;
    }
}

static HRESULT token_begin( XUserHandle user, XUserGetTokenAndSignatureOptions options,
                            const char *method, const char *url, SIZE_T body_size,
                            BOOL utf16, XAsyncBlock *async )
{
    struct token_request *req;
    HRESULT hr;

    if (!user_from_handle( user )) return E_INVALIDARG;
    if (!(req = calloc( 1, sizeof(*req) ))) return E_OUTOFMEMORY;

    req->utf16 = utf16;
    req->method = strdup( method && *method ? method : "GET" );
    req->path_and_query = path_and_query_from_url( url );

    /* The signature covers the body too. Nothing seen so far sends one -- every
     * observed call is a GET with bodySize 0 -- so rather than carry bytes that
     * would have to be escaped for nothing, say so if one ever turns up. */
    if (body_size)
        FIXME( "request body of %Iu bytes is not covered by the signature.\n", body_size );

    if (!(req->relying_party = relying_party_from_url( url )))
    {
        token_request_free( req );
        return E_INVALIDARG;
    }
    if ((req->url = strdup( url )))
    {
        /* Trim the query: the endpoint table matches on host and path, and a
         * query string would have to be XML-escaped for nothing. */
        char *query = strchr( req->url, '?' );

        if (query) *query = 0;
    }
    else
    {
        token_request_free( req );
        return E_OUTOFMEMORY;
    }

    TRACE( "relying party %s.\n", debugstr_a( req->relying_party ) );

    hr = IXThreadingImpl_XAsyncBegin( x_threading_impl, async, req, token_begin, "XUserGetTokenAndSignature",
                                      token_async_provider );
    if (FAILED(hr)) token_request_free( req );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureAsync( IXUserImpl6 *iface, XUserHandle user, XUserGetTokenAndSignatureOptions options, const char *method, const char *url, SIZE_T headerCount, const XUserGetTokenAndSignatureHttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async )
{
    TRACE( "iface %p, user %p, options %d, method %s, url %s, headerCount %Iu, bodySize %Iu, async %p.\n",
           iface, user, options, debugstr_a( method ), debugstr_a( url ), headerCount, bodySize, async );

    return token_begin( user, options, method, url, bodySize, FALSE, async );
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    TRACE( "iface %p, async %p, bufferSize %p.\n", iface, async, bufferSize );

    if (!bufferSize) return E_POINTER;
    return IXThreadingImpl_XAsyncGetResultSize( x_threading_impl, async, bufferSize );
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureData **ptrToBuffer, SIZE_T *bufferUsed )
{
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %Iu, buffer %p, ptrToBuffer %p, bufferUsed %p.\n",
           iface, async, bufferSize, buffer, ptrToBuffer, bufferUsed );

    if (!buffer) return E_POINTER;
    hr = IXThreadingImpl_XAsyncGetResult( x_threading_impl, async, token_begin, bufferSize, buffer, bufferUsed );
    /* The data is laid out at the head of the caller's own buffer. */
    if (SUCCEEDED(hr) && ptrToBuffer) *ptrToBuffer = buffer;
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16Async( IXUserImpl6 *iface, XUserHandle user, XUserGetTokenAndSignatureOptions options, const WCHAR *method, const WCHAR *url, SIZE_T headerCount, const XUserGetTokenAndSignatureUtf16HttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async )
{
    char *url_utf8, *method_utf8;
    HRESULT hr;

    TRACE( "iface %p, user %p, options %d, method %s, url %s, headerCount %Iu, bodySize %Iu, async %p.\n",
           iface, user, options, debugstr_w( method ), debugstr_w( url ), headerCount, bodySize, async );

    if (!(url_utf8 = utf16_to_utf8( url ))) return E_INVALIDARG;
    method_utf8 = utf16_to_utf8( method );
    hr = token_begin( user, options, method_utf8, url_utf8, bodySize, TRUE, async );
    free( method_utf8 );
    free( url_utf8 );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16ResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    TRACE( "iface %p, async %p, bufferSize %p.\n", iface, async, bufferSize );

    if (!bufferSize) return E_POINTER;
    return IXThreadingImpl_XAsyncGetResultSize( x_threading_impl, async, bufferSize );
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16Result( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureUtf16Data **ptrToBuffer, SIZE_T *bufferUsed )
{
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %Iu, buffer %p, ptrToBuffer %p, bufferUsed %p.\n",
           iface, async, bufferSize, buffer, ptrToBuffer, bufferUsed );

    if (!buffer) return E_POINTER;
    hr = IXThreadingImpl_XAsyncGetResult( x_threading_impl, async, token_begin, bufferSize, buffer, bufferUsed );
    if (SUCCEEDED(hr) && ptrToBuffer) *ptrToBuffer = buffer;
    return hr;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiAsync( IXUserImpl6 *iface, XUserHandle user, const char *url, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, url %s, async %p stub!\n", iface, user, debugstr_a( url ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiUtf16Async( IXUserImpl6 *iface, XUserHandle user, const WCHAR *url, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, url %s, async %p stub!\n", iface, user, debugstr_w( url ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiUtf16Result( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static LONG64 user_change_event_token;

static HRESULT WINAPI x_user_XUserRegisterForChangeEvent( IXUserImpl6 *iface, XTaskQueueHandle queue, void *context, XUserChangeEventCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p: accepted, no user events will be raised.\n",
           iface, queue, context, callback, token );

    if (!callback || !token) return E_INVALIDARG;

    /* There is no user store yet, so no change will ever be raised. Handing
     * back a valid token anyway is deliberate: titles register this before
     * sign-in and treat a failure here as fatal, while an accepted-but-quiet
     * registration simply means "no user ever changed". */
    token->token = InterlockedIncrement64( &user_change_event_token );
    return S_OK;
}

static BOOLEAN WINAPI x_user_XUserUnregisterForChangeEvent( IXUserImpl6 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    TRACE( "iface %p, token %I64d, wait %d.\n", iface, token.token, wait );

    /* Nothing was ever queued against the registration, so there is nothing to
     * wait for and removal always succeeds. */
    return TRUE;
}

static HRESULT WINAPI x_user_XUserGetSignOutDeferral( IXUserImpl6 *iface, XUserSignOutDeferralHandle *deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );
    *deferral = NULL;
    return E_GAMEUSER_DEFERRAL_NOT_AVAILABLE;
}

static void WINAPI x_user_XUserCloseSignOutDeferralHandle( IXUserImpl6 *iface, XUserSignOutDeferralHandle deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );
}

static HRESULT WINAPI x_user_XUserAddByIdWithUiAsync( IXUserImpl6 *iface, UINT64 userId, XAsyncBlock *async )
{
    FIXME( "iface %p, userId %llu, async %p stub!\n", iface, userId, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserAddByIdWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async, XUserHandle *newUser )
{
    FIXME( "iface %p, async %p, newUser %p stub!\n", iface, async, newUser );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyAsync( IXUserImpl6 *iface, XUserHandle user, XUserGetMsaTokenSilentlyOptions options, const char *scope, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, options %u, scope %s, async %p stub!\n", iface, user, options, debugstr_a( scope ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T resultTokenSize, char *resultToken, SIZE_T *resultTokenUsed )
{
    FIXME( "iface %p, async %p, resultTokenSize %Iu, resultToken %p, resultTokenUsed %p stub!\n", iface, async, resultTokenSize, resultToken, resultTokenUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *tokenSize )
{
    FIXME( "iface %p, async %p, tokenSize %p stub!\n", iface, async, tokenSize );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_XUserIsStoreUser( IXUserImpl6 *iface, XUserHandle user )
{
    FIXME( "iface %p, user %p stub!\n", iface, user );
    return TRUE;
}

static HRESULT WINAPI x_user_XUserPlatformRemoteConnectSetEventHandlers( IXUserImpl6 *iface, XTaskQueueHandle queue, XUserPlatformRemoteConnectEventHandlers *handlers )
{
    FIXME( "iface %p, queue %p, handlers %p stub!\n", iface, queue, handlers );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformRemoteConnectCancelPrompt( IXUserImpl6 *iface, XUserPlatformOperation operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformSpopPromptSetEventHandlers( IXUserImpl6 *iface, XTaskQueueHandle queue, XUserPlatformSpopPromptEventHandler *handler, void *context )
{
    FIXME( "iface %p, queue %p, handler %p, context %p stub!\n", iface, queue, handler, context );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformSpopPromptComplete( IXUserImpl6 *iface, XUserPlatformOperation operation, XUserPlatformOperationResult result )
{
    FIXME( "iface %p, operation %p, result %d stub!\n", iface, operation, result );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_XUserIsSignOutPresent( IXUserImpl6 *iface )
{
    TRACE( "iface %p.\n", iface );
    return FALSE;
}

static HRESULT WINAPI x_user_XUserSignOutAsync( IXUserImpl6 *iface, XUserHandle user, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, async %p stub!\n", iface, user, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserSignOutResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static const struct IXUserImpl6Vtbl x_user_vtbl =
{
    x_user_QueryInterface,
    x_user_AddRef,
    x_user_Release,
    /* IXUserImpl methods */
    x_user_XUserDuplicateHandle,
    x_user_XUserCloseHandle,
    x_user_XUserCompare,
    x_user_XUserGetMaxUsers,
    x_user_XUserAddAsync,
    x_user_XUserAddResult,
    x_user_XUserGetLocalId,
    x_user_XUserFindUserByLocalId,
    x_user_XUserGetId,
    x_user_XUserFindUserById,
    x_user_XUserGetIsGuest,
    x_user_XUserGetState,
    __PADDING__,
    x_user_XUserGetGamerPictureAsync,
    x_user_XUserGetGamerPictureResultSize,
    x_user_XUserGetGamerPictureResult,
    x_user_XUserGetAgeGroup,
    x_user_XUserCheckPrivilege,
    x_user_XUserResolvePrivilegeWithUiAsync,
    x_user_XUserResolvePrivilegeWithUiResult,
    x_user_XUserGetTokenAndSignatureAsync,
    x_user_XUserGetTokenAndSignatureResultSize,
    x_user_XUserGetTokenAndSignatureResult,
    x_user_XUserGetTokenAndSignatureUtf16Async,
    x_user_XUserGetTokenAndSignatureUtf16ResultSize,
    x_user_XUserGetTokenAndSignatureUtf16Result,
    x_user_XUserResolveIssueWithUiAsync,
    x_user_XUserResolveIssueWithUiResult,
    x_user_XUserResolveIssueWithUiUtf16Async,
    x_user_XUserResolveIssueWithUiUtf16Result,
    x_user_XUserRegisterForChangeEvent,
    x_user_XUserUnregisterForChangeEvent,
    x_user_XUserGetSignOutDeferral,
    x_user_XUserCloseSignOutDeferralHandle,
    /* IXUserImpl2 methods */
    x_user_XUserAddByIdWithUiAsync,
    x_user_XUserAddByIdWithUiResult,
    /* IXUserImpl3 methods */
    x_user_XUserGetMsaTokenSilentlyAsync,
    x_user_XUserGetMsaTokenSilentlyResult,
    x_user_XUserGetMsaTokenSilentlyResultSize,
    /* IXUserImpl4 methods */
    x_user_XUserIsStoreUser,
    /* IXUserImpl5 methods */
    x_user_XUserPlatformRemoteConnectSetEventHandlers,
    x_user_XUserPlatformRemoteConnectCancelPrompt,
    x_user_XUserPlatformSpopPromptSetEventHandlers,
    x_user_XUserPlatformSpopPromptComplete,
    /* IXUserImpl6 methods */
    x_user_XUserIsSignOutPresent,
    x_user_XUserSignOutAsync,
    x_user_XUserSignOutResult,
};

static inline struct x_user *impl_from_IXUserGamertagImpl( IXUserGamertagImpl *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserGamertagImpl_iface );
}

static HRESULT WINAPI x_user_gamertag_QueryInterface( IXUserGamertagImpl *iface, REFIID riid, void **out )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_QueryInterface( &impl->IXUserImpl6_iface, riid, out );
}

static ULONG WINAPI x_user_gamertag_AddRef( IXUserGamertagImpl *iface )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_AddRef( &impl->IXUserImpl6_iface );
}

static ULONG WINAPI x_user_gamertag_Release( IXUserGamertagImpl *iface )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_Release( &impl->IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_gamertag_XUserGetGamertag( IXUserGamertagImpl *iface, XUserHandle user_handle, XUserGamertagComponent gamertagComponent, SIZE_T gamertagSize, char *gamertag, SIZE_T *gamertagUsed )
{
    struct user_object *user = user_from_handle( user_handle );
    SIZE_T needed;

    if (!user || !gamertag) return E_INVALIDARG;

    /* Say which name is actually being handed over. This used to report a
     * placeholder unconditionally, which was true before identities were real
     * and has been misleading ever since: a signed-in account gets its own
     * gamertag here, and the log claimed otherwise. */
    if (user->xuid)
        TRACE( "iface %p, user %p, component %d: gamertag %s.\n",
               iface, user_handle, gamertagComponent, debugstr_a( user->gamertag ) );
    else
        FIXME( "iface %p, user %p, component %d: no signed-in account, returning %s.\n",
               iface, user_handle, gamertagComponent, debugstr_a( user->gamertag ) );

    /* Every component (classic, modern, suffix) resolves to the same name until
     * a real account is wired up. */
    needed = strlen( user->gamertag ) + 1;
    if (gamertagSize < needed) return E_NOT_SUFFICIENT_BUFFER;

    memcpy( gamertag, user->gamertag, needed );
    if (gamertagUsed) *gamertagUsed = needed;
    return S_OK;
}

static const struct IXUserGamertagImplVtbl x_user_gamertag_vtbl =
{
    x_user_gamertag_QueryInterface,
    x_user_gamertag_AddRef,
    x_user_gamertag_Release,
    /* IXUserGamertag methods */
    x_user_gamertag_XUserGetGamertag,
};

static inline struct x_user *impl_from_IXUserDeviceImpl2( IXUserDeviceImpl2 *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserDeviceImpl2_iface );
}

static HRESULT WINAPI x_user_device_QueryInterface( IXUserDeviceImpl2 *iface, REFIID iid, void **out )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown          ) ||
        IsEqualGUID( iid, &IID_IXUserDeviceImpl  ) ||
        IsEqualGUID( iid, &IID_IXUserDeviceImpl2 ))
    {
        IXUserDeviceImpl2_AddRef( *out = &impl->IXUserDeviceImpl2_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_user_device_AddRef( IXUserDeviceImpl2 *iface )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );
    return IXUserImpl6_AddRef( &impl->IXUserImpl6_iface );
}

static ULONG WINAPI x_user_device_Release( IXUserDeviceImpl2 *iface )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );
    return IXUserImpl6_Release( &impl->IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_device_XUserFindForDevice( IXUserDeviceImpl2 *iface, const APP_LOCAL_DEVICE_ID *deviceId, XUserHandle *handle )
{
    FIXME( "iface %p, deviceId %p, handle %p stub!\n", iface, deviceId, handle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserRegisterForDeviceAssociationChanged( IXUserDeviceImpl2 *iface, XTaskQueueHandle queue, void *context, XUserDeviceAssociationChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_device_XUserUnregisterForDeviceAssociationChanged( IXUserDeviceImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

static HRESULT WINAPI x_user_device_XUserGetDefaultAudioEndpointUtf16( IXUserDeviceImpl2 *iface, XUserLocalId user, XUserDefaultAudioEndpointKind defaultAudioEndpointKind, SIZE_T endpointIdUtf16Count, WCHAR *endpointIdUtf16, SIZE_T *endpointIdUtf16Used )
{
    FIXME( "iface %p, user %p, defaultAudioEndpointKind %d, endpointIdUtf16Count %Iu, endpointIdUtf16 %p, endpointIdUtf16Used %p stub!\n", iface, &user, defaultAudioEndpointKind, endpointIdUtf16Count, endpointIdUtf16, endpointIdUtf16Used );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserRegisterForDefaultAudioEndpointUtf16Changed( IXUserDeviceImpl2 *iface, XTaskQueueHandle queue, void *context, XUserDefaultAudioEndpointUtf16ChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_device_XUserUnregisterForDefaultAudioEndpointUtf16Changed( IXUserDeviceImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

static HRESULT WINAPI x_user_device_XUserFindControllerForUserWithUiAsync( IXUserDeviceImpl2 *iface, XUserHandle user, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, async %p stub!\n", iface, user, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserFindControllerForUserWithUiResult( IXUserDeviceImpl2 *iface, XAsyncBlock *async, APP_LOCAL_DEVICE_ID *deviceId )
{
    FIXME( "iface %p, async %p, deviceId %p stub!\n", iface, async, deviceId );
    return E_NOTIMPL;
}

static const struct IXUserDeviceImpl2Vtbl x_user_device_vtbl =
{
    x_user_device_QueryInterface,
    x_user_device_AddRef,
    x_user_device_Release,
    /* IXUserDeviceImpl/IXUserDeviceImpl2 methods */
    x_user_device_XUserFindForDevice,
    x_user_device_XUserRegisterForDeviceAssociationChanged,
    x_user_device_XUserUnregisterForDeviceAssociationChanged,
    x_user_device_XUserGetDefaultAudioEndpointUtf16,
    x_user_device_XUserRegisterForDefaultAudioEndpointUtf16Changed,
    x_user_device_XUserUnregisterForDefaultAudioEndpointUtf16Changed,
    x_user_device_XUserFindControllerForUserWithUiAsync,
    x_user_device_XUserFindControllerForUserWithUiResult,
};

static struct x_user x_user =
{
    {&x_user_vtbl},
    {&x_user_gamertag_vtbl},
    {&x_user_device_vtbl},
    0,
};

IXUserImpl *x_user_impl = (IXUserImpl *)&x_user.IXUserImpl6_iface;
IXUserDeviceImpl *x_user_device_impl = (IXUserDeviceImpl *)&x_user.IXUserDeviceImpl2_iface;
