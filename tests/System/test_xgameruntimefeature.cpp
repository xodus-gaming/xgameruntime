/*
 * Xbox Game runtime Library
 *  Tests: XGameRuntimeFeature
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

#include <windows.h>
#include <inspectable.h>
#include <objidl.h>
#include <xgameruntimeinit.h>
#include <xgameruntimefeature.h>
#include <xgame.h>
#include <xsystem.h>
#include <xnetworking.h>
#include <xuser.h>
#include <xerror.h>
#include <xasyncprovider.h>
#include <private/logging.h>

namespace
{
    // Not a GUID of any interface the provider implements.
    constexpr GUID kUnknownGuid = { 0x12345678, 0x9abc, 0xdef0, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };

    typedef HRESULT (WINAPI *QueryApiImplFn)( REFCLSID clsid, REFIID iid, void **out );

    QueryApiImplFn LoadQueryApiImpl()
    {
        HMODULE module = LoadLibraryA( "xgameruntime.dll" );
        EXPECT_NE( module, nullptr ) << "LoadLibraryA failed: " << GetLastError();
        if ( !module ) return nullptr;

        auto fn = reinterpret_cast<QueryApiImplFn>( GetProcAddress( module, "QueryApiImpl" ) );
        EXPECT_NE( fn, nullptr ) << "GetProcAddress(QueryApiImpl) failed: " << GetLastError();
        return fn;
    }
}

class XGameRuntimeFeatureTests : public XGameRuntimeTests
{
};

namespace
{
    typedef HRESULT (WINAPI *InitializeApiImplExFn)( ULONG gdkVer, ULONG gsVer, char mode );

    // Sets the init mode through the DLL's own export and restores 0 on scope exit.
    class InitModeGuard
    {
    public:
        explicit InitModeGuard( char mode ) { Set( mode ); }
        ~InitModeGuard() { Set( 0 ); }
    private:
        static void Set( char mode )
        {
            HMODULE module = LoadLibraryA( "xgameruntime.dll" );
            ASSERT_NE( module, nullptr );
            auto fn = reinterpret_cast<InitializeApiImplExFn>( GetProcAddress( module, "InitializeApiImplEx" ) );
            ASSERT_NE( fn, nullptr );
            ASSERT_EQ( fn( 0, 0, mode ), S_OK );
        }
    };

    constexpr bool kInitFlagGroup[22] = {
        true, true, false, false, true, false, false, true, false, true, false,
        true, true, true, true, false, false, false, false, false, true, true };
    constexpr bool kAlwaysGroup[22] = {
        false, false, true, true, false, false, true, false, true, false, false,
        false, false, false, false, false, true, true, false, false, false, false };
    // XGame 5, XNetworking 10, XSystem 15, XUser 18 and XError 19 follow their class; none is served here.
    constexpr bool kServedOnDev[22] = {};
}

TEST_F( XGameRuntimeFeatureTests, FeatureAvailabilityMatchesWindows )
{
    for ( UINT32 value = 0; value <= static_cast<UINT32>( XGameRuntimeFeature::XGameStreaming ); value++ )
    {
        bool expected = kAlwaysGroup[value] || kInitFlagGroup[value] || kServedOnDev[value];
        EXPECT_EQ( !!XGameRuntimeIsFeatureAvailable( static_cast<XGameRuntimeFeature>( value ) ), expected ) << "feature " << value;
    }
}

TEST_F( XGameRuntimeFeatureTests, ComponentFeaturesFollowServedClasses )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    struct { XGameRuntimeFeature feature; const CLSID *clsid; } rows[] = {
        { XGameRuntimeFeature::XGame,       &CLSID_XGameImpl },
        { XGameRuntimeFeature::XNetworking, &CLSID_XNetworkingImpl },
        { XGameRuntimeFeature::XSystem,     &CLSID_XSystemImpl },
        { XGameRuntimeFeature::XUser,       &CLSID_XUserImpl },
        { XGameRuntimeFeature::XError,      &CLSID_XErrorImpl },
    };
    for ( const auto &row : rows )
    {
        IUnknown *out = nullptr;
        HRESULT hr = queryApiImpl( *row.clsid, __uuidof( IUnknown ), reinterpret_cast<void **>( &out ) );
        bool served = SUCCEEDED( hr );
        if ( out ) out->Release();
        EXPECT_EQ( !!XGameRuntimeIsFeatureAvailable( row.feature ), served ) << "feature " << static_cast<UINT32>( row.feature );
    }
}

TEST_F( XGameRuntimeFeatureTests, InitFlagHidesTheThirdGroup )
{
    {
        InitModeGuard guard( 8 );
        for ( UINT32 value = 0; value <= 21; value++ )
        {
            bool expected = kAlwaysGroup[value] || kServedOnDev[value];
            EXPECT_EQ( !!XGameRuntimeIsFeatureAvailable( static_cast<XGameRuntimeFeature>( value ) ), expected ) << "feature " << value;
        }
    }
    for ( char mode : { char( 0 ), char( 0x7 ) } )
    {
        InitModeGuard guard( mode );
        for ( UINT32 value = 0; value <= 21; value++ )
        {
            bool expected = kAlwaysGroup[value] || kInitFlagGroup[value] || kServedOnDev[value];
            EXPECT_EQ( !!XGameRuntimeIsFeatureAvailable( static_cast<XGameRuntimeFeature>( value ) ), expected ) << "mode " << int( mode ) << " feature " << value;
        }
    }
}

TEST_F( XGameRuntimeFeatureTests, InitializeApiImplResetsAndEx2Records )
{
    HMODULE module = LoadLibraryA( "xgameruntime.dll" );
    ASSERT_NE( module, nullptr );
    typedef HRESULT (WINAPI *InitFn)( ULONG gdkVer, ULONG gsVer );
    typedef HRESULT (WINAPI *InitEx2Fn)( ULONG gdkVer, ULONG gsVer, char mode, const void *options );
    auto init = reinterpret_cast<InitFn>( GetProcAddress( module, "InitializeApiImpl" ) );
    auto initEx2 = reinterpret_cast<InitEx2Fn>( GetProcAddress( module, "InitializeApiImplEx2" ) );
    ASSERT_NE( init, nullptr );
    ASSERT_NE( initEx2, nullptr );

    const XGameRuntimeFeature thirdGroup = XGameRuntimeFeature::XPackage;

    ASSERT_EQ( initEx2( 0, 0, 8, nullptr ), S_OK );
    EXPECT_FALSE( XGameRuntimeIsFeatureAvailable( thirdGroup ) ) << "Ex2 must record bit 3";

    ASSERT_EQ( init( 0, 0 ), S_OK );
    EXPECT_TRUE( XGameRuntimeIsFeatureAvailable( thirdGroup ) ) << "InitializeApiImpl must record 0";

    ASSERT_EQ( initEx2( 0, 0, 0, nullptr ), S_OK );
}

TEST_F( XGameRuntimeFeatureTests, ServedClsidsQueryAndUnservedAreNotSupported )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    const CLSID *served[] = { &CLSID_XThreadingImpl, &CLSID_XGameRuntimeFeatureImpl };
    for ( const CLSID *clsid : served )
    {
        IUnknown *out = nullptr;
        EXPECT_EQ( queryApiImpl( *clsid, __uuidof( IUnknown ), reinterpret_cast<void **>( &out ) ), S_OK ) << debugstr_guid( clsid );
        if ( out ) out->Release();
    }

    for ( const CLSID *clsid : { &CLSID_XUserImpl, &CLSID_XErrorImpl, &kUnknownGuid } )
    {
        void *out = reinterpret_cast<void *>( 1 );
        EXPECT_EQ( queryApiImpl( *clsid, __uuidof( IUnknown ), &out ), HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED ) );
        EXPECT_EQ( out, reinterpret_cast<void *>( 1 ) );
    }
}

TEST_F( XGameRuntimeFeatureTests, UnknownFeaturesAreNotAvailable )
{
    // 21 is the last known feature (XGameStreaming), 22 is the first unknown one.
    for ( UINT32 value : { 22u, 23u, 0x7FFFFFFFu, 0xFFFFFFFFu } )
        EXPECT_FALSE( XGameRuntimeIsFeatureAvailable( static_cast<XGameRuntimeFeature>( value ) ) ) << "feature " << value;
}

TEST_F( XGameRuntimeFeatureTests, QueryInterfaceSucceedsForSupportedIids )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    struct { const char *name; GUID iid; } iids[] = {
        { "IUnknown",                  __uuidof( IUnknown ) },
        { "IInspectable",              __uuidof( IInspectable ) },
        { "IAgileObject",              __uuidof( IAgileObject ) },
        { "IXGameRuntimeFeatureImpl",  __uuidof( IXGameRuntimeFeatureImpl ) },
    };

    for ( const auto &entry : iids )
    {
        IUnknown *out = nullptr;
        HRESULT hr = queryApiImpl( CLSID_XGameRuntimeFeatureImpl, entry.iid, reinterpret_cast<void **>( &out ) );
        EXPECT_EQ( hr, S_OK ) << entry.name;
        EXPECT_NE( out, nullptr ) << entry.name;
        if ( out ) out->Release();
    }
}

TEST_F( XGameRuntimeFeatureTests, QueryInterfaceRejectsUnknownIidAndNullOut )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    void *out = reinterpret_cast<void *>( 1 );
    EXPECT_EQ( queryApiImpl( CLSID_XGameRuntimeFeatureImpl, kUnknownGuid, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );

    EXPECT_EQ( queryApiImpl( CLSID_XGameRuntimeFeatureImpl, __uuidof( IUnknown ), nullptr ), E_POINTER );
}

TEST_F( XGameRuntimeFeatureTests, EveryServedClsidRejectsAnUnknownIid )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    const CLSID *served[] = { &CLSID_XThreadingImpl, &CLSID_XGameRuntimeFeatureImpl };
    for ( const CLSID *clsid : served )
    {
        void *out = reinterpret_cast<void *>( 1 );
        EXPECT_EQ( queryApiImpl( *clsid, kUnknownGuid, &out ), E_NOINTERFACE ) << debugstr_guid( clsid );
        EXPECT_EQ( out, nullptr ) << debugstr_guid( clsid );
    }
}

TEST_F( XGameRuntimeFeatureTests, QueryInterfaceAddsOneReference )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    IUnknown *first = nullptr;
    ASSERT_EQ( queryApiImpl( CLSID_XGameRuntimeFeatureImpl, __uuidof( IUnknown ), reinterpret_cast<void **>( &first ) ), S_OK );
    ASSERT_NE( first, nullptr );

    // The object is static, so the count is shared: measure it before and after the second query.
    first->AddRef();
    ULONG before = first->Release();

    IUnknown *second = nullptr;
    ASSERT_EQ( queryApiImpl( CLSID_XGameRuntimeFeatureImpl, __uuidof( IUnknown ), reinterpret_cast<void **>( &second ) ), S_OK );
    ASSERT_NE( second, nullptr );

    second->AddRef();
    EXPECT_EQ( second->Release(), before + 1 );
    EXPECT_EQ( second->Release(), before );

    first->Release();
}
