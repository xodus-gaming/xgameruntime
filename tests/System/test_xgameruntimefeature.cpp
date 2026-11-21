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

TEST_F( XGameRuntimeFeatureTests, KnownFeaturesAreAvailable )
{
    for ( UINT32 value = 0; value <= static_cast<UINT32>( XGameRuntimeFeature::XGameStreaming ); value++ )
        EXPECT_TRUE( XGameRuntimeIsFeatureAvailable( static_cast<XGameRuntimeFeature>( value ) ) ) << "feature " << value;
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

TEST_F( XGameRuntimeFeatureTests, UnknownClsidIsNotRegistered )
{
    QueryApiImplFn queryApiImpl = LoadQueryApiImpl();
    ASSERT_NE( queryApiImpl, nullptr );

    void *out = nullptr;
    EXPECT_EQ( queryApiImpl( kUnknownGuid, __uuidof( IUnknown ), &out ), REGDB_E_CLASSNOTREG );
}
