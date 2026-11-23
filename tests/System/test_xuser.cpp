/*
 * XUser Tests
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

#include "../test_fixtures.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <windows.h>
#include <xgameruntimeinit.h>
#include <inspectable.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xuser.h>

#include "../../src/System/XUserHandle.h"
#include "tests/Xodus/xodus_test_support.h"

// Public wrappers exported by libxgameruntime (libs/libxgameruntime/Impl.cpp); no installed header declares them.
HRESULT __stdcall XUserDuplicateHandle( XUserHandle handle, XUserHandle *duplicatedHandle );
void    __stdcall XUserCloseHandle( XUserHandle user );
INT32   __stdcall XUserCompare( XUserHandle user1, XUserHandle user2 );
HRESULT __stdcall XUserGetMaxUsers( UINT32 *maxUsers );
HRESULT __stdcall XUserGetLocalId( XUserHandle user, XUserLocalId *userLocalId );
HRESULT __stdcall XUserGetIsGuest( XUserHandle user, BOOLEAN *isGuest );
HRESULT __stdcall XUserGetState( XUserHandle user, XUserState *state );
HRESULT __stdcall XUserGetAgeGroup( XUserHandle user, XUserAgeGroup *ageGroup );
HRESULT __stdcall XUserCheckPrivilege( XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, BOOLEAN *hasPrivilege, XUserPrivilegeDenyReason *reason );
BOOLEAN __stdcall XUserIsStoreUser( XUserHandle user );
BOOLEAN __stdcall XUserIsSignOutPresent();
HRESULT __stdcall XUserAddAsync( XUserAddOptions options, XAsyncBlock *async );
HRESULT __stdcall XUserAddResult( XAsyncBlock *async, XUserHandle *newUser );
HRESULT __stdcall XUserFindUserByLocalId( XUserLocalId userLocalId, XUserHandle *handle );
HRESULT __stdcall XUserFindUserById( UINT64 userId, XUserHandle *handle );
HRESULT __stdcall XUserRegisterForChangeEvent( XTaskQueueHandle queue, void *context, XUserChangeEventCallback *callback, XTaskQueueRegistrationToken *token );
BOOLEAN __stdcall XUserUnregisterForChangeEvent( XTaskQueueRegistrationToken token, BOOLEAN wait );
HRESULT __stdcall XUserGetSignOutDeferral( XUserSignOutDeferralHandle *deferral );
void    __stdcall XUserCloseSignOutDeferralHandle( XUserSignOutDeferralHandle deferral );
HRESULT __stdcall XUserGetId( XUserHandle user, UINT64 *userId );
HRESULT __stdcall XUserGetGamertag( XUserHandle user, XUserGamertagComponent component, SIZE_T size, char *gamertag, SIZE_T *used );
HRESULT __stdcall XUserGetGamerPictureAsync( XUserHandle user, XUserGamerPictureSize size, XAsyncBlock *async );
HRESULT __stdcall XUserGetGamerPictureResultSize( XAsyncBlock *async, SIZE_T *bufferSize );
HRESULT __stdcall XUserGetGamerPictureResult( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed );
HRESULT __stdcall XUserGetTokenAndSignatureAsync( XUserHandle user, XUserGetTokenAndSignatureOptions options, const char *method, const char *url, SIZE_T headerCount, const XUserGetTokenAndSignatureHttpHeader headers[], SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async );
HRESULT __stdcall XUserGetTokenAndSignatureResultSize( XAsyncBlock *async, SIZE_T *bufferSize );
HRESULT __stdcall XUserGetTokenAndSignatureResult( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureData **ptrToBuffer, SIZE_T *bufferUsed );
HRESULT __stdcall XUserGetTokenAndSignatureUtf16Async( XUserHandle user, XUserGetTokenAndSignatureOptions options, const WCHAR *method, const WCHAR *url, SIZE_T headerCount, const XUserGetTokenAndSignatureUtf16HttpHeader headers[], SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async );
HRESULT __stdcall XUserGetTokenAndSignatureUtf16ResultSize( XAsyncBlock *async, SIZE_T *bufferSize );
HRESULT __stdcall XUserGetTokenAndSignatureUtf16Result( XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureUtf16Data **ptrToBuffer, SIZE_T *bufferUsed );
HRESULT __stdcall XUserGetMsaTokenSilentlyAsync( XUserHandle user, XUserGetMsaTokenSilentlyOptions options, const char *scope, XAsyncBlock *async );
HRESULT __stdcall XUserGetMsaTokenSilentlyResult( XAsyncBlock *async, SIZE_T resultTokenSize, char *resultToken, SIZE_T *resultTokenUsed );
HRESULT __stdcall XUserGetMsaTokenSilentlyResultSize( XAsyncBlock *async, SIZE_T *tokenSize );
HRESULT __stdcall XUserResolvePrivilegeWithUiAsync( XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, XAsyncBlock *async );
HRESULT __stdcall XUserResolvePrivilegeWithUiResult( XAsyncBlock *async );
HRESULT __stdcall XUserResolveIssueWithUiAsync( XUserHandle user, const char *url, XAsyncBlock *async );
HRESULT __stdcall XUserResolveIssueWithUiResult( XAsyncBlock *async );
HRESULT __stdcall XUserResolveIssueWithUiUtf16Async( XUserHandle user, const WCHAR *url, XAsyncBlock *async );
HRESULT __stdcall XUserResolveIssueWithUiUtf16Result( XAsyncBlock *async );
HRESULT __stdcall XUserAddByIdWithUiAsync( UINT64 userId, XAsyncBlock *async );
HRESULT __stdcall XUserAddByIdWithUiResult( XAsyncBlock *async, XUserHandle *newUser );
HRESULT __stdcall XUserSignOutAsync( XUserHandle user, XAsyncBlock *async );
HRESULT __stdcall XUserSignOutResult( XAsyncBlock *async );
HRESULT __stdcall XUserPlatformRemoteConnectSetEventHandlers( XTaskQueueHandle queue, XUserPlatformRemoteConnectEventHandlers *handlers );
HRESULT __stdcall XUserPlatformRemoteConnectCancelPrompt( XUserPlatformOperation operation );
HRESULT __stdcall XUserPlatformSpopPromptSetEventHandlers( XTaskQueueHandle queue, XUserPlatformSpopPromptEventHandler *handler, void *context );
HRESULT __stdcall XUserPlatformSpopPromptComplete( XUserPlatformOperation operation, XUserPlatformOperationResult result );

namespace
{

typedef HRESULT (WINAPI *QueryApiImplFn)( REFCLSID clsid, REFIID iid, void **out );

QueryApiImplFn GetQueryApiImpl()
{
    EXPECT_EQ( S_OK, XGameRuntimeInitialize() );
    HMODULE module = LoadLibraryA( "xgameruntime.dll" );
    EXPECT_NE( nullptr, module );
    return reinterpret_cast<QueryApiImplFn>( GetProcAddress( module, "QueryApiImpl" ) );
}

// IIDs the game asks for, written out literally so a wrong header value is caught.
const GUID IID_Game_IXUserImpl5 = { 0x26f3c674, 0xa2fe, 0x44fa, { 0xb6, 0xc4, 0xa3, 0x23, 0xbc, 0x94, 0xff, 0x53 } };
const GUID IID_Game_IXUserGamertagImpl = { 0xcef4fac0, 0x7676, 0x4a94, { 0xa1, 0x19, 0x4c, 0x43, 0xf9, 0xeb, 0x5b, 0x74 } };

// Test-owned handle: refs 2, so the DLL never frees it. The test releases both references.
XUserHandle MakeUser( UINT64 localId )
{
    return new XUser{ XUSER_SIGNATURE, 2, { localId } };
}

XUserHandle MakeIdentityUser( UINT64 localId, UINT64 xuid, XUserAgeGroup age )
{
    XUser *u = new XUser{ XUSER_SIGNATURE, 2, { localId }, xuid, age };
    u->gamertag = "Classic";
    u->modernGamertag = "Modern";
    u->modernGamertagSuffix = "1234";
    u->uniqueModernGamertag = "Modern#1234";
    return u;
}

void FreeUser( XUserHandle h )
{
    UserRelease( h );
    UserRelease( h );
}

IUnknown *GetIdentity( IUnknown *p )
{
    IUnknown *identity = nullptr;
    EXPECT_EQ( S_OK, p->QueryInterface( __uuidof( IUnknown ), reinterpret_cast<void **>( &identity ) ) );
    if (identity) identity->Release();
    return identity;
}

void ExpectQiOk( REFCLSID clsid, REFIID iid )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );
    void *out = nullptr;
    EXPECT_EQ( S_OK, query( clsid, iid, &out ) );
    EXPECT_NE( nullptr, out );
    if (out) static_cast<IUnknown *>( out )->Release();
}

} // namespace

TEST( XUserObject, AcceptedIids )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    const IID *iids[] = {
        &__uuidof( IUnknown ), &__uuidof( IInspectable ), &__uuidof( IAgileObject ),
        &__uuidof( IXUserImpl ), &__uuidof( IXUserImpl2 ), &__uuidof( IXUserImpl3 ),
        &__uuidof( IXUserImpl4 ), &__uuidof( IXUserImpl5 ), &__uuidof( IXUserImpl6 ),
        &__uuidof( IXUserGamertagImpl ),
    };
    for (const IID *iid : iids)
    {
        ExpectQiOk( CLSID_XUserImpl, *iid );
    }
}

TEST( XUserObject, GameIidIXUserImpl5 )
{
    ExpectQiOk( CLSID_XUserImpl, IID_Game_IXUserImpl5 );
}

TEST( XUserObject, GameIidIXUserGamertagImpl )
{
    ExpectQiOk( CLSID_XUserImpl, IID_Game_IXUserGamertagImpl );
}

TEST( XUserObject, GamertagPointerIdentity )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    IUnknown *main_ptr = nullptr, *gamertag = nullptr;
    ASSERT_EQ( S_OK, query( CLSID_XUserImpl, __uuidof( IXUserImpl6 ), reinterpret_cast<void **>( &main_ptr ) ) );
    ASSERT_EQ( S_OK, query( CLSID_XUserImpl, __uuidof( IXUserGamertagImpl ), reinterpret_cast<void **>( &gamertag ) ) );

    EXPECT_EQ( GetIdentity( main_ptr ), GetIdentity( gamertag ) );
    EXPECT_NE( static_cast<void *>( gamertag ), static_cast<void *>( GetIdentity( gamertag ) ) );

    gamertag->Release();
    main_ptr->Release();
}

TEST( XUserObject, UnknownIidAndNullOut )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    void *out = reinterpret_cast<void *>( 0x1234 );
    EXPECT_EQ( E_NOINTERFACE, query( CLSID_XUserImpl, GUID_NULL, &out ) );
    EXPECT_EQ( nullptr, out );

    EXPECT_EQ( E_POINTER, query( CLSID_XUserImpl, __uuidof( IXUserImpl6 ), nullptr ) );
}

TEST( XUserObject, RefCounting )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    IUnknown *main_ptr = nullptr, *gamertag = nullptr;
    ASSERT_EQ( S_OK, query( CLSID_XUserImpl, __uuidof( IXUserImpl6 ), reinterpret_cast<void **>( &main_ptr ) ) );

    ULONG base = main_ptr->AddRef();
    EXPECT_EQ( base + 1, main_ptr->AddRef() );
    EXPECT_EQ( base, main_ptr->Release() );

    // A QI on the main pointer for the gamertag interface adds one reference to the shared count,
    // and releasing through the gamertag pointer removes it from the same count.
    ASSERT_EQ( S_OK, main_ptr->QueryInterface( __uuidof( IXUserGamertagImpl ), reinterpret_cast<void **>( &gamertag ) ) );
    EXPECT_EQ( base + 2, main_ptr->AddRef() );
    EXPECT_EQ( base + 1, main_ptr->Release() );
    EXPECT_EQ( base, gamertag->Release() );

    main_ptr->Release();
    main_ptr->Release();
}

TEST( XUserObject, UnregisteredClsid )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );
    void *out = nullptr;
    EXPECT_EQ( REGDB_E_CLASSNOTREG, query( GUID_NULL, __uuidof( IUnknown ), &out ) );
}

TEST( XUserHandle, Validity )
{
    EXPECT_FALSE( UserIsValid( nullptr ) );

    XUser zeroed{};
    EXPECT_FALSE( UserIsValid( &zeroed ) );

    XUser no_refs{ XUSER_SIGNATURE, 0, { 1 } };
    EXPECT_FALSE( UserIsValid( &no_refs ) );

    XUser bad_signature{ 0x12345678, 1, { 1 } };
    EXPECT_FALSE( UserIsValid( &bad_signature ) );

    XUser good{ XUSER_SIGNATURE, 1, { 1 } };
    EXPECT_TRUE( UserIsValid( &good ) );
}

TEST( XUserHandle, AddRefReleaseReturnNewCount )
{
    XUserHandle h = new XUser{ XUSER_SIGNATURE, 1, { 1 } };
    EXPECT_EQ( 2, UserAddRef( h ) );
    EXPECT_EQ( 1, UserRelease( h ) );
    EXPECT_EQ( 0, UserRelease( h ) ); // deletes
}

TEST( XUserHandle, DuplicateAndClose )
{
    XUserHandle h = MakeUser( 7 ), dup = nullptr;

    EXPECT_EQ( S_OK, XUserDuplicateHandle( h, &dup ) );
    EXPECT_EQ( h, dup );
    EXPECT_EQ( 3, h->refs.load() );

    XUserCloseHandle( dup );
    EXPECT_EQ( 2, h->refs.load() );
    FreeUser( h );
}

TEST( XUserHandle, DuplicateInvalidArgs )
{
    XUserHandle h = MakeUser( 7 );
    XUserHandle out = reinterpret_cast<XUserHandle>( 0x1234 );

    EXPECT_EQ( E_INVALIDARG, XUserDuplicateHandle( nullptr, &out ) );
    EXPECT_EQ( nullptr, out );

    XUser zeroed{};
    out = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( E_INVALIDARG, XUserDuplicateHandle( &zeroed, &out ) );
    EXPECT_EQ( nullptr, out );
    EXPECT_EQ( 0, zeroed.refs.load() );

    EXPECT_EQ( E_INVALIDARG, XUserDuplicateHandle( h, nullptr ) );
    EXPECT_EQ( 2, h->refs.load() );
    FreeUser( h );
}

TEST( XUserHandle, CloseInvalidIsIgnored )
{
    XUserCloseHandle( nullptr );

    XUser zeroed{};
    XUserCloseHandle( &zeroed );
    EXPECT_EQ( 0u, zeroed.signature );
    EXPECT_EQ( 0, zeroed.refs.load() );
}

TEST( XUserHandle, EightThreadsDuplicateClose )
{
    XUserHandle h = MakeUser( 7 );

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++)
    {
        threads.emplace_back( [h]
        {
            for (int n = 0; n < 100000; n++)
            {
                XUserHandle dup = nullptr;
                if (XUserDuplicateHandle( h, &dup ) == S_OK) XUserCloseHandle( dup );
            }
        } );
    }
    for (auto &t : threads) t.join();

    EXPECT_EQ( 2, h->refs.load() );
    FreeUser( h );
}

TEST( XUserCompare, Ordering )
{
    XUserHandle a = MakeUser( 5 ), b = MakeUser( 5 ), one = MakeUser( 1 ), two = MakeUser( 2 );
    XUser zeroed1{}, zeroed2{};

    EXPECT_EQ( 0, XUserCompare( nullptr, nullptr ) );
    EXPECT_EQ( 0, XUserCompare( a, a ) );
    EXPECT_EQ( 0, XUserCompare( a, b ) );
    EXPECT_EQ( -1, XUserCompare( one, two ) );
    EXPECT_EQ( 1, XUserCompare( two, one ) );
    EXPECT_EQ( -1, XUserCompare( nullptr, a ) );
    EXPECT_EQ( 1, XUserCompare( a, nullptr ) );
    EXPECT_EQ( 0, XUserCompare( nullptr, &zeroed1 ) );
    EXPECT_EQ( 0, XUserCompare( &zeroed1, &zeroed2 ) );
    EXPECT_EQ( -1, XUserCompare( &zeroed1, a ) );
    EXPECT_EQ( 1, XUserCompare( a, &zeroed1 ) );

    FreeUser( a ); FreeUser( b ); FreeUser( one ); FreeUser( two );
}

TEST( XUserCompare, ZeroedStructsWithDifferingLocalIdAreEqual )
{
    XUser x{ 0, 0, { 1 } }, y{ 0, 0, { 2 } };
    EXPECT_EQ( 0, XUserCompare( &x, &y ) );
}

TEST( XUserCompare, MaxUsers )
{
    UINT32 max = 0;
    EXPECT_EQ( S_OK, XUserGetMaxUsers( &max ) );
    EXPECT_EQ( 1u, max );
    EXPECT_EQ( E_INVALIDARG, XUserGetMaxUsers( nullptr ) );
}

TEST( XUserGetters, ValidHandle )
{
    XUserHandle h = MakeUser( 42 );

    XUserLocalId id{ 0 };
    EXPECT_EQ( S_OK, XUserGetLocalId( h, &id ) );
    EXPECT_EQ( 42u, id.value );

    XUserState state = XUserState::SignedOut;
    EXPECT_EQ( S_OK, XUserGetState( h, &state ) );
    EXPECT_EQ( XUserState::SignedIn, state );

    BOOLEAN guest = TRUE;
    EXPECT_EQ( S_OK, XUserGetIsGuest( h, &guest ) );
    EXPECT_FALSE( guest );

    XUserAgeGroup age = XUserAgeGroup::Unknown;
    EXPECT_EQ( S_OK, XUserGetAgeGroup( h, &age ) );
    EXPECT_EQ( XUserAgeGroup::Unknown, age );

    BOOLEAN has = FALSE;
    XUserPrivilegeDenyReason reason = XUserPrivilegeDenyReason::Banned;
    EXPECT_EQ( S_OK, XUserCheckPrivilege( h, XUserPrivilegeOptions::None, XUserPrivilege::Multiplayer, &has, &reason ) );
    EXPECT_TRUE( has );
    EXPECT_EQ( XUserPrivilegeDenyReason::None, reason );

    has = FALSE;
    EXPECT_EQ( S_OK, XUserCheckPrivilege( h, XUserPrivilegeOptions::AllUsers, XUserPrivilege::Multiplayer, &has, nullptr ) );
    EXPECT_TRUE( has );

    EXPECT_TRUE( XUserIsStoreUser( h ) );
    FreeUser( h );
}

TEST( XUserGetters, InvalidHandleAndNullOut )
{
    XUserHandle h = MakeUser( 42 );
    XUser zeroed{};
    XUserHandle invalid[] = { nullptr, &zeroed };

    for (XUserHandle bad : invalid)
    {
        XUserLocalId id{ 99 };
        EXPECT_EQ( E_INVALIDARG, XUserGetLocalId( bad, &id ) );
        EXPECT_EQ( 99u, id.value );

        UINT64 xuid = 99;
        EXPECT_EQ( E_INVALIDARG, XUserGetId( bad, &xuid ) );
        EXPECT_EQ( 99u, xuid );

        SIZE_T used = 99;
        char text[8] = "abcdefg";
        EXPECT_EQ( E_INVALIDARG, XUserGetGamertag( bad, XUserGamertagComponent::Classic, sizeof(text), text, &used ) );
        EXPECT_EQ( 99u, used );
        EXPECT_STREQ( "abcdefg", text );

        XUserState state = XUserState::SigningOut;
        EXPECT_EQ( E_INVALIDARG, XUserGetState( bad, &state ) );
        EXPECT_EQ( XUserState::SigningOut, state );

        BOOLEAN guest = 7;
        EXPECT_EQ( E_INVALIDARG, XUserGetIsGuest( bad, &guest ) );
        EXPECT_EQ( 7, guest );

        XUserAgeGroup age = XUserAgeGroup::Teen;
        EXPECT_EQ( E_INVALIDARG, XUserGetAgeGroup( bad, &age ) );
        EXPECT_EQ( XUserAgeGroup::Teen, age );

        BOOLEAN has = 7;
        XUserPrivilegeDenyReason reason = XUserPrivilegeDenyReason::Banned;
        EXPECT_EQ( E_INVALIDARG, XUserCheckPrivilege( bad, XUserPrivilegeOptions::None, XUserPrivilege::Multiplayer, &has, &reason ) );
        EXPECT_EQ( 7, has );
        EXPECT_EQ( XUserPrivilegeDenyReason::Banned, reason );

        EXPECT_FALSE( XUserIsStoreUser( bad ) );
    }

    EXPECT_EQ( E_INVALIDARG, XUserGetLocalId( h, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserGetId( h, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserGetState( h, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserGetIsGuest( h, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserGetAgeGroup( h, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserCheckPrivilege( h, XUserPrivilegeOptions::None, XUserPrivilege::Multiplayer, nullptr, nullptr ) );
    FreeUser( h );
}

TEST( XUserGetters, IdentityValues )
{
    XUserHandle h = MakeIdentityUser( 42, 2535405123456789ull, XUserAgeGroup::Teen );

    UINT64 xuid = 0;
    EXPECT_EQ( S_OK, XUserGetId( h, &xuid ) );
    EXPECT_EQ( 2535405123456789ull, xuid );

    XUserLocalId id{ 0 };
    EXPECT_EQ( S_OK, XUserGetLocalId( h, &id ) );
    EXPECT_EQ( 42u, id.value );

    XUserAgeGroup age = XUserAgeGroup::Unknown;
    EXPECT_EQ( S_OK, XUserGetAgeGroup( h, &age ) );
    EXPECT_EQ( XUserAgeGroup::Teen, age );

    XUserState state = XUserState::SignedOut;
    EXPECT_EQ( S_OK, XUserGetState( h, &state ) );
    EXPECT_EQ( XUserState::SignedIn, state );
    FreeUser( h );
}

TEST( XUserGetters, GamertagComponents )
{
    XUserHandle h = MakeIdentityUser( 42, 1, XUserAgeGroup::Adult );
    const struct { XUserGamertagComponent c; const char *text; } cases[] = {
        { XUserGamertagComponent::Classic, "Classic" },
        { XUserGamertagComponent::Modern, "Modern" },
        { XUserGamertagComponent::ModernSuffix, "1234" },
        { XUserGamertagComponent::UniqueModern, "Modern#1234" },
    };

    for (const auto &t : cases)
    {
        SCOPED_TRACE( t.text );
        size_t need = strlen( t.text ) + 1;
        char text[32];
        SIZE_T used = 0;
        EXPECT_EQ( S_OK, XUserGetGamertag( h, t.c, sizeof(text), text, &used ) );
        EXPECT_EQ( need, used );
        EXPECT_STREQ( t.text, text );

        char small[32] = "untouched";
        used = 0;
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ), XUserGetGamertag( h, t.c, need - 1, small, &used ) );
        EXPECT_EQ( need, used );
        EXPECT_STREQ( "untouched", small );

        used = 0;
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ), XUserGetGamertag( h, t.c, 0, nullptr, &used ) );
        EXPECT_EQ( need, used );
    }

    SIZE_T used = 99;
    char text[8] = "abcdefg";
    EXPECT_EQ( E_INVALIDARG, XUserGetGamertag( h, (XUserGamertagComponent)9, sizeof(text), text, &used ) );
    EXPECT_EQ( 99u, used );
    EXPECT_STREQ( "abcdefg", text );

    used = 99;
    EXPECT_EQ( E_INVALIDARG, XUserGetGamertag( h, XUserGamertagComponent::Classic, sizeof(text), nullptr, &used ) );
    EXPECT_EQ( 99u, used );
    FreeUser( h );
}

TEST( XUserGetters, SignOutNotPresent )
{
    EXPECT_FALSE( XUserIsSignOutPresent() );
}

namespace
{

struct AddWait
{
    std::atomic<int> count{ 0 };
    HANDLE event = CreateEventA( nullptr, FALSE, FALSE, nullptr );
    ~AddWait() { CloseHandle( event ); }
};

void CALLBACK AddCompleted( XAsyncBlock *async )
{
    AddWait *w = static_cast<AddWait *>( async->context );
    w->count++;
    SetEvent( w->event );
}

HRESULT CALLBACK PendingUntilCancel( XAsyncOp op, const XAsyncProviderData *data )
{
    if (op == XAsyncOp::Cancel) XAsyncComplete( data->async, E_ABORT, 0 );
    return S_OK;
}

void CALLBACK ChangeCb( void *context, XUserLocalId, XUserChangeEvent )
{
    static_cast<std::atomic<int> *>( context )->fetch_add( 1 );
}

} // namespace

TEST( XUserAdd, AllOptionsNoDefaultUser )
{
    if (GetFileAttributesA( "Z:\\tmp\\xodus.sock" ) != INVALID_FILE_ATTRIBUTES)
        GTEST_SKIP() << "/tmp/xodus.sock exists: an xodus-service may be listening";

    const XUserAddOptions options[] = { XUserAddOptions::None, XUserAddOptions::AddDefaultUserSilently, XUserAddOptions::AddDefaultUserAllowingUI };
    for (XUserAddOptions opt : options)
    {
        AddWait w;
        XAsyncBlock async = {};
        async.context = &w;
        async.callback = AddCompleted;

        ASSERT_EQ( S_OK, XUserAddAsync( opt, &async ) );
        ASSERT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
        EXPECT_EQ( (HRESULT)0x89245106, XAsyncGetStatus( &async, TRUE ) );
        Sleep( 50 );
        EXPECT_EQ( 1, w.count.load() );

        XUserHandle user = reinterpret_cast<XUserHandle>( 0x1234 );
        EXPECT_EQ( (HRESULT)0x89245106, XUserAddResult( &async, &user ) );
        EXPECT_EQ( nullptr, user );
    }
}

TEST( XUserAdd, NullArguments )
{
    XAsyncBlock async = {};
    XUserHandle user = nullptr;
    EXPECT_EQ( E_INVALIDARG, XUserAddAsync( XUserAddOptions::None, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserAddResult( nullptr, &user ) );
    EXPECT_EQ( E_INVALIDARG, XUserAddResult( &async, nullptr ) );
}

TEST( XUserAdd, PendingBlockNotCompleted )
{
    AddWait w;
    XAsyncBlock async = {};
    async.context = &w;
    async.callback = AddCompleted;

    ASSERT_EQ( S_OK, XAsyncBegin( &async, nullptr, reinterpret_cast<const void *>( PendingUntilCancel ), "Pending", PendingUntilCancel ) );
    EXPECT_NE( S_OK, XUserAddAsync( XUserAddOptions::None, &async ) );
    Sleep( 100 );
    EXPECT_EQ( 0, w.count.load() );

    XAsyncCancel( &async );
    EXPECT_EQ( E_ABORT, XAsyncGetStatus( &async, TRUE ) );
    EXPECT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
}

TEST( XUserChangeEvents, RegisterUnregister )
{
    std::atomic<int> calls{ 0 };
    XTaskQueueRegistrationToken t1 = {}, t2 = {};
    ASSERT_EQ( S_OK, XUserRegisterForChangeEvent( nullptr, &calls, ChangeCb, &t1 ) );
    ASSERT_EQ( S_OK, XUserRegisterForChangeEvent( nullptr, &calls, ChangeCb, &t2 ) );
    EXPECT_NE( 0u, t1.token );
    EXPECT_NE( 0u, t2.token );
    EXPECT_NE( t1.token, t2.token );

    // A completed add must not fire the callback.
    AddWait w;
    XAsyncBlock async = {};
    async.context = &w;
    async.callback = AddCompleted;
    ASSERT_EQ( S_OK, XUserAddAsync( XUserAddOptions::None, &async ) );
    ASSERT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
    Sleep( 100 );
    EXPECT_EQ( 0, calls.load() );
    XUserHandle user = nullptr;
    XUserAddResult( &async, &user );

    EXPECT_TRUE( XUserUnregisterForChangeEvent( t1, TRUE ) );
    EXPECT_FALSE( XUserUnregisterForChangeEvent( t1, TRUE ) );
    EXPECT_TRUE( XUserUnregisterForChangeEvent( t2, FALSE ) );
    EXPECT_FALSE( XUserUnregisterForChangeEvent( XTaskQueueRegistrationToken{ 0 }, TRUE ) );
    EXPECT_FALSE( XUserUnregisterForChangeEvent( XTaskQueueRegistrationToken{ 0xdeadbeef }, TRUE ) );
}

TEST( XUserChangeEvents, NullArguments )
{
    XTaskQueueRegistrationToken t = {};
    EXPECT_EQ( E_INVALIDARG, XUserRegisterForChangeEvent( nullptr, nullptr, nullptr, &t ) );
    EXPECT_EQ( E_INVALIDARG, XUserRegisterForChangeEvent( nullptr, nullptr, ChangeCb, nullptr ) );
}

TEST( XUserFind, NotFound )
{
    XUserHandle h = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindUserByLocalId( XUserLocalId{ 1 }, &h ) );
    EXPECT_EQ( nullptr, h );
    h = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindUserById( 1, &h ) );
    EXPECT_EQ( nullptr, h );
    EXPECT_EQ( E_INVALIDARG, XUserFindUserByLocalId( XUserLocalId{ 1 }, nullptr ) );
    EXPECT_EQ( E_INVALIDARG, XUserFindUserById( 1, nullptr ) );
}

// The exported API runs in xgameruntime.dll, whose user object is separate from this exe's copy of
// XUserIdentity.cpp, so the user is installed through XUserAddAsync against a test xodus server.
TEST( XUserFind, InstalledUserMatchAndMiss )
{
    if (GetFileAttributesA( "Z:\\tmp\\xodus.sock" ) != INVALID_FILE_ATTRIBUTES)
        GTEST_SKIP() << "/tmp/xodus.sock exists: an xodus-service may be listening";

    XodusTest::TestServer server( XTS_MODE_SERVE, "/tmp/xodus.sock",
        "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>4242</Xuid><Gamertag>Find</Gamertag></UserInfoResponse>" );

    AddWait w;
    XAsyncBlock async = {};
    async.context = &w;
    async.callback = AddCompleted;
    ASSERT_EQ( S_OK, XUserAddAsync( XUserAddOptions::AddDefaultUserSilently, &async ) );
    ASSERT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, XUserAddResult( &async, &u ) );
    ASSERT_NE( nullptr, u );
    UINT64 xuid = 0;
    ASSERT_EQ( S_OK, XUserGetId( u, &xuid ) );
    ASSERT_EQ( 4242u, xuid );
    XUserLocalId localId{ 0 };
    ASSERT_EQ( S_OK, XUserGetLocalId( u, &localId ) );
    LONG before = u->refs.load();

    XUserHandle h = nullptr;
    EXPECT_EQ( S_OK, XUserFindUserById( 4242, &h ) );
    EXPECT_EQ( u, h );
    EXPECT_EQ( before + 1, u->refs.load() );
    XUserCloseHandle( h );

    h = nullptr;
    EXPECT_EQ( S_OK, XUserFindUserByLocalId( localId, &h ) );
    EXPECT_EQ( u, h );
    EXPECT_EQ( before + 1, u->refs.load() );
    XUserCloseHandle( h );

    h = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindUserById( 4243, &h ) );
    EXPECT_EQ( nullptr, h );
    h = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindUserByLocalId( XUserLocalId{ localId.value + 1 }, &h ) );
    EXPECT_EQ( nullptr, h );

    EXPECT_EQ( before, u->refs.load() );
    XUserCloseHandle( u );
}

TEST( XUserSignOutDeferral, NotAvailable )
{
    XUserSignOutDeferralHandle d = reinterpret_cast<XUserSignOutDeferralHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245103, XUserGetSignOutDeferral( &d ) );
    EXPECT_EQ( nullptr, d );
    EXPECT_EQ( E_INVALIDARG, XUserGetSignOutDeferral( nullptr ) );
    XUserCloseSignOutDeferralHandle( nullptr );
}

// One entry per Async stub: calls it with a zeroed block, then the same block must still start a real XUserAddAsync.
TEST( XUserStubs, AsyncStubsDoNotStartOperation )
{
    XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
    typedef std::function<HRESULT( XAsyncBlock * )> Call;
    const std::vector<Call> calls = {
        [u]( XAsyncBlock *a ) { return XUserGetGamerPictureAsync( u, XUserGamerPictureSize::Small, a ); },
        [u]( XAsyncBlock *a ) { return XUserGetTokenAndSignatureAsync( u, XUserGetTokenAndSignatureOptions::None, "GET", "http://x", 0, nullptr, 0, nullptr, a ); },
        [u]( XAsyncBlock *a ) { return XUserGetTokenAndSignatureUtf16Async( u, XUserGetTokenAndSignatureOptions::None, L"GET", L"http://x", 0, nullptr, 0, nullptr, a ); },
        [u]( XAsyncBlock *a ) { return XUserGetMsaTokenSilentlyAsync( u, XUserGetMsaTokenSilentlyOptions::None, "scope", a ); },
        [u]( XAsyncBlock *a ) { return XUserResolvePrivilegeWithUiAsync( u, XUserPrivilegeOptions::None, XUserPrivilege::Multiplayer, a ); },
        [u]( XAsyncBlock *a ) { return XUserResolveIssueWithUiAsync( u, "http://x", a ); },
        [u]( XAsyncBlock *a ) { return XUserResolveIssueWithUiUtf16Async( u, L"http://x", a ); },
        [u]( XAsyncBlock *a ) { return XUserAddByIdWithUiAsync( 1, a ); },
        [u]( XAsyncBlock *a ) { return XUserSignOutAsync( u, a ); },
    };

    for (size_t i = 0; i < calls.size(); i++)
    {
        SCOPED_TRACE( i );
        AddWait w;
        XAsyncBlock async = {};
        async.context = &w;
        async.callback = AddCompleted;
        ASSERT_EQ( E_NOTIMPL, calls[i]( &async ) );
        ASSERT_EQ( S_OK, XUserAddAsync( XUserAddOptions::None, &async ) );
        ASSERT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
        EXPECT_EQ( 1, w.count.load() );
        XUserHandle user = nullptr;
        XUserAddResult( &async, &user );
    }
}

TEST( XUserStubs, ResultAndOutStubs )
{
    XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
    XAsyncBlock block = {};
    const SIZE_T sentinel = 0xdeadbeef;

    SIZE_T used = sentinel;
    char text[8] = "abcdefg";

    SIZE_T size = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetGamerPictureResultSize( &block, &size ) );
    EXPECT_EQ( 0u, size );
    used = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetGamerPictureResult( &block, sizeof(text), text, &used ) );
    EXPECT_EQ( 0u, used );

    size = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetTokenAndSignatureResultSize( &block, &size ) );
    EXPECT_EQ( 0u, size );
    size = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetTokenAndSignatureUtf16ResultSize( &block, &size ) );
    EXPECT_EQ( 0u, size );

    XUserGetTokenAndSignatureData *ptr = reinterpret_cast<XUserGetTokenAndSignatureData *>( sentinel );
    used = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetTokenAndSignatureResult( &block, sizeof(text), text, &ptr, &used ) );
    EXPECT_EQ( nullptr, ptr );
    EXPECT_EQ( 0u, used );
    XUserGetTokenAndSignatureUtf16Data *ptr16 = reinterpret_cast<XUserGetTokenAndSignatureUtf16Data *>( sentinel );
    used = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetTokenAndSignatureUtf16Result( &block, sizeof(text), text, &ptr16, &used ) );
    EXPECT_EQ( nullptr, ptr16 );
    EXPECT_EQ( 0u, used );

    size = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetMsaTokenSilentlyResultSize( &block, &size ) );
    EXPECT_EQ( 0u, size );
    used = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserGetMsaTokenSilentlyResult( &block, sizeof(text), text, &used ) );
    EXPECT_EQ( 0u, used );
    EXPECT_STREQ( "abcdefg", text );

    XUserHandle added = reinterpret_cast<XUserHandle>( sentinel );
    EXPECT_EQ( E_NOTIMPL, XUserAddByIdWithUiResult( &block, &added ) );
    EXPECT_EQ( nullptr, added );

    EXPECT_EQ( E_NOTIMPL, XUserResolvePrivilegeWithUiResult( &block ) );
    EXPECT_EQ( E_NOTIMPL, XUserResolveIssueWithUiResult( &block ) );
    EXPECT_EQ( E_NOTIMPL, XUserResolveIssueWithUiUtf16Result( &block ) );
    EXPECT_EQ( E_NOTIMPL, XUserSignOutResult( &block ) );
    EXPECT_EQ( E_NOTIMPL, XUserPlatformRemoteConnectSetEventHandlers( nullptr, nullptr ) );
    EXPECT_EQ( E_NOTIMPL, XUserPlatformRemoteConnectCancelPrompt( nullptr ) );
    EXPECT_EQ( E_NOTIMPL, XUserPlatformSpopPromptSetEventHandlers( nullptr, nullptr, nullptr ) );
    EXPECT_EQ( E_NOTIMPL, XUserPlatformSpopPromptComplete( nullptr, XUserPlatformOperationResult::Success ) );
}

// --- XUserDeviceImpl --- //

HRESULT __stdcall XUserFindForDevice( const APP_LOCAL_DEVICE_ID *deviceId, XUserHandle *handle );
HRESULT __stdcall XUserRegisterForDeviceAssociationChanged( XTaskQueueHandle queue, void *context, XUserDeviceAssociationChangedCallback *callback, XTaskQueueRegistrationToken *token );
BOOLEAN __stdcall XUserUnregisterForDeviceAssociationChanged( XTaskQueueRegistrationToken token, BOOLEAN wait );
HRESULT __stdcall XUserGetDefaultAudioEndpointUtf16( XUserLocalId user, XUserDefaultAudioEndpointKind kind, SIZE_T count, WCHAR endpointId[], SIZE_T *used );
HRESULT __stdcall XUserRegisterForDefaultAudioEndpointUtf16Changed( XTaskQueueHandle queue, void *context, XUserDefaultAudioEndpointUtf16ChangedCallback *callback, XTaskQueueRegistrationToken *token );
BOOLEAN __stdcall XUserUnregisterForDefaultAudioEndpointUtf16Changed( XTaskQueueRegistrationToken token, BOOLEAN wait );
HRESULT __stdcall XUserFindControllerForUserWithUiAsync( XUserHandle user, XAsyncBlock *async );
HRESULT __stdcall XUserFindControllerForUserWithUiResult( XAsyncBlock *async, APP_LOCAL_DEVICE_ID *deviceId );

namespace
{
// Written out literally so a wrong header value is caught.
const GUID IID_Game_IXUserDeviceImpl2 = { 0x0cc6a956, 0xe7e1, 0x4fdf, { 0x93, 0x41, 0x9d, 0x5d, 0xa6, 0x49, 0xeb, 0xc8 } };
const GUID IID_Game_IXUserDeviceImpl = { 0x7d824997, 0x10dc, 0x45ab, { 0x86, 0xb7, 0x27, 0x37, 0x76, 0x7c, 0x0b, 0xf1 } };
}

TEST( XUserDeviceObject, AcceptedIids )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    const IID *iids[] = {
        &__uuidof( IUnknown ), &__uuidof( IInspectable ), &__uuidof( IAgileObject ),
        &__uuidof( IXUserDeviceImpl ), &__uuidof( IXUserDeviceImpl2 ),
    };
    for (const IID *iid : iids)
    {
        ExpectQiOk( CLSID_XUserDeviceImpl, *iid );
    }
}

TEST( XUserDeviceObject, GameIid0cc6a956 )
{
    ExpectQiOk( CLSID_XUserDeviceImpl, IID_Game_IXUserDeviceImpl2 );
    ExpectQiOk( CLSID_XUserDeviceImpl, IID_Game_IXUserDeviceImpl );
}

TEST( XUserDeviceObject, UnknownIidNullOutAndRefCounting )
{
    QueryApiImplFn query = GetQueryApiImpl();
    ASSERT_NE( nullptr, query );

    void *out = reinterpret_cast<void *>( 0x1234 );
    EXPECT_EQ( E_NOINTERFACE, query( CLSID_XUserDeviceImpl, GUID_NULL, &out ) );
    EXPECT_EQ( nullptr, out );
    EXPECT_EQ( E_POINTER, query( CLSID_XUserDeviceImpl, __uuidof( IXUserDeviceImpl2 ), nullptr ) );

    IUnknown *p = nullptr;
    ASSERT_EQ( S_OK, query( CLSID_XUserDeviceImpl, __uuidof( IXUserDeviceImpl2 ), reinterpret_cast<void **>( &p ) ) );
    ULONG base = p->AddRef();
    EXPECT_EQ( base + 1, p->AddRef() );
    EXPECT_EQ( base, p->Release() );
    EXPECT_EQ( base - 1, p->Release() );
    p->Release();
}

TEST( XUserDeviceStubs, AllMethods )
{
    ASSERT_EQ( S_OK, XGameRuntimeInitialize() );
    const SIZE_T sentinel = 0xdeadbeef;

    APP_LOCAL_DEVICE_ID dev = {};
    XUserHandle h = reinterpret_cast<XUserHandle>( sentinel );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindForDevice( &dev, &h ) );
    EXPECT_EQ( nullptr, h );
    EXPECT_EQ( E_INVALIDARG, XUserFindForDevice( &dev, nullptr ) );
    h = reinterpret_cast<XUserHandle>( sentinel );
    EXPECT_EQ( E_INVALIDARG, XUserFindForDevice( nullptr, &h ) );

    XTaskQueueRegistrationToken token = { sentinel };
    EXPECT_EQ( E_NOTIMPL, XUserRegisterForDeviceAssociationChanged( nullptr, nullptr, nullptr, &token ) );
    EXPECT_EQ( 0u, token.token );
    EXPECT_EQ( E_NOTIMPL, XUserRegisterForDeviceAssociationChanged( nullptr, nullptr, nullptr, nullptr ) );
    EXPECT_FALSE( XUserUnregisterForDeviceAssociationChanged( token, TRUE ) );

    SIZE_T used = sentinel;
    WCHAR buf[4] = L"abc";
    EXPECT_EQ( E_NOTIMPL, XUserGetDefaultAudioEndpointUtf16( { 1 }, XUserDefaultAudioEndpointKind::CommunicationRender, 4, buf, &used ) );
    EXPECT_EQ( 0u, used );
    EXPECT_EQ( 0, wcscmp( L"abc", buf ) );
    EXPECT_EQ( E_NOTIMPL, XUserGetDefaultAudioEndpointUtf16( { 1 }, XUserDefaultAudioEndpointKind::CommunicationRender, 4, buf, nullptr ) );

    token.token = sentinel;
    EXPECT_EQ( E_NOTIMPL, XUserRegisterForDefaultAudioEndpointUtf16Changed( nullptr, nullptr, nullptr, &token ) );
    EXPECT_EQ( 0u, token.token );
    EXPECT_EQ( E_NOTIMPL, XUserRegisterForDefaultAudioEndpointUtf16Changed( nullptr, nullptr, nullptr, nullptr ) );
    EXPECT_FALSE( XUserUnregisterForDefaultAudioEndpointUtf16Changed( token, FALSE ) );

    // The async stub must not start an operation: the same block then runs a real XUserAddAsync.
    AddWait w;
    XAsyncBlock async = {};
    async.context = &w;
    async.callback = AddCompleted;
    EXPECT_EQ( E_NOTIMPL, XUserFindControllerForUserWithUiAsync( nullptr, &async ) );
    ASSERT_EQ( S_OK, XUserAddAsync( XUserAddOptions::None, &async ) );
    ASSERT_EQ( WAIT_OBJECT_0, WaitForSingleObject( w.event, 5000 ) );
    EXPECT_EQ( 1, w.count.load() );
    XUserHandle added = nullptr;
    XUserAddResult( &async, &added );

    APP_LOCAL_DEVICE_ID out;
    memset( &out, 0xcd, sizeof( out ) );
    EXPECT_EQ( E_NOTIMPL, XUserFindControllerForUserWithUiResult( &async, &out ) );
    for (BYTE b : out.value) EXPECT_EQ( 0, b );
    EXPECT_EQ( E_NOTIMPL, XUserFindControllerForUserWithUiResult( &async, nullptr ) );
}

// ---- Signed-in path through the DLL, against the test xodus server ----
//
// The DLL keeps one process-wide user object that survives across tests. Tests here never assume it is
// absent or has particular identity before they add: each serves its own reply and adds first. The
// user-less tests earlier in this file (XUserFind.NotFound and others) must stay ahead of these.

namespace
{

constexpr HRESULT kNoDefaultUser = (HRESULT)0x89245106;

std::string UserInfoReply( const std::string &xuid = "4242", const std::string &gamertag = "Classic",
                           const std::string &modern = "Modern", const std::string &suffix = "1234",
                           const std::string &unique = "Modern#1234", const std::string &age = "Adult" )
{
    return "<UserInfoResponse><SignedIn>true</SignedIn><Xuid>" + xuid + "</Xuid><Gamertag>" + gamertag +
           "</Gamertag><ModernGamertag>" + modern + "</ModernGamertag><ModernGamertagSuffix>" + suffix +
           "</ModernGamertagSuffix><UniqueModernGamertag>" + unique + "</UniqueModernGamertag><AgeGroup>" + age +
           "</AgeGroup></UserInfoResponse>";
}

class XUserSignedIn : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (GetFileAttributesA( "Z:\\tmp\\xodus.sock" ) != INVALID_FILE_ATTRIBUTES)
            GTEST_SKIP() << "/tmp/xodus.sock exists: an xodus-service may be listening";
    }

    void TearDown() override
    {
        server.reset();
        XGameRuntimeOptions options{ XGameRuntimeGameConfigSource::Default, nullptr };
        XGameRuntimeInitializeWithOptions( &options );
        XGameRuntimeUninitialize();
    }

    // Starts the test server. The process-wide client may still hold a connection to the previous test's
    // server; its first request then fails once and reconnects (see StaleConnectionFailsOnceThenReconnects),
    // so a throwaway add absorbs that unless the test starts from a known-closed client (flush false).
    void Serve( const std::string &reply, bool flush = true )
    {
        server.reset();
        server = std::make_unique<XodusTest::TestServer>( XTS_MODE_SERVE, "/tmp/xodus.sock", reply );
        if (!flush) return;
        XUserHandle h = nullptr;
        if (Add( XUserAddOptions::None, &h ) == S_OK) XUserCloseHandle( h );
    }

    static void UseConfig( XGameRuntimeGameConfigSource source, const char *config )
    {
        XGameRuntimeOptions options{ source, config };
        ASSERT_EQ( S_OK, XGameRuntimeInitializeWithOptions( &options ) );
    }

    // Runs one XUserAddAsync to completion; returns the XUserAddResult HRESULT and the handle (null on failure).
    static HRESULT Add( XUserAddOptions opt, XUserHandle *user )
    {
        AddWait w;
        XAsyncBlock async = {};
        async.context = &w;
        async.callback = AddCompleted;
        HRESULT hr = XUserAddAsync( opt, &async );
        if (hr != S_OK) return hr;
        if (WaitForSingleObject( w.event, 5000 ) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32( WAIT_TIMEOUT );
        *user = reinterpret_cast<XUserHandle>( 0x1234 );
        return XUserAddResult( &async, user );
    }

    static std::string Tag( XUserHandle u, XUserGamertagComponent c )
    {
        char buf[64] = {};
        SIZE_T used = 0;
        EXPECT_EQ( S_OK, XUserGetGamertag( u, c, sizeof( buf ), buf, &used ) );
        return buf;
    }

    std::unique_ptr<XodusTest::TestServer> server;
};

} // namespace

// (1) one add, every getter returns the served value.
TEST_F( XUserSignedIn, AddReturnsServedIdentity )
{
    Serve( UserInfoReply( "2535400000123456", "ClassicTag", "ModernTag", "9876", "ModernTag#9876", "Teen" ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::AddDefaultUserSilently, &u ) );
    ASSERT_NE( nullptr, u );

    UINT64 xuid = 0;
    ASSERT_EQ( S_OK, XUserGetId( u, &xuid ) );
    EXPECT_EQ( 2535400000123456ull, xuid );

    XUserLocalId local{ 0 };
    ASSERT_EQ( S_OK, XUserGetLocalId( u, &local ) );
    EXPECT_EQ( 1u, local.value );

    EXPECT_EQ( "ClassicTag", Tag( u, XUserGamertagComponent::Classic ) );
    EXPECT_EQ( "ModernTag", Tag( u, XUserGamertagComponent::Modern ) );
    EXPECT_EQ( "9876", Tag( u, XUserGamertagComponent::ModernSuffix ) );
    EXPECT_EQ( "ModernTag#9876", Tag( u, XUserGamertagComponent::UniqueModern ) );

    XUserState state = XUserState::SignedOut;
    ASSERT_EQ( S_OK, XUserGetState( u, &state ) );
    EXPECT_EQ( XUserState::SignedIn, state );

    XUserAgeGroup age = XUserAgeGroup::Unknown;
    ASSERT_EQ( S_OK, XUserGetAgeGroup( u, &age ) );
    EXPECT_EQ( XUserAgeGroup::Teen, age );

    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, GamertagBufferSizing )
{
    Serve( UserInfoReply( "4242", "ClassicTag", "ModernTag", "9876", "ModernTag#9876" ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );

    struct { XUserGamertagComponent c; const char *text; } cases[] = {
        { XUserGamertagComponent::Classic, "ClassicTag" },
        { XUserGamertagComponent::Modern, "ModernTag" },
        { XUserGamertagComponent::ModernSuffix, "9876" },
        { XUserGamertagComponent::UniqueModern, "ModernTag#9876" },
    };
    for (auto &t : cases)
    {
        SIZE_T need = strlen( t.text ) + 1;
        char buf[64];

        // Exact fit with terminator.
        memset( buf, 'x', sizeof( buf ) );
        SIZE_T used = 0;
        EXPECT_EQ( S_OK, XUserGetGamertag( u, t.c, need, buf, &used ) ) << t.text;
        EXPECT_EQ( need, used );
        EXPECT_STREQ( t.text, buf );

        // One byte too small: required size reported, buffer untouched.
        memset( buf, 'x', sizeof( buf ) );
        used = 0;
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ), XUserGetGamertag( u, t.c, need - 1, buf, &used ) ) << t.text;
        EXPECT_EQ( need, used );
        EXPECT_EQ( 'x', buf[0] );

        // Size query with no buffer.
        used = 0;
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ), XUserGetGamertag( u, t.c, 0, nullptr, &used ) ) << t.text;
        EXPECT_EQ( need, used );
    }

    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, AgeGroupMapping )
{
    Serve( UserInfoReply() );
    struct { const char *served; XUserAgeGroup expected; } cases[] = {
        { "Child", XUserAgeGroup::Child },
        { "Teen", XUserAgeGroup::Teen },
        { "Adult", XUserAgeGroup::Adult },
        { "Unknown", XUserAgeGroup::Unknown },
    };
    for (auto &t : cases)
    {
        server->SetReply( UserInfoReply( "4242", "Age", "Age", "1", "Age#1", t.served ) );
        XUserHandle u = nullptr;
        ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) ) << t.served;
        XUserAgeGroup age = static_cast<XUserAgeGroup>( 99 );
        ASSERT_EQ( S_OK, XUserGetAgeGroup( u, &age ) );
        EXPECT_EQ( t.expected, age ) << t.served;
        XUserCloseHandle( u );
    }
}

// (2) a second add returns the same object with one more reference.
TEST_F( XUserSignedIn, SecondAddReturnsSameHandleWithExtraRef )
{
    Serve( UserInfoReply() );
    XUserHandle a = nullptr, b = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &a ) );
    LONG afterFirst = a->refs.load();
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &b ) );
    EXPECT_EQ( a, b );
    EXPECT_EQ( afterFirst + 1, a->refs.load() );

    XUserCloseHandle( b );
    EXPECT_EQ( afterFirst, a->refs.load() );
    XUserCloseHandle( a );
    EXPECT_EQ( afterFirst - 1, a->refs.load() ); // the DLL's own reference remains, so the object stays valid
    EXPECT_GE( a->refs.load(), 1 );
}

// (3) every option reaches the same signed-in user.
TEST_F( XUserSignedIn, AllAddOptionsSucceed )
{
    Serve( UserInfoReply() );
    const XUserAddOptions options[] = { XUserAddOptions::None, XUserAddOptions::AddDefaultUserSilently, XUserAddOptions::AddDefaultUserAllowingUI };
    XUserHandle first = nullptr;
    for (XUserAddOptions opt : options)
    {
        XUserHandle u = nullptr;
        ASSERT_EQ( S_OK, Add( opt, &u ) ) << (UINT32)opt;
        ASSERT_NE( nullptr, u );
        if (!first) first = u;
        EXPECT_EQ( first, u );
        UINT64 xuid = 0;
        EXPECT_EQ( S_OK, XUserGetId( u, &xuid ) );
        EXPECT_EQ( 4242u, xuid );
        XUserCloseHandle( u );
    }
}

// (4) request content, as the server received it.
TEST_F( XUserSignedIn, RequestCarriesTitleIdAndForceRefreshFalse )
{
    // Default config beside the exe: TitleId 35760C07, no MSAAppId.
    Serve( UserInfoReply() );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    auto frame = server->LastFrame();
    EXPECT_EQ( 7, frame.first );
    EXPECT_EQ( "<UserInfoRequest><TitleId>896928775</TitleId><ForceRefresh>false</ForceRefresh></UserInfoRequest>", frame.second );
    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, RequestCarriesClientIdWhenConfigured )
{
    UseConfig( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>0000ABCD</TitleId><MSAAppId>  my-app  </MSAAppId></Game>" );
    Serve( UserInfoReply() );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( "<UserInfoRequest><ClientId>my-app</ClientId><TitleId>43981</TitleId><ForceRefresh>false</ForceRefresh></UserInfoRequest>",
               server->LastFrame().second );
    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, RequestOmitsTitleIdWhenOnlyClientIdReadable )
{
    UseConfig( XGameRuntimeGameConfigSource::Inline, "<Game><MSAAppId>only-app</MSAAppId></Game>" );
    Serve( UserInfoReply() );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( "<UserInfoRequest><ClientId>only-app</ClientId><ForceRefresh>false</ForceRefresh></UserInfoRequest>",
               server->LastFrame().second );
    XUserCloseHandle( u );
}

// (8) an unreadable or malformed config still adds; the request just leaves both identifiers out.
TEST_F( XUserSignedIn, MissingConfigFileOmitsBothIdentifiers )
{
    UseConfig( XGameRuntimeGameConfigSource::File, "C:\\xuser-no-such-dir\\MicrosoftGame.config" );
    Serve( UserInfoReply() );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( "<UserInfoRequest><ForceRefresh>false</ForceRefresh></UserInfoRequest>", server->LastFrame().second );
    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, MalformedConfigOmitsBothIdentifiers )
{
    UseConfig( XGameRuntimeGameConfigSource::Inline, "<Game><TitleId>nothex</TitleId><MSAAppId>   </MSAAppId></Game>" );
    Serve( UserInfoReply() );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( "<UserInfoRequest><ForceRefresh>false</ForceRefresh></UserInfoRequest>", server->LastFrame().second );
    XUserCloseHandle( u );
}

// (5) service replies.
TEST_F( XUserSignedIn, NotSignedInReplyIsNoDefaultUser )
{
    Serve( "<UserInfoResponse><SignedIn>false</SignedIn></UserInfoResponse>" );
    XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( nullptr, u );
}

TEST_F( XUserSignedIn, ServiceErrorHresultPassesThrough )
{
    Serve( UserInfoReply() );
    const struct { const char *code; HRESULT expected; } cases[] = {
        { "0x89245103", (HRESULT)0x89245103 },
        { "0x80070005", (HRESULT)0x80070005 },
        { "0xC0000001", (HRESULT)0xC0000001 },
    };
    for (auto &t : cases)
    {
        server->SetReply( std::string( "<UserInfoResponse><Error>" ) + t.code + "</Error><Message>boom</Message></UserInfoResponse>" );
        XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
        EXPECT_EQ( t.expected, Add( XUserAddOptions::None, &u ) ) << t.code;
        EXPECT_EQ( nullptr, u );
    }
}

TEST_F( XUserSignedIn, ServiceErrorWithNonFailureCodeIsEFail )
{
    Serve( UserInfoReply() );
    // Well-formed 0x%08X codes that are not failures cannot be passed through.
    for (const char *code : { "0x00000000", "0x00000001", "0x7FFFFFFF" })
    {
        server->SetReply( std::string( "<UserInfoResponse><Error>" ) + code + "</Error></UserInfoResponse>" );
        XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
        EXPECT_EQ( E_FAIL, Add( XUserAddOptions::None, &u ) ) << code;
        EXPECT_EQ( nullptr, u );
    }
}

TEST_F( XUserSignedIn, UnparsableErrorReplyIsNoDefaultUser )
{
    // An <Error> that is outside the wire grammar makes the reply unusable, which callers see as no default user.
    Serve( "<UserInfoResponse><Error>oops</Error></UserInfoResponse>" );
    XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( nullptr, u );
}

TEST_F( XUserSignedIn, InvalidXuidIsInvalidData )
{
    Serve( UserInfoReply() );
    for (const char *xuid : { "", "0", "abc", "-5", "12 34", "18446744073709551616" })
    {
        server->SetReply( UserInfoReply( xuid ) );
        XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
        EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INVALID_DATA ), Add( XUserAddOptions::None, &u ) ) << "xuid '" << xuid << "'";
        EXPECT_EQ( nullptr, u );
    }
}

TEST_F( XUserSignedIn, LargestXuidIsAccepted )
{
    Serve( UserInfoReply( "18446744073709551615" ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    UINT64 xuid = 0;
    EXPECT_EQ( S_OK, XUserGetId( u, &xuid ) );
    EXPECT_EQ( UINT64_MAX, xuid );
    XUserCloseHandle( u );
}

TEST_F( XUserSignedIn, RejectedReplyDoesNotChangeExistingUser )
{
    Serve( UserInfoReply( "4242", "Keep" ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );

    server->SetReply( UserInfoReply( "0", "Replaced" ) );
    XUserHandle bad = nullptr;
    EXPECT_EQ( HRESULT_FROM_WIN32( ERROR_INVALID_DATA ), Add( XUserAddOptions::None, &bad ) );

    EXPECT_EQ( "Keep", Tag( u, XUserGamertagComponent::Classic ) );
    UINT64 xuid = 0;
    EXPECT_EQ( S_OK, XUserGetId( u, &xuid ) );
    EXPECT_EQ( 4242u, xuid );
    XUserCloseHandle( u );
}

// (6) a failed attempt with no server is not remembered.
TEST_F( XUserSignedIn, AddRecoversAfterServerAppears )
{
    // The first failure may only absorb a dead connection left by the previous test; the second is a real
    // connect failure, which must not leave the client believing it is connected.
    XUserHandle u = reinterpret_cast<XUserHandle>( 0x1234 );
    ASSERT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    u = reinterpret_cast<XUserHandle>( 0x1234 );
    ASSERT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( nullptr, u );

    Serve( UserInfoReply( "777", "Recovered" ), false );
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    ASSERT_NE( nullptr, u );
    UINT64 xuid = 0;
    EXPECT_EQ( S_OK, XUserGetId( u, &xuid ) );
    EXPECT_EQ( 777u, xuid );
    XUserCloseHandle( u );

    // And it fails again once the server is gone.
    server.reset();
    u = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( nullptr, u );
}

// A server restart leaves the client holding a dead connection: that one request fails, the next reconnects.
TEST_F( XUserSignedIn, StaleConnectionFailsOnceThenReconnects )
{
    Serve( UserInfoReply( "4242", "Old" ) );
    XUserHandle u = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    XUserCloseHandle( u );

    Serve( UserInfoReply( "4242", "New" ), false );
    u = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( kNoDefaultUser, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( nullptr, u );

    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &u ) );
    EXPECT_EQ( "New", Tag( u, XUserGamertagComponent::Classic ) );
    XUserCloseHandle( u );
}

// (7) a later add refreshes the identity seen through earlier handles.
TEST_F( XUserSignedIn, LaterAddRefreshesEarlierHandle )
{
    Serve( UserInfoReply( "4242", "Before", "Before", "1", "Before#1", "Child" ) );
    XUserHandle first = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &first ) );
    EXPECT_EQ( "Before", Tag( first, XUserGamertagComponent::Classic ) );

    server->SetReply( UserInfoReply( "4343", "After", "After", "2", "After#2", "Adult" ) );
    XUserHandle second = nullptr;
    ASSERT_EQ( S_OK, Add( XUserAddOptions::None, &second ) );
    EXPECT_EQ( first, second );

    EXPECT_EQ( "After", Tag( first, XUserGamertagComponent::Classic ) );
    EXPECT_EQ( "After", Tag( first, XUserGamertagComponent::Modern ) );
    EXPECT_EQ( "2", Tag( first, XUserGamertagComponent::ModernSuffix ) );
    EXPECT_EQ( "After#2", Tag( first, XUserGamertagComponent::UniqueModern ) );
    UINT64 xuid = 0;
    EXPECT_EQ( S_OK, XUserGetId( first, &xuid ) );
    EXPECT_EQ( 4343u, xuid );
    XUserAgeGroup age = XUserAgeGroup::Unknown;
    EXPECT_EQ( S_OK, XUserGetAgeGroup( first, &age ) );
    EXPECT_EQ( XUserAgeGroup::Adult, age );

    // Finds use the refreshed xuid.
    XUserHandle found = nullptr;
    EXPECT_EQ( S_OK, XUserFindUserById( 4343, &found ) );
    EXPECT_EQ( first, found );
    XUserCloseHandle( found );
    found = reinterpret_cast<XUserHandle>( 0x1234 );
    EXPECT_EQ( (HRESULT)0x89245104, XUserFindUserById( 4242, &found ) );
    EXPECT_EQ( nullptr, found );

    XUserCloseHandle( second );
    XUserCloseHandle( first );
}
