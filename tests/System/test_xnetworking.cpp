/*
 * XNetworking Tests
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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>
#include <inspectable.h>
#include <initguid.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xgameruntimeinit.h>
#include <xtaskqueue.h>
#include <xnetworking.h>

namespace
{

const DWORD kWaitMs = 10000;

/* The unit under test is xgameruntime.dll, reached through the same QueryApiImpl export
 * a title uses. */
typedef HRESULT (WINAPI *QueryApiImplFn)( REFCLSID clsid, REFIID iid, void **out );

QueryApiImplFn GetQueryApiImpl()
{
    static QueryApiImplFn fn = []() -> QueryApiImplFn
    {
        if ( FAILED( XGameRuntimeInitialize() ) ) return nullptr;
        HMODULE dll = GetModuleHandleA( "xgameruntime.dll" );
        return dll ? reinterpret_cast<QueryApiImplFn>( GetProcAddress( dll, "QueryApiImpl" ) ) : nullptr;
    }();
    return fn;
}

/* Calls QueryApiImpl for the XNetworking class and returns the interface asked for. */
template <typename T>
T *GetNetworking( REFIID iid )
{
    void *out = nullptr;
    QueryApiImplFn query = GetQueryApiImpl();
    if ( !query || FAILED( query( CLSID_XNetworkingImpl, iid, &out ) ) ) return nullptr;
    return static_cast<T *>( out );
}

/* One asynchronous call: its block, a manual-dispatch queue and a completion counter. */
struct AsyncCall
{
    XAsyncBlock block{};
    XTaskQueueHandle queue = nullptr;
    HANDLE done = nullptr;
    std::atomic<int> completions{ 0 };

    AsyncCall()
    {
        XTaskQueueCreate( XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue );
        done = CreateEventA( nullptr, TRUE, FALSE, nullptr );
        block.queue = queue;
        block.context = this;
        block.callback = []( XAsyncBlock *b )
        {
            auto *self = static_cast<AsyncCall *>( b->context );
            self->completions++;
            SetEvent( self->done );
        };
    }

    ~AsyncCall()
    {
        XTaskQueueCloseHandle( queue );
        CloseHandle( done );
    }

    /* Dispatches both ports until the completion callback ran or the wait expires. */
    bool Drain()
    {
        DWORD start = GetTickCount();
        while ( WaitForSingleObject( done, 0 ) != WAIT_OBJECT_0 )
        {
            if ( GetTickCount() - start > kWaitMs ) return false;
            XTaskQueueDispatch( queue, XTaskQueuePort::Work, 0 );
            XTaskQueueDispatch( queue, XTaskQueuePort::Completion, 0 );
            Sleep( 1 );
        }
        return true;
    }

    /* Keeps dispatching after completion, to expose a second completion. */
    void DrainMore()
    {
        for ( int i = 0; i < 50; ++i )
        {
            XTaskQueueDispatch( queue, XTaskQueuePort::Work, 0 );
            XTaskQueueDispatch( queue, XTaskQueuePort::Completion, 0 );
            Sleep( 1 );
        }
    }
};

std::wstring Widen( const char *s )
{
    return std::wstring( s, s + strlen( s ) );
}

/* The two security-information variants run the same cases through these adapters. */
struct Utf8Variant
{
    static HRESULT Begin( IXNetworkingImpl *n, const char *url, XAsyncBlock *b )
    {
        return n->XNetworkingQuerySecurityInformationForUrlAsync( url, b );
    }
    static HRESULT Size( IXNetworkingImpl *n, XAsyncBlock *b, SIZE_T *size )
    {
        return n->XNetworkingQuerySecurityInformationForUrlAsyncResultSize( b, size );
    }
    static HRESULT Result( IXNetworkingImpl *n, XAsyncBlock *b, SIZE_T size, SIZE_T *used, UINT8 *buffer, XNetworkingSecurityInformation **out )
    {
        return n->XNetworkingQuerySecurityInformationForUrlAsyncResult( b, size, used, buffer, out );
    }
    static HRESULT BeginString( IXNetworkingImpl *n, const char *url, XAsyncBlock *b )
    {
        return Begin( n, url, b );
    }
};

struct Utf16Variant
{
    static HRESULT Begin( IXNetworkingImpl *n, const WCHAR *url, XAsyncBlock *b )
    {
        return n->XNetworkingQuerySecurityInformationForUrlUtf16Async( url, b );
    }
    static HRESULT Size( IXNetworkingImpl *n, XAsyncBlock *b, SIZE_T *size )
    {
        return n->XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize( b, size );
    }
    static HRESULT Result( IXNetworkingImpl *n, XAsyncBlock *b, SIZE_T size, SIZE_T *used, UINT8 *buffer, XNetworkingSecurityInformation **out )
    {
        return n->XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult( b, size, used, buffer, out );
    }
    static HRESULT BeginString( IXNetworkingImpl *n, const char *url, XAsyncBlock *b )
    {
        std::wstring wide = Widen( url );
        return Begin( n, wide.c_str(), b );
    }
};

const SIZE_T kInfoSize = sizeof( XNetworkingSecurityInformation );

}

class XNetworkingTests : public XGameRuntimeTests
{
protected:
    void SetUp() override
    {
        networking = GetNetworking<IXNetworkingImpl>( __uuidof( IXNetworkingImpl ) );
        ASSERT_NE( networking, nullptr );
        networking2 = GetNetworking<IXNetworkingImpl2>( __uuidof( IXNetworkingImpl2 ) );
        ASSERT_NE( networking2, nullptr );
    }

    void TearDown() override
    {
        if ( networking ) networking->Release();
        if ( networking2 ) networking2->Release();
    }

    IXNetworkingImpl *networking = nullptr;
    IXNetworkingImpl2 *networking2 = nullptr;
};

/* --- QueryApiImpl dispatch and QueryInterface --- */

TEST_F( XNetworkingTests, DispatchesEveryAcceptedInterface )
{
    const IID *iids[] = { &__uuidof( IUnknown ), &__uuidof( IInspectable ), &__uuidof( IAgileObject ),
                          &__uuidof( IXNetworkingImpl ), &__uuidof( IXNetworkingImpl2 ) };

    for ( const IID *iid : iids )
    {
        void *out = nullptr;
        ASSERT_EQ( GetQueryApiImpl()( CLSID_XNetworkingImpl, *iid, &out ), S_OK );
        ASSERT_NE( out, nullptr );
        static_cast<IUnknown *>( out )->Release();
    }
}

TEST_F( XNetworkingTests, UnknownInterfaceIsRejectedAndOutIsCleared )
{
    void *out = reinterpret_cast<void *>( 1 );
    EXPECT_EQ( networking->QueryInterface( IID_IXThreadingImpl, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
}

TEST_F( XNetworkingTests, NullOutIsEPointer )
{
    EXPECT_EQ( networking->QueryInterface( __uuidof( IXNetworkingImpl ), nullptr ), E_POINTER );
}

TEST_F( XNetworkingTests, UnknownClsidIsClassNotRegistered )
{
    void *out = nullptr;
    EXPECT_EQ( GetQueryApiImpl()( GUID_NULL, __uuidof( IUnknown ), &out ), REGDB_E_CLASSNOTREG );
    EXPECT_EQ( out, nullptr );
}

TEST_F( XNetworkingTests, AllInterfacesAreOneObject )
{
    IUnknown *u1 = nullptr, *u2 = nullptr;
    ASSERT_EQ( networking->QueryInterface( __uuidof( IUnknown ), reinterpret_cast<void **>( &u1 ) ), S_OK );
    ASSERT_EQ( networking2->QueryInterface( __uuidof( IUnknown ), reinterpret_cast<void **>( &u2 ) ), S_OK );
    EXPECT_EQ( u1, u2 );
    u1->Release();
    u2->Release();
}

TEST_F( XNetworkingTests, Impl2PointerReachesBothInterfaces )
{
    UINT64 value = 0;
    XNetworkingConnectivityHint hint{};
    EXPECT_EQ( networking2->XNetworkingQueryConfigurationSetting( XNetworkingConfigurationSetting::MaxTitleTcpQueuedReceiveBufferSize, &value ), E_NOTIMPL );
    ASSERT_EQ( networking2->XNetworkingGetConnectivityHint( &hint ), S_OK );
    EXPECT_EQ( hint.connectivityLevel, XNetworkingConnectivityLevelHint::InternetAccess );
}

TEST_F( XNetworkingTests, ReferenceCountStaysBalanced )
{
    ULONG base = networking->AddRef();
    networking->Release();

    void *out = nullptr;
    ASSERT_EQ( networking->QueryInterface( __uuidof( IXNetworkingImpl2 ), &out ), S_OK );
    ULONG during = networking->AddRef();
    networking->Release();
    EXPECT_EQ( during, base + 1 );

    static_cast<IUnknown *>( out )->Release();
    ULONG after = networking->AddRef();
    networking->Release();
    EXPECT_EQ( after, base );
}

/* --- Security information, run for both variants --- */

template <typename V>
class XNetworkingSecurityInfoTests : public XNetworkingTests
{
protected:
    /* Starts a call and runs it to completion. */
    void StartAndComplete( AsyncCall &call, const char *url = "https://example.invalid/" )
    {
        ASSERT_EQ( V::BeginString( networking, url, &call.block ), S_OK );
        ASSERT_TRUE( call.Drain() );
    }
};

typedef ::testing::Types<Utf8Variant, Utf16Variant> SecurityInfoVariants;
TYPED_TEST_SUITE( XNetworkingSecurityInfoTests, SecurityInfoVariants );

TYPED_TEST( XNetworkingSecurityInfoTests, CompletesExactlyOnceWithSuccess )
{
    AsyncCall call;
    this->StartAndComplete( call );
    call.DrainMore();

    EXPECT_EQ( call.completions.load(), 1 );
    EXPECT_EQ( XAsyncGetStatus( &call.block, FALSE ), S_OK );
}

TYPED_TEST( XNetworkingSecurityInfoTests, AnyNonNullUrlIsAccepted )
{
    const char *urls[] = { "", "not a url", "https://example.invalid/?a=b" };

    for ( const char *url : urls )
    {
        AsyncCall call;
        this->StartAndComplete( call, url );
        EXPECT_EQ( XAsyncGetStatus( &call.block, FALSE ), S_OK ) << url;
        EXPECT_EQ( call.completions.load(), 1 ) << url;
    }
}

TYPED_TEST( XNetworkingSecurityInfoTests, NullUrlAndNullBlockAreInvalid )
{
    AsyncCall call;
    EXPECT_EQ( TypeParam::Begin( this->networking, nullptr, &call.block ), E_INVALIDARG );
    call.DrainMore();
    EXPECT_EQ( call.completions.load(), 0 );

    EXPECT_EQ( TypeParam::BeginString( this->networking, "https://example.invalid/", nullptr ), E_INVALIDARG );
}

TYPED_TEST( XNetworkingSecurityInfoTests, ResultSizeIsTheStructureSize )
{
    AsyncCall call;
    this->StartAndComplete( call );

    SIZE_T size = 0;
    EXPECT_EQ( TypeParam::Size( this->networking, &call.block, &size ), S_OK );
    EXPECT_EQ( size, kInfoSize );
}

TYPED_TEST( XNetworkingSecurityInfoTests, ResultSizeRejectsNullArguments )
{
    AsyncCall call;
    this->StartAndComplete( call );

    SIZE_T size = 0;
    EXPECT_EQ( TypeParam::Size( this->networking, nullptr, &size ), E_INVALIDARG );
    EXPECT_EQ( TypeParam::Size( this->networking, &call.block, nullptr ), E_INVALIDARG );
}

TYPED_TEST( XNetworkingSecurityInfoTests, ResultHasTls12AndNoThumbprints )
{
    AsyncCall call;
    this->StartAndComplete( call );

    UINT8 buffer[sizeof( XNetworkingSecurityInformation ) + 16];
    memset( buffer, 0xAB, sizeof( buffer ) );
    XNetworkingSecurityInformation *info = nullptr;
    SIZE_T used = 0;

    ASSERT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, &used, buffer, &info ), S_OK );

    EXPECT_EQ( used, kInfoSize );
    EXPECT_EQ( info, reinterpret_cast<XNetworkingSecurityInformation *>( buffer ) );
    EXPECT_EQ( info->enabledHttpSecurityProtocolFlags, 0x800u );
    EXPECT_EQ( info->thumbprintCount, 0u );
    EXPECT_EQ( info->thumbprints, nullptr );

    /* Nothing is written past the size the caller gave. */
    for ( size_t i = kInfoSize; i < sizeof( buffer ); ++i )
        ASSERT_EQ( buffer[i], 0xAB ) << "byte " << i;
}

TYPED_TEST( XNetworkingSecurityInfoTests, ExactSizeBufferWorksWithoutUsedCount )
{
    AsyncCall call;
    this->StartAndComplete( call );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *info = nullptr;

    ASSERT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), S_OK );
    EXPECT_EQ( info, reinterpret_cast<XNetworkingSecurityInformation *>( buffer.data() ) );
}

TYPED_TEST( XNetworkingSecurityInfoTests, OneByteTooSmallFailsAndLeavesOutPointer )
{
    AsyncCall call;
    this->StartAndComplete( call );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *sentinel = reinterpret_cast<XNetworkingSecurityInformation *>( 0x1234 );
    XNetworkingSecurityInformation *info = sentinel;

    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize - 1, nullptr, buffer.data(), &info ), E_NOT_SUFFICIENT_BUFFER );
    EXPECT_EQ( info, sentinel );
}

TYPED_TEST( XNetworkingSecurityInfoTests, NullArgumentsFailBeforeTouchingTheCall )
{
    AsyncCall call;
    this->StartAndComplete( call );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *info = nullptr;

    EXPECT_EQ( TypeParam::Result( this->networking, nullptr, kInfoSize, nullptr, buffer.data(), &info ), E_INVALIDARG );
    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, nullptr, &info ), E_INVALIDARG );
    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), nullptr ), E_INVALIDARG );
    EXPECT_EQ( info, nullptr );

    /* The failed calls left the result retrievable. */
    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), S_OK );
    EXPECT_NE( info, nullptr );
}

TYPED_TEST( XNetworkingSecurityInfoTests, ResultIsRetrievableOnce )
{
    AsyncCall call;
    this->StartAndComplete( call );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *info = nullptr;
    ASSERT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), S_OK );

    info = nullptr;
    EXPECT_NE( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), S_OK );
    EXPECT_EQ( info, nullptr );
}

TYPED_TEST( XNetworkingSecurityInfoTests, ResultBeforeDispatchIsPendingThenSucceeds )
{
    AsyncCall call;
    ASSERT_EQ( TypeParam::BeginString( this->networking, "https://example.invalid/", &call.block ), S_OK );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *sentinel = reinterpret_cast<XNetworkingSecurityInformation *>( 0x1234 );
    XNetworkingSecurityInformation *info = sentinel;

    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), E_PENDING );
    EXPECT_EQ( info, sentinel );

    ASSERT_TRUE( call.Drain() );
    EXPECT_EQ( TypeParam::Result( this->networking, &call.block, kInfoSize, nullptr, buffer.data(), &info ), S_OK );
    EXPECT_EQ( info, reinterpret_cast<XNetworkingSecurityInformation *>( buffer.data() ) );
}

TYPED_TEST( XNetworkingSecurityInfoTests, CancelledCallStillCompletesExactlyOnce )
{
    AsyncCall call;
    ASSERT_EQ( TypeParam::BeginString( this->networking, "https://example.invalid/", &call.block ), S_OK );

    XAsyncCancel( &call.block );
    ASSERT_TRUE( call.Drain() );
    call.DrainMore();

    EXPECT_EQ( call.completions.load(), 1 );
    EXPECT_NE( XAsyncGetStatus( &call.block, FALSE ), E_UNEXPECTED );
}

/* A result requested through the other variant's call is rejected by XAsyncGetResult,
 * which asserts on a call/result mismatch. */
template <typename Started, typename Other>
void ReadThroughTheOtherVariant()
{
    IXNetworkingImpl *networking = GetNetworking<IXNetworkingImpl>( __uuidof( IXNetworkingImpl ) );
    AsyncCall call;
    ASSERT_EQ( Started::BeginString( networking, "https://example.invalid/", &call.block ), S_OK );
    ASSERT_TRUE( call.Drain() );

    std::vector<UINT8> buffer( kInfoSize );
    XNetworkingSecurityInformation *info = nullptr;
    Other::Result( networking, &call.block, kInfoSize, nullptr, buffer.data(), &info );
}

TEST( XNetworkingDeathTest, Utf16ResultOfAUtf8CallIsRejected )
{
    EXPECT_DEATH( ( ReadThroughTheOtherVariant<Utf8Variant, Utf16Variant>() ), "Assertion failed" );
}

TEST( XNetworkingDeathTest, Utf8ResultOfAUtf16CallIsRejected )
{
    EXPECT_DEATH( ( ReadThroughTheOtherVariant<Utf16Variant, Utf8Variant>() ), "Assertion failed" );
}

/* --- Connectivity hint --- */

TEST_F( XNetworkingTests, ConnectivityHintIsFixedInternetAccess )
{
    XNetworkingConnectivityHint hint;
    memset( &hint, 0xCC, sizeof( hint ) );

    ASSERT_EQ( networking->XNetworkingGetConnectivityHint( &hint ), S_OK );

    EXPECT_EQ( hint.connectivityLevel, XNetworkingConnectivityLevelHint::InternetAccess );
    EXPECT_EQ( hint.connectivityCost, XNetworkingConnectivityCostHint::Unrestricted );
    EXPECT_EQ( hint.ianaInterfaceType, 0u );
    EXPECT_EQ( hint.networkInitialized, TRUE );
    EXPECT_EQ( hint.approachingDataLimit, FALSE );
    EXPECT_EQ( hint.overDataLimit, FALSE );
    EXPECT_EQ( hint.roaming, FALSE );
}

TEST_F( XNetworkingTests, ConnectivityHintRejectsNull )
{
    EXPECT_EQ( networking->XNetworkingGetConnectivityHint( nullptr ), E_INVALIDARG );
}

namespace
{
std::atomic<int> g_hint_callbacks;
void __stdcall CountHintCallback( void *, const XNetworkingConnectivityHint * ) { g_hint_callbacks++; }
}

TEST_F( XNetworkingTests, RegisteredHintCallbackIsNeverInvoked )
{
    g_hint_callbacks = 0;
    XTaskQueueHandle queue = nullptr;
    ASSERT_EQ( XTaskQueueCreate( XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue ), S_OK );

    XTaskQueueRegistrationToken token{};
    ASSERT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( queue, nullptr, CountHintCallback, &token ), S_OK );
    EXPECT_EQ( g_hint_callbacks.load(), 0 );

    Sleep( 200 );
    EXPECT_EQ( g_hint_callbacks.load(), 0 );

    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( token, TRUE ), TRUE );
    XTaskQueueCloseHandle( queue );
}

TEST_F( XNetworkingTests, HintTokensAreNonZeroAndDistinct )
{
    XTaskQueueRegistrationToken a{}, b{};
    ASSERT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, CountHintCallback, &a ), S_OK );
    ASSERT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, CountHintCallback, &b ), S_OK );

    EXPECT_NE( a.token, 0u );
    EXPECT_NE( b.token, 0u );
    EXPECT_NE( a.token, b.token );

    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( a, FALSE ), TRUE );
    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( b, FALSE ), TRUE );
}

TEST_F( XNetworkingTests, UnregisterSucceedsOnceThenFails )
{
    XTaskQueueRegistrationToken token{};
    ASSERT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, CountHintCallback, &token ), S_OK );

    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( token, FALSE ), TRUE );
    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( token, FALSE ), FALSE );

    XTaskQueueRegistrationToken never{ ~0ULL };
    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( never, FALSE ), FALSE );
}

TEST_F( XNetworkingTests, HintRegistrationRejectsNullArguments )
{
    XTaskQueueRegistrationToken token{};
    EXPECT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, nullptr, &token ), E_INVALIDARG );
    EXPECT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, CountHintCallback, nullptr ), E_INVALIDARG );
}

/* --- Methods without a backing implementation --- */

TEST_F( XNetworkingTests, PortMethodsAreNotImplemented )
{
    UINT16 port = 0;
    XAsyncBlock block{};
    XTaskQueueRegistrationToken token{};

    EXPECT_EQ( networking->XNetworkingQueryPreferredLocalUdpMultiplayerPort( &port ), E_NOTIMPL );
    EXPECT_EQ( networking->XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync( &block ), E_NOTIMPL );
    EXPECT_EQ( networking->XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult( &block, &port ), E_NOTIMPL );
    EXPECT_EQ( networking->XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged( nullptr, nullptr, nullptr, &token ), E_NOTIMPL );
    EXPECT_EQ( networking->XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged( token, FALSE ), FALSE );
}

TEST_F( XNetworkingTests, Impl2MethodsAreNotImplemented )
{
    UINT64 value = 0;
    XNetworkingStatisticsBuffer stats{};

    EXPECT_EQ( networking2->XNetworkingQueryConfigurationSetting( XNetworkingConfigurationSetting::MaxSystemTcpQueuedReceiveBufferSize, &value ), E_NOTIMPL );
    EXPECT_EQ( networking2->XNetworkingSetConfigurationSetting( XNetworkingConfigurationSetting::MaxToolsTcpQueuedReceiveBufferSize, 1 ), E_NOTIMPL );
    EXPECT_EQ( networking2->XNetworkingQueryStatistics( XNetworkingStatisticsType::TitleTcpQueuedReceivedBufferUsage, &stats ), E_NOTIMPL );
}

TEST_F( XNetworkingTests, VerifyServerCertificateChecksItsArguments )
{
    /* The structure the security-information call itself returns. */
    AsyncCall call;
    ASSERT_EQ( Utf8Variant::BeginString( networking, "https://example.invalid/", &call.block ), S_OK );
    ASSERT_TRUE( call.Drain() );
    UINT8 buffer[sizeof( XNetworkingSecurityInformation )];
    XNetworkingSecurityInformation *fromCall = nullptr;
    ASSERT_EQ( Utf8Variant::Result( networking, &call.block, sizeof( buffer ), nullptr, buffer, &fromCall ), S_OK );

    XNetworkingSecurityInformation none{ 0x800, 0, nullptr };
    XNetworkingThumbprint pin{};
    XNetworkingSecurityInformation pinned{ 0x800, 1, &pin };
    int request = 0;

    EXPECT_EQ( networking->XNetworkingVerifyServerCertificate( &request, fromCall ), S_OK );
    EXPECT_EQ( networking->XNetworkingVerifyServerCertificate( &request, &none ), S_OK );
    EXPECT_EQ( networking->XNetworkingVerifyServerCertificate( &request, &pinned ), E_NOTIMPL );
    EXPECT_EQ( networking->XNetworkingVerifyServerCertificate( &request, nullptr ), E_INVALIDARG );
    EXPECT_EQ( networking->XNetworkingVerifyServerCertificate( nullptr, &none ), E_INVALIDARG );
}

/* --- Logging: the url is never written to the log --- */

/* The DLL reads its log settings when it is loaded, so the call runs in a child copy of
 * this program started with them set; the child is also a fresh process for the first-token check. The child test is an ordinary test too: run on its
 * own it makes the same calls without capturing anything. */
TEST( XNetworkingLogChild, DISABLED_SecretUrlCall )
{
    IXNetworkingImpl *networking = GetNetworking<IXNetworkingImpl>( __uuidof( IXNetworkingImpl ) );
    ASSERT_NE( networking, nullptr );

    AsyncCall utf8, utf16;
    ASSERT_EQ( Utf8Variant::BeginString( networking, "https://example.invalid/?sig=SECRET", &utf8.block ), S_OK );
    ASSERT_EQ( Utf16Variant::BeginString( networking, "https://example.invalid/?sig=SECRET", &utf16.block ), S_OK );
    EXPECT_TRUE( utf8.Drain() );
    EXPECT_TRUE( utf16.Drain() );

    /* This is a fresh process, so this is the first registration the library ever sees:
     * its token must already be non-zero. */
    XTaskQueueRegistrationToken first{};
    ASSERT_EQ( networking->XNetworkingRegisterConnectivityHintChanged( nullptr, nullptr, CountHintCallback, &first ), S_OK );
    EXPECT_NE( first.token, 0u );
    EXPECT_EQ( networking->XNetworkingUnregisterConnectivityHintChanged( first, FALSE ), TRUE );

    networking->Release();
}

TEST( XNetworkingLog, UrlIsNeverLogged )
{
    char dir[MAX_PATH];
    char logPath[MAX_PATH];
    ASSERT_NE( GetTempPathA( sizeof( dir ), dir ), 0u );
    snprintf( logPath, sizeof( logPath ), "%sxnetworking-log-%lu-%lu.txt", dir, GetCurrentProcessId(), GetTickCount() );
    DeleteFileA( logPath );

    char exe[MAX_PATH];
    ASSERT_NE( GetModuleFileNameA( nullptr, exe, sizeof( exe ) ), 0u );
    std::string command = std::string( "\"" ) + exe + "\" --gtest_also_run_disabled_tests --gtest_filter=XNetworkingLogChild.DISABLED_SecretUrlCall";

    SetEnvironmentVariableA( "XGAMERUNTIME_LOG_LEVEL", "trace" );
    SetEnvironmentVariableA( "XGAMERUNTIME_LOG_FILE", logPath );

    STARTUPINFOA si{};
    si.cb = sizeof( si );
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessA( nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi );

    SetEnvironmentVariableA( "XGAMERUNTIME_LOG_LEVEL", nullptr );
    SetEnvironmentVariableA( "XGAMERUNTIME_LOG_FILE", nullptr );
    ASSERT_TRUE( started );

    ASSERT_EQ( WaitForSingleObject( pi.hProcess, 120000 ), WAIT_OBJECT_0 );
    DWORD exitCode = 1;
    GetExitCodeProcess( pi.hProcess, &exitCode );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    EXPECT_EQ( exitCode, 0u );

    std::ifstream in( logPath, std::ios::binary );
    std::string log( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    in.close();
    DeleteFileA( logPath );

    /* Both entry points logged, so an empty or misconfigured log cannot pass. */
    EXPECT_NE( log.find( "XNetworkingQuerySecurityInformationForUrlAsync" ), std::string::npos );
    EXPECT_NE( log.find( "XNetworkingQuerySecurityInformationForUrlUtf16Async" ), std::string::npos );
    EXPECT_EQ( log.find( "SECRET" ), std::string::npos );
}
