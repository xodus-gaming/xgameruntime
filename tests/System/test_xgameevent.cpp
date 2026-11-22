/*
 * Xbox Game runtime Library
 *  Tests: XGameEvent
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
#include <xgameruntimeinit.h>
#include <xgameevent.h>

namespace
{
    typedef HRESULT ( WINAPI *QueryApiImplFn )( REFCLSID clsid, REFIID iid, void **out );

    // Each test below stays valid with only these arguments changed.
    const XUserHandle kUser = reinterpret_cast<XUserHandle>( static_cast<uintptr_t>( 1 ) );
    const char kCfg[] = "00000000-0000-0000-0000-000000000000";
    const char kSession[] = "session-1";
    const char kName[] = "TestEvent";
    const char kDims[] = "{\"a\":\"b\"}";
    const char kMeas[] = "{\"x\":1}";

    // IIDs the object must not accept (defined here to avoid pulling in WinRT headers).
    const GUID kIidInspectable = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
    const GUID kIidAgileObject = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
}

class XGameEventTests : public XGameRuntimeTests
{
protected:
    QueryApiImplFn query = nullptr;

    void SetUp() override
    {
        ASSERT_HRESULT_SUCCEEDED( XGameRuntimeInitialize() );
        HMODULE dll = GetModuleHandleA( "xgameruntime.dll" );
        ASSERT_NE( dll, nullptr );
        query = reinterpret_cast<QueryApiImplFn>( GetProcAddress( dll, "QueryApiImpl" ) );
        ASSERT_NE( query, nullptr );
    }

    void TearDown() override
    {
        XGameRuntimeUninitialize();
    }

    IXGameEventImpl *Get()
    {
        IXGameEventImpl *obj = nullptr;
        EXPECT_EQ( query( CLSID_XGameEventImpl, __uuidof( IXGameEventImpl ), reinterpret_cast<void **>( &obj ) ), S_OK );
        return obj;
    }
};

// --- REQ-001 ---

TEST_F( XGameEventTests, QueryInterfaceAcceptsUnknownAndOwnIid )
{
    IUnknown *unk = nullptr;
    IXGameEventImpl *impl = nullptr;
    ASSERT_EQ( query( CLSID_XGameEventImpl, __uuidof( IUnknown ), reinterpret_cast<void **>( &unk ) ), S_OK );
    ASSERT_EQ( query( CLSID_XGameEventImpl, __uuidof( IXGameEventImpl ), reinterpret_cast<void **>( &impl ) ), S_OK );
    EXPECT_NE( unk, nullptr );
    EXPECT_EQ( static_cast<void *>( unk ), static_cast<void *>( impl ) );
    unk->Release();
    impl->Release();
}

TEST_F( XGameEventTests, QueryInterfaceRejectsOtherIid )
{
    void *out = reinterpret_cast<void *>( 0x1 );
    EXPECT_EQ( query( CLSID_XGameEventImpl, kIidInspectable, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
    out = reinterpret_cast<void *>( 0x1 );
    EXPECT_EQ( query( CLSID_XGameEventImpl, kIidAgileObject, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
}

TEST_F( XGameEventTests, QueryInterfaceNullOut )
{
    IXGameEventImpl *impl = Get();
    ASSERT_NE( impl, nullptr );
    EXPECT_EQ( impl->QueryInterface( __uuidof( IUnknown ), nullptr ), E_POINTER );
    impl->Release();
}

TEST_F( XGameEventTests, RefCounting )
{
    IXGameEventImpl *impl = Get();
    ASSERT_NE( impl, nullptr );
    ULONG base = impl->AddRef();
    EXPECT_EQ( impl->AddRef(), base + 1 );
    EXPECT_EQ( impl->Release(), base );
    void *again = nullptr;
    ASSERT_EQ( impl->QueryInterface( __uuidof( IUnknown ), &again ), S_OK );
    EXPECT_EQ( static_cast<IUnknown *>( again )->Release(), base );
    EXPECT_EQ( impl->Release(), base - 1 );
    impl->Release();
}

// --- REQ-002 (through the public API) ---

TEST_F( XGameEventTests, ValidArgumentsSucceed )
{
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, kDims, kMeas ), S_OK );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, kDims, kMeas ), S_OK );
}

TEST_F( XGameEventTests, RequiredArgumentsNullFail )
{
    EXPECT_EQ( XGameEventWrite( nullptr, kCfg, kSession, kName, kDims, kMeas ), E_INVALIDARG );
    EXPECT_EQ( XGameEventWrite( kUser, nullptr, kSession, kName, kDims, kMeas ), E_INVALIDARG );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, nullptr, kName, kDims, kMeas ), E_INVALIDARG );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, nullptr, kDims, kMeas ), E_INVALIDARG );
}

TEST_F( XGameEventTests, EmptyConfigIdAndEventNameFail )
{
    EXPECT_EQ( XGameEventWrite( kUser, "", kSession, kName, kDims, kMeas ), E_INVALIDARG );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, "", kDims, kMeas ), E_INVALIDARG );
}

TEST_F( XGameEventTests, EmptyPlaySessionIdAccepted )
{
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, "", kName, kDims, kMeas ), S_OK );
}

TEST_F( XGameEventTests, JsonArgumentsOptional )
{
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, nullptr, nullptr ), S_OK );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, "", "" ), S_OK );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, kDims, nullptr ), S_OK );
    EXPECT_EQ( XGameEventWrite( kUser, kCfg, kSession, kName, nullptr, kMeas ), S_OK );
}
