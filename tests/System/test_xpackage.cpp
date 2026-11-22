/*
 * XPackage Tests
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

#include <cstddef>
#include <cstring>
#include <string>

#include <windows.h>
#include <xgameruntimeinit.h>
#include <xgameerr.h>

// The generated C interface (IXPackageImpl4Vtbl) is only available with CINTERFACE.
#define CINTERFACE
#include <xpackage.h>
#undef CINTERFACE

// Public wrappers, defined in libxgameruntime.
HRESULT __stdcall XPackageGetUserLocale( SIZE_T localeSize, char *locale );
BOOLEAN __stdcall XPackageIsPackagedProcess();
HRESULT __stdcall XPackageGetCurrentProcessPackageIdentifier( SIZE_T bufferSize, char *buffer );

typedef HRESULT (WINAPI *QueryApiImplFn)( REFCLSID, REFIID, void ** );

static constexpr size_t kSlots = 42;
static constexpr unsigned char kCanary = 0xCC;

// The slot positions below are written from build/windows-x64/include/xpackage.h (IXPackageImpl4Vtbl);
// the asserts check the ones the tests rely on against the generated struct.
#define SLOT(name) (offsetof( IXPackageImpl4Vtbl, name ) / sizeof( void * ))
static_assert( sizeof( IXPackageImpl4Vtbl ) == kSlots * sizeof( void * ), "IXPackageImpl4Vtbl must have 42 slots" );
static_assert( SLOT( XPackageGetCurrentProcessPackageIdentifier ) == 3, "slot 3" );
static_assert( SLOT( XPackageGetUserLocale ) == 11, "slot 11" );
static_assert( SLOT( __PADDING__ ) == 20, "slot 20" );
static_assert( SLOT( __PADDING_2__ ) == 21, "slot 21" );
static_assert( SLOT( __PADDING_3__ ) == 23, "slot 23" );
static_assert( SLOT( __PADDING_4__ ) == 27, "slot 27" );
static_assert( SLOT( __PADDING_5__ ) == 31, "slot 31" );
static_assert( SLOT( XPackageEnumeratePackages ) == 28, "slot 28" );
static_assert( SLOT( XPackageEnumerateFeatures ) == 33, "slot 33" );
static_assert( SLOT( IXPackageImpl2_XPackageEnumeratePackages ) == 35, "slot 35" );
static_assert( SLOT( IXPackageImpl3_XPackageEnumeratePackages ) == 39, "slot 39" );
static_assert( SLOT( XPackageGetKind ) == 41, "slot 41" );

enum SlotKind
{
    IUNKNOWN,   // slots 0..2, checked by the QueryInterface tests
    REAL,       // slots with their own tests
    STUB_HR,    // returns E_NOTIMPL, writes nothing
    STUB_BOOL,  // returns FALSE
    STUB_VOID,  // returns, writes nothing
};

struct SlotInfo
{
    const char *name;
    SlotKind kind;
};

static const GUID unknown = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };

static const SlotInfo kSlotTable[kSlots] =
{
    { "QueryInterface", IUNKNOWN },
    { "AddRef", IUNKNOWN },
    { "Release", IUNKNOWN },
    { "XPackageGetCurrentProcessPackageIdentifier", REAL },
    { "XPackageIsPackagedProcess", REAL },
    { "XPackageCreateInstallationMonitor", STUB_HR },
    { "XPackageCloseInstallationMonitorHandle", STUB_VOID },
    { "XPackageGetInstallationProgress", STUB_VOID },
    { "XPackageUpdateInstallationMonitor", STUB_BOOL },
    { "XPackageRegisterInstallationProgressChanged", STUB_HR },
    { "XPackageUnregisterInstallationProgressChanged", STUB_BOOL },
    { "XPackageGetUserLocale", REAL },
    { "XPackageFindChunkAvailability", STUB_HR },
    { "XPackageEnumerateChunkAvailability", STUB_HR },
    { "XPackageChangeChunkInstallOrder", STUB_HR },
    { "XPackageInstallChunks", STUB_HR },
    { "XPackageInstallChunksAsync", STUB_HR },
    { "XPackageInstallChunksResult", STUB_HR },
    { "XPackageEstimateDownloadSize", STUB_HR },
    { "XPackageUninstallChunks", STUB_HR },
    { "__PADDING__", STUB_HR },
    { "__PADDING_2__", STUB_HR },
    { "XPackageUnregisterPackageInstalled", STUB_BOOL },
    { "__PADDING_3__", STUB_HR },
    { "XPackageGetMountPathSize", STUB_HR },
    { "XPackageGetMountPath", STUB_HR },
    { "XPackageCloseMountHandle", STUB_VOID },
    { "__PADDING_4__", STUB_HR },
    { "XPackageEnumeratePackages", REAL },
    { "XPackageRegisterPackageInstalled", STUB_HR },
    { "XPackageGetWriteStats", STUB_HR },
    { "__PADDING_5__", STUB_HR },
    { "XPackageUninstallUWPInstance", STUB_HR },
    { "XPackageEnumerateFeatures", REAL },
    { "XPackageUninstallPackage", STUB_BOOL },
    { "IXPackageImpl2_XPackageEnumeratePackages", REAL },
    { "IXPackageImpl2_XPackageRegisterPackageInstalled", STUB_HR },
    { "XPackageMountWithUiAsync", STUB_HR },
    { "XPackageMountWithUiResult", STUB_HR },
    { "IXPackageImpl3_XPackageEnumeratePackages", REAL },
    { "IXPackageImpl3_XPackageRegisterPackageInstalled", STUB_HR },
    { "XPackageGetKind", STUB_HR },
};

// Every slot is called with the same shape: this plus up to 8 pointer-sized arguments.
// All of them are pointers to canary buffers, so a stub that writes through any argument is caught.
typedef HRESULT (STDMETHODCALLTYPE *AnySlotHr)( void *, void *, void *, void *, void *, void *, void *, void *, void * );
typedef BOOLEAN (STDMETHODCALLTYPE *AnySlotBool)( void *, void *, void *, void *, void *, void *, void *, void *, void * );
typedef void (STDMETHODCALLTYPE *AnySlotVoid)( void *, void *, void *, void *, void *, void *, void *, void *, void * );

static int g_callbackCount;

static BOOLEAN __stdcall CountingPackagesCallback( void *, const XPackageDetails * )
{
    g_callbackCount++;
    return TRUE;
}

static BOOLEAN __stdcall CountingFeaturesCallback( void *, const XPackageFeature * )
{
    g_callbackCount++;
    return TRUE;
}

class XPackageTests : public XGameRuntimeTests
{
protected:
    static IXPackageImpl4 *obj;
    static QueryApiImplFn queryApiImpl;

    static void SetUpTestSuite()
    {
        XGameRuntimeTests::SetUpTestSuite();
        ASSERT_EQ( XGameRuntimeInitialize(), S_OK );
        HMODULE module = LoadLibraryA( "xgameruntime.dll" );
        ASSERT_NE( module, nullptr );
        queryApiImpl = reinterpret_cast<QueryApiImplFn>( GetProcAddress( module, "QueryApiImpl" ) );
        ASSERT_NE( queryApiImpl, nullptr );
        ASSERT_EQ( queryApiImpl( CLSID_XPackageImpl, IID_IXPackageImpl4, reinterpret_cast<void **>( &obj ) ), S_OK );
        ASSERT_NE( obj, nullptr );
        obj->lpVtbl->Release( obj );
    }

    static void *Slot( size_t index )
    {
        return reinterpret_cast<void *const *>( obj->lpVtbl )[index];
    }

    static std::string ExpectedLocale()
    {
        WCHAR wide[LOCALE_NAME_MAX_LENGTH];
        int n = GetUserDefaultLocaleName( wide, LOCALE_NAME_MAX_LENGTH );
        EXPECT_GT( n, 1 );
        std::string out;
        // Locale names are ASCII (for example en-US), widening each character is an independent conversion.
        for ( int i = 0; i < n - 1; i++ ) out.push_back( static_cast<char>( wide[i] ) );
        return out;
    }
};

IXPackageImpl4 *XPackageTests::obj;
QueryApiImplFn XPackageTests::queryApiImpl;

// --- REQ-001 ---

TEST_F( XPackageTests, QueryInterfaceAcceptedIids )
{
    const IID *iids[] = { &IID_IUnknown, &IID_IXPackageImpl, &IID_IXPackageImpl2, &IID_IXPackageImpl3, &IID_IXPackageImpl4 };
    void *first = nullptr;

    for ( const IID *iid : iids )
    {
        void *out = nullptr;
        ASSERT_EQ( queryApiImpl( CLSID_XPackageImpl, *iid, &out ), S_OK );
        ASSERT_NE( out, nullptr );
        if (!first) first = out;
        EXPECT_EQ( out, first );
        EXPECT_EQ( out, obj );
        static_cast<IXPackageImpl4 *>( out )->lpVtbl->Release( static_cast<IXPackageImpl4 *>( out ) );
    }
}

TEST_F( XPackageTests, QueryInterfaceUnknownIid )
{
    void *out = reinterpret_cast<void *>( 0x1 );
    EXPECT_EQ( obj->lpVtbl->QueryInterface( obj, unknown, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
}

TEST_F( XPackageTests, QueryInterfaceNullOut )
{
    EXPECT_EQ( obj->lpVtbl->QueryInterface( obj, IID_IXPackageImpl4, nullptr ), E_POINTER );
    EXPECT_EQ( obj->lpVtbl->QueryInterface( obj, unknown, nullptr ), E_POINTER );
}

TEST_F( XPackageTests, RefCounting )
{
    ULONG n = obj->lpVtbl->AddRef( obj );
    EXPECT_EQ( obj->lpVtbl->AddRef( obj ), n + 1 );
    EXPECT_EQ( obj->lpVtbl->Release( obj ), n );

    void *out = nullptr;
    ASSERT_EQ( obj->lpVtbl->QueryInterface( obj, IID_IXPackageImpl3, &out ), S_OK );
    EXPECT_EQ( obj->lpVtbl->AddRef( obj ), n + 2 );  // QI added exactly one reference
    EXPECT_EQ( obj->lpVtbl->Release( obj ), n + 1 );
    EXPECT_EQ( obj->lpVtbl->Release( obj ), n );
    EXPECT_EQ( obj->lpVtbl->AddRef( obj ), n + 1 );
    EXPECT_EQ( obj->lpVtbl->Release( obj ), n );

    EXPECT_EQ( obj->lpVtbl->Release( obj ), n - 1 );  // drop the QI reference, back to the start
    EXPECT_EQ( obj->lpVtbl->AddRef( obj ), n );
    obj->lpVtbl->Release( obj );
}

TEST_F( XPackageTests, UnregisteredClass )
{
    static const GUID unknownClass = { 0x87654321, 0x4321, 0x4321, { 8, 7, 6, 5, 4, 3, 2, 1 } };
    void *out = nullptr;
    EXPECT_EQ( queryApiImpl( unknownClass, IID_IUnknown, &out ), REGDB_E_CLASSNOTREG );
}

// --- REQ-002 / REQ-006 ---

TEST_F( XPackageTests, SlotTable )
{
    for ( size_t i = 0; i < kSlots; i++ )
    {
        const SlotInfo &info = kSlotTable[i];
        ASSERT_NE( Slot( i ), nullptr ) << "slot " << i << " " << info.name;
    }
}

TEST_F( XPackageTests, StubSlots )
{
    for ( size_t i = 0; i < kSlots; i++ )
    {
        const SlotInfo &info = kSlotTable[i];
        if ( info.kind == IUNKNOWN || info.kind == REAL ) continue;

        unsigned char canary[256];
        memset( canary, kCanary, sizeof( canary ) );
        void *p = canary;

        if ( info.kind == STUB_HR )
            EXPECT_EQ( reinterpret_cast<AnySlotHr>( Slot( i ) )( obj, p, p, p, p, p, p, p, p ), E_NOTIMPL ) << "slot " << i << " " << info.name;
        else if ( info.kind == STUB_BOOL )
            EXPECT_EQ( reinterpret_cast<AnySlotBool>( Slot( i ) )( obj, p, p, p, p, p, p, p, p ), FALSE ) << "slot " << i << " " << info.name;
        else
            reinterpret_cast<AnySlotVoid>( Slot( i ) )( obj, p, p, p, p, p, p, p, p );

        for ( unsigned char b : canary )
            ASSERT_EQ( b, kCanary ) << "slot " << i << " " << info.name << " wrote its output";
    }
}

// --- REQ-003 ---

TEST_F( XPackageTests, UserLocaleGameCall )
{
    std::string expected = ExpectedLocale();
    unsigned char buf[128];
    memset( buf, kCanary, sizeof( buf ) );

    EXPECT_EQ( XPackageGetUserLocale( 128, reinterpret_cast<char *>( buf ) ), S_OK );
    EXPECT_STREQ( reinterpret_cast<char *>( buf ), expected.c_str() );
    EXPECT_EQ( buf[expected.size()], 0 );
    for ( size_t i = expected.size() + 1; i < sizeof( buf ); i++ )
        ASSERT_EQ( buf[i], kCanary ) << "byte " << i << " after the NUL was touched";
}

TEST_F( XPackageTests, UserLocaleThroughVtable )
{
    std::string expected = ExpectedLocale();
    char buf[128] = {};
    EXPECT_EQ( obj->lpVtbl->XPackageGetUserLocale( obj, sizeof( buf ), buf ), S_OK );
    EXPECT_STREQ( buf, expected.c_str() );
}

TEST_F( XPackageTests, UserLocaleSizes )
{
    std::string expected = ExpectedLocale();
    size_t len = expected.size();
    HRESULT insufficient = HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );

    for ( size_t size : { size_t( 0 ), size_t( 1 ), len } )
    {
        unsigned char buf[128];
        memset( buf, kCanary, sizeof( buf ) );
        EXPECT_EQ( XPackageGetUserLocale( size, reinterpret_cast<char *>( buf ) ), insufficient ) << "size " << size;
        for ( unsigned char b : buf ) ASSERT_EQ( b, kCanary ) << "size " << size;
    }

    // A short size reports the error before the null buffer check.
    EXPECT_EQ( XPackageGetUserLocale( len, nullptr ), insufficient );

    // Exactly strlen + 1 succeeds and writes the NUL.
    unsigned char buf[128];
    memset( buf, kCanary, sizeof( buf ) );
    EXPECT_EQ( XPackageGetUserLocale( len + 1, reinterpret_cast<char *>( buf ) ), S_OK );
    EXPECT_EQ( std::string( reinterpret_cast<char *>( buf ) ), expected );
    EXPECT_EQ( buf[len], 0 );
    EXPECT_EQ( buf[len + 1], kCanary );
}

TEST_F( XPackageTests, UserLocaleNullBuffer )
{
    EXPECT_EQ( XPackageGetUserLocale( 128, nullptr ), E_INVALIDARG );
}

// --- REQ-004 ---

TEST_F( XPackageTests, IsPackagedProcess )
{
    EXPECT_EQ( XPackageIsPackagedProcess(), FALSE );
    EXPECT_EQ( obj->lpVtbl->XPackageIsPackagedProcess( obj ), FALSE );
}

TEST_F( XPackageTests, CurrentProcessPackageIdentifier )
{
    const HRESULT noIdentifier = static_cast<HRESULT>( 0x89245208 );
    EXPECT_EQ( E_GAMEPACKAGE_NO_PACKAGE_IDENTIFIER, noIdentifier );

    EXPECT_EQ( XPackageGetCurrentProcessPackageIdentifier( 0, nullptr ), noIdentifier );
    EXPECT_EQ( XPackageGetCurrentProcessPackageIdentifier( 260, nullptr ), noIdentifier );

    unsigned char buf[260];
    memset( buf, kCanary, sizeof( buf ) );
    EXPECT_EQ( XPackageGetCurrentProcessPackageIdentifier( sizeof( buf ), reinterpret_cast<char *>( buf ) ), noIdentifier );
    for ( unsigned char b : buf ) ASSERT_EQ( b, kCanary );
}

// --- REQ-005 ---

TEST_F( XPackageTests, EnumeratePackages )
{
    const size_t slots[] = { 28, 35, 39 };
    for ( size_t slot : slots )
    {
        auto fn = reinterpret_cast<HRESULT (STDMETHODCALLTYPE *)( IXPackageImpl4 *, XPackageKind, XPackageEnumerationScope, void *, XPackageEnumerationCallback * )>( Slot( slot ) );
        for ( unsigned kind = 0; kind < 3; kind++ )
            for ( unsigned scope = 0; scope < 3; scope++ )
            {
                g_callbackCount = 0;
                EXPECT_EQ( fn( obj, static_cast<XPackageKind>( kind ), static_cast<XPackageEnumerationScope>( scope ), nullptr, CountingPackagesCallback ), S_OK ) << "slot " << slot;
                EXPECT_EQ( g_callbackCount, 0 ) << "slot " << slot;
            }
        EXPECT_EQ( fn( obj, static_cast<XPackageKind>( 0 ), static_cast<XPackageEnumerationScope>( 0 ), nullptr, nullptr ), E_INVALIDARG ) << "slot " << slot;
    }
}

TEST_F( XPackageTests, EnumerateFeatures )
{
    g_callbackCount = 0;
    EXPECT_EQ( obj->lpVtbl->XPackageEnumerateFeatures( obj, nullptr, nullptr, CountingFeaturesCallback ), S_OK );
    EXPECT_EQ( obj->lpVtbl->XPackageEnumerateFeatures( obj, "some.package", nullptr, CountingFeaturesCallback ), S_OK );
    EXPECT_EQ( g_callbackCount, 0 );
    EXPECT_EQ( obj->lpVtbl->XPackageEnumerateFeatures( obj, nullptr, nullptr, nullptr ), E_INVALIDARG );
    EXPECT_EQ( obj->lpVtbl->XPackageEnumerateFeatures( obj, "some.package", nullptr, nullptr ), E_INVALIDARG );
}
