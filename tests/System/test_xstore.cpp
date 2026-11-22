/*
 * XStore Tests
 *
 * Written by Weather
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

#include <cstring>
#include <vector>

#include <windows.h>
#include <xasync.h>
#include <xstore.h>

// A byte buffer pre-filled with 0xCD, to check that a stub zeroes its [out] parameters.
class Buf
{
public:
    explicit Buf( size_t size ) : bytes( size, 0xCD ) {}

    void *ptr() { return bytes.data(); }

    bool zero() const
    {
        for ( unsigned char b : bytes )
            if ( b != 0 ) return false;
        return true;
    }

private:
    std::vector<unsigned char> bytes;
};

class XStoreTests : public XGameRuntimeTests
{
protected:
    typedef HRESULT (WINAPI *QueryApiImplFn)( REFCLSID clsid, REFIID iid, void **out );

    static HRESULT Query( REFIID iid, void **out )
    {
        HMODULE module = LoadLibraryA( "xgameruntime.dll" );
        if ( !module ) return E_FAIL;

        auto query = reinterpret_cast<QueryApiImplFn>( GetProcAddress( module, "QueryApiImpl" ) );
        if ( !query ) return E_FAIL;

        return query( CLSID_XStoreImpl, iid, out );
    }
};

TEST_F(XStoreTests, QueryInterface)
{
    const IID *iids[] = {
        &IID_IUnknown,
        &IID_IXStoreImpl,
        &IID_IXStoreImpl2,
        &IID_IXStoreImpl3,
        &IID_IXStoreImpl4,
        &IID_IXStoreImpl5,
        &IID_IXStoreImpl6,
    };

    for ( const IID *iid : iids )
    {
        void *out = nullptr;
        EXPECT_EQ( Query( *iid, &out ), S_OK );
        EXPECT_NE( out, nullptr );
        if ( out ) static_cast<IUnknown *>( out )->Release();
    }

    void *out = reinterpret_cast<void *>( 1 );
    EXPECT_EQ( Query( IID_IPersist, &out ), E_NOINTERFACE );
    EXPECT_EQ( out, nullptr );
}

TEST_F(XStoreTests, RefCount)
{
    IXStoreImpl6 *s = nullptr;
    ASSERT_EQ( Query( IID_IXStoreImpl6, reinterpret_cast<void **>( &s ) ), S_OK );

    // Other callers share the instance, so only check deltas.
    ULONG base = s->AddRef();
    EXPECT_EQ( s->Release(), base - 1 );
    EXPECT_EQ( s->AddRef(), base );
    EXPECT_EQ( s->AddRef(), base + 1 );
    EXPECT_EQ( s->Release(), base );
    EXPECT_EQ( s->Release(), base - 1 );

    // A successful QueryInterface takes exactly one reference; a failed one takes none.
    void *out = nullptr;
    ASSERT_EQ( Query( IID_IXStoreImpl, &out ), S_OK );
    ULONG after = s->AddRef();
    EXPECT_EQ( after, base + 1 );
    s->Release();
    static_cast<IUnknown *>( out )->Release();

    out = nullptr;
    EXPECT_EQ( Query( IID_IPersist, &out ), E_NOINTERFACE );
    EXPECT_EQ( s->AddRef(), base );
    s->Release();

    // Drop the reference taken by the first Query; the object is static and must stay callable.
    s->Release();
    EXPECT_FALSE( s->XStoreProductsQueryHasMorePages( nullptr ) );
}

TEST_F(XStoreTests, MethodTable)
{
    IXStoreImpl6 *s = nullptr;
    ASSERT_EQ( Query( IID_IXStoreImpl6, reinterpret_cast<void **>( &s ) ), S_OK );

    // XAsyncBlock arguments must never be touched.
    XAsyncBlock async;
    memset( &async, 0xCD, sizeof( async ) );
    Buf asyncBefore( sizeof( async ) );
    memcpy( asyncBefore.ptr(), &async, sizeof( async ) );

    {
        SCOPED_TRACE( "XStoreCreateContext" );
        Buf o1( sizeof( XStoreContextHandle ) );
        EXPECT_EQ( s->XStoreCreateContext( nullptr, static_cast<XStoreContextHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreCreateContext( nullptr, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreCloseContextHandle" );
        s->XStoreCloseContextHandle( nullptr );
    }

    {
        SCOPED_TRACE( "XStoreQueryAssociatedProductsAsync" );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsAsync( nullptr, XStoreProductKind{}, UINT32{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAssociatedProductsResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductsAsync" );
        EXPECT_EQ( s->XStoreQueryProductsAsync( nullptr, XStoreProductKind{}, nullptr, SIZE_T{}, nullptr, SIZE_T{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductsResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryProductsResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryProductsResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryEntitledProductsAsync" );
        EXPECT_EQ( s->XStoreQueryEntitledProductsAsync( nullptr, XStoreProductKind{}, UINT32{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryEntitledProductsResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryEntitledProductsResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryEntitledProductsResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductForCurrentGameAsync" );
        EXPECT_EQ( s->XStoreQueryProductForCurrentGameAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductForCurrentGameResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryProductForCurrentGameResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryProductForCurrentGameResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductForPackageAsync" );
        EXPECT_EQ( s->XStoreQueryProductForPackageAsync( nullptr, XStoreProductKind{}, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryProductForPackageResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryProductForPackageResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryProductForPackageResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreEnumerateProductsQuery" );
        EXPECT_EQ( s->XStoreEnumerateProductsQuery( nullptr, nullptr, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreProductsQueryHasMorePages" );
        EXPECT_FALSE( s->XStoreProductsQueryHasMorePages( nullptr ) );
    }

    {
        SCOPED_TRACE( "XStoreProductsQueryNextPageAsync" );
        EXPECT_EQ( s->XStoreProductsQueryNextPageAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreProductsQueryNextPageResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreProductsQueryNextPageResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreProductsQueryNextPageResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreCloseProductsQueryHandle" );
        s->XStoreCloseProductsQueryHandle( nullptr );
    }

    {
        SCOPED_TRACE( "XStoreAcquireLicenseForPackageAsync" );
        EXPECT_EQ( s->XStoreAcquireLicenseForPackageAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreAcquireLicenseForPackageResult" );
        Buf o1( sizeof( XStoreLicenseHandle ) );
        EXPECT_EQ( s->XStoreAcquireLicenseForPackageResult( &async, static_cast<XStoreLicenseHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreAcquireLicenseForPackageResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreIsLicenseValid" );
        EXPECT_FALSE( s->XStoreIsLicenseValid( nullptr ) );
    }

    {
        SCOPED_TRACE( "XStoreCloseLicenseHandle" );
        s->XStoreCloseLicenseHandle( nullptr );
    }

    {
        SCOPED_TRACE( "XStoreCanAcquireLicenseForStoreIdAsync" );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForStoreIdAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreCanAcquireLicenseForStoreIdResult" );
        Buf o1( sizeof( XStoreCanAcquireLicenseResult ) );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForStoreIdResult( &async, static_cast<XStoreCanAcquireLicenseResult *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForStoreIdResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreCanAcquireLicenseForPackageAsync" );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForPackageAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreCanAcquireLicenseForPackageResult" );
        Buf o1( sizeof( XStoreCanAcquireLicenseResult ) );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForPackageResult( &async, static_cast<XStoreCanAcquireLicenseResult *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreCanAcquireLicenseForPackageResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryGameLicenseAsync" );
        EXPECT_EQ( s->XStoreQueryGameLicenseAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryGameLicenseResult" );
        Buf o1( sizeof( XStoreGameLicense ) );
        EXPECT_EQ( s->XStoreQueryGameLicenseResult( &async, static_cast<XStoreGameLicense *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryGameLicenseResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAddOnLicensesAsync" );
        EXPECT_EQ( s->XStoreQueryAddOnLicensesAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAddOnLicensesResultCount" );
        Buf o1( sizeof( UINT32 ) );
        EXPECT_EQ( s->XStoreQueryAddOnLicensesResultCount( &async, static_cast<UINT32 *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryAddOnLicensesResultCount( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAddOnLicensesResult" );
        Buf o2( 2 * sizeof( XStoreAddonLicense ) );
        EXPECT_EQ( s->XStoreQueryAddOnLicensesResult( &async, 2, static_cast<XStoreAddonLicense *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreQueryAddOnLicensesResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryConsumableBalanceRemainingAsync" );
        EXPECT_EQ( s->XStoreQueryConsumableBalanceRemainingAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryConsumableBalanceRemainingResult" );
        Buf o1( sizeof( XStoreConsumableResult ) );
        EXPECT_EQ( s->XStoreQueryConsumableBalanceRemainingResult( &async, static_cast<XStoreConsumableResult *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryConsumableBalanceRemainingResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreReportConsumableFulfillmentAsync" );
        EXPECT_EQ( s->XStoreReportConsumableFulfillmentAsync( nullptr, nullptr, UINT32{}, GUID{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreReportConsumableFulfillmentResult" );
        Buf o1( sizeof( XStoreConsumableResult ) );
        EXPECT_EQ( s->XStoreReportConsumableFulfillmentResult( &async, static_cast<XStoreConsumableResult *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreReportConsumableFulfillmentResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserCollectionsIdAsync" );
        EXPECT_EQ( s->XStoreGetUserCollectionsIdAsync( nullptr, nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserCollectionsIdResultSize" );
        Buf o1( sizeof( SIZE_T ) );
        EXPECT_EQ( s->XStoreGetUserCollectionsIdResultSize( &async, static_cast<SIZE_T *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreGetUserCollectionsIdResultSize( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserCollectionsIdResult" );
        Buf o2( 2 );
        EXPECT_EQ( s->XStoreGetUserCollectionsIdResult( &async, 2, static_cast<char *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreGetUserCollectionsIdResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserPurchaseIdAsync" );
        EXPECT_EQ( s->XStoreGetUserPurchaseIdAsync( nullptr, nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserPurchaseIdResultSize" );
        Buf o1( sizeof( SIZE_T ) );
        EXPECT_EQ( s->XStoreGetUserPurchaseIdResultSize( &async, static_cast<SIZE_T *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreGetUserPurchaseIdResultSize( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreGetUserPurchaseIdResult" );
        Buf o2( 2 );
        EXPECT_EQ( s->XStoreGetUserPurchaseIdResult( &async, 2, static_cast<char *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreGetUserPurchaseIdResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryLicenseTokenAsync" );
        EXPECT_EQ( s->XStoreQueryLicenseTokenAsync( nullptr, nullptr, SIZE_T{}, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryLicenseTokenResultSize" );
        Buf o1( sizeof( SIZE_T ) );
        EXPECT_EQ( s->XStoreQueryLicenseTokenResultSize( &async, static_cast<SIZE_T *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryLicenseTokenResultSize( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryLicenseTokenResult" );
        Buf o2( 2 );
        EXPECT_EQ( s->XStoreQueryLicenseTokenResult( &async, 2, static_cast<char *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreQueryLicenseTokenResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "__PADDING__" );
        EXPECT_EQ( s->__PADDING__(  ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "__PADDING_2__" );
        EXPECT_EQ( s->__PADDING_2__(  ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "__PADDING_3__" );
        EXPECT_EQ( s->__PADDING_3__(  ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowPurchaseUIAsync" );
        EXPECT_EQ( s->XStoreShowPurchaseUIAsync( nullptr, nullptr, nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowPurchaseUIResult" );
        EXPECT_EQ( s->XStoreShowPurchaseUIResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowRateAndReviewUIAsync" );
        EXPECT_EQ( s->XStoreShowRateAndReviewUIAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowRateAndReviewUIResult" );
        Buf o1( sizeof( XStoreRateAndReviewResult ) );
        EXPECT_EQ( s->XStoreShowRateAndReviewUIResult( &async, static_cast<XStoreRateAndReviewResult *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreShowRateAndReviewUIResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowRedeemTokenUIAsync" );
        EXPECT_EQ( s->XStoreShowRedeemTokenUIAsync( nullptr, nullptr, nullptr, SIZE_T{}, TRUE, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowRedeemTokenUIResult" );
        EXPECT_EQ( s->XStoreShowRedeemTokenUIResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryGameAndDlcPackageUpdatesAsync" );
        EXPECT_EQ( s->XStoreQueryGameAndDlcPackageUpdatesAsync( nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryGameAndDlcPackageUpdatesResultCount" );
        Buf o1( sizeof( UINT32 ) );
        EXPECT_EQ( s->XStoreQueryGameAndDlcPackageUpdatesResultCount( &async, static_cast<UINT32 *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryGameAndDlcPackageUpdatesResultCount( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryGameAndDlcPackageUpdatesResult" );
        Buf o2( 2 * sizeof( XStorePackageUpdate ) );
        EXPECT_EQ( s->XStoreQueryGameAndDlcPackageUpdatesResult( &async, 2, static_cast<XStorePackageUpdate *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreQueryGameAndDlcPackageUpdatesResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadPackageUpdatesAsync" );
        EXPECT_EQ( s->XStoreDownloadPackageUpdatesAsync( nullptr, nullptr, SIZE_T{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadPackageUpdatesResult" );
        EXPECT_EQ( s->XStoreDownloadPackageUpdatesResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadAndInstallPackageUpdatesAsync" );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackageUpdatesAsync( nullptr, nullptr, SIZE_T{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadAndInstallPackageUpdatesResult" );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackageUpdatesResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadAndInstallPackagesAsync" );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackagesAsync( nullptr, nullptr, SIZE_T{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadAndInstallPackagesResultCount" );
        Buf o1( sizeof( UINT32 ) );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackagesResultCount( &async, static_cast<UINT32 *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackagesResultCount( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreDownloadAndInstallPackagesResult" );
        Buf o2( 2 * 33 );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackagesResult( &async, 2, static_cast<char **>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreDownloadAndInstallPackagesResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryPackageIdentifier" );
        Buf o2( 2 );
        EXPECT_EQ( s->XStoreQueryPackageIdentifier( nullptr, 2, static_cast<char *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreQueryPackageIdentifier( nullptr, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreRegisterGameLicenseChanged" );
        Buf o4( sizeof( XTaskQueueRegistrationToken ) );
        EXPECT_EQ( s->XStoreRegisterGameLicenseChanged( nullptr, nullptr, nullptr, nullptr, static_cast<XTaskQueueRegistrationToken *>( o4.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o4.zero() );
        EXPECT_EQ( s->XStoreRegisterGameLicenseChanged( nullptr, nullptr, nullptr, nullptr, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreUnregisterGameLicenseChanged" );
        EXPECT_FALSE( s->XStoreUnregisterGameLicenseChanged( nullptr, XTaskQueueRegistrationToken{}, TRUE ) );
    }

    {
        SCOPED_TRACE( "XStoreRegisterPackageLicenseLost" );
        Buf o4( sizeof( XTaskQueueRegistrationToken ) );
        EXPECT_EQ( s->XStoreRegisterPackageLicenseLost( nullptr, nullptr, nullptr, nullptr, static_cast<XTaskQueueRegistrationToken *>( o4.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o4.zero() );
        EXPECT_EQ( s->XStoreRegisterPackageLicenseLost( nullptr, nullptr, nullptr, nullptr, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreUnregisterPackageLicenseLost" );
        EXPECT_FALSE( s->XStoreUnregisterPackageLicenseLost( nullptr, XTaskQueueRegistrationToken{}, TRUE ) );
    }

    {
        SCOPED_TRACE( "XStoreIsAvailabilityPurchasable" );
        EXPECT_FALSE( s->XStoreIsAvailabilityPurchasable( XStoreAvailability{} ) );
    }

    {
        SCOPED_TRACE( "XStoreAcquireLicenseForDurablesAsync" );
        EXPECT_EQ( s->XStoreAcquireLicenseForDurablesAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreAcquireLicenseForDurablesResult" );
        Buf o1( sizeof( XStoreLicenseHandle ) );
        EXPECT_EQ( s->XStoreAcquireLicenseForDurablesResult( &async, static_cast<XStoreLicenseHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreAcquireLicenseForDurablesResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowAssociatedProductsUIAsync" );
        EXPECT_EQ( s->XStoreShowAssociatedProductsUIAsync( nullptr, nullptr, XStoreProductKind{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowAssociatedProductsUIResult" );
        EXPECT_EQ( s->XStoreShowAssociatedProductsUIResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowProductPageUIAsync" );
        EXPECT_EQ( s->XStoreShowProductPageUIAsync( nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowProductPageUIResult" );
        EXPECT_EQ( s->XStoreShowProductPageUIResult( &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAssociatedProductsForStoreIdAsync" );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsForStoreIdAsync( nullptr, nullptr, XStoreProductKind{}, UINT32{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryAssociatedProductsForStoreIdResult" );
        Buf o1( sizeof( XStoreProductQueryHandle ) );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsForStoreIdResult( &async, static_cast<XStoreProductQueryHandle *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryAssociatedProductsForStoreIdResult( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryPackageUpdatesAsync" );
        EXPECT_EQ( s->XStoreQueryPackageUpdatesAsync( nullptr, nullptr, SIZE_T{}, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryPackageUpdatesResultCount" );
        Buf o1( sizeof( UINT32 ) );
        EXPECT_EQ( s->XStoreQueryPackageUpdatesResultCount( &async, static_cast<UINT32 *>( o1.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o1.zero() );
        EXPECT_EQ( s->XStoreQueryPackageUpdatesResultCount( &async, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreQueryPackageUpdatesResult" );
        Buf o2( 2 * sizeof( XStorePackageUpdate ) );
        EXPECT_EQ( s->XStoreQueryPackageUpdatesResult( &async, 2, static_cast<XStorePackageUpdate *>( o2.ptr() ) ), E_NOTIMPL );
        EXPECT_TRUE( o2.zero() );
        EXPECT_EQ( s->XStoreQueryPackageUpdatesResult( &async, 2, nullptr ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowGiftingUIAsync" );
        EXPECT_EQ( s->XStoreShowGiftingUIAsync( nullptr, nullptr, nullptr, nullptr, &async ), E_NOTIMPL );
    }

    {
        SCOPED_TRACE( "XStoreShowGiftingUIResult" );
        EXPECT_EQ( s->XStoreShowGiftingUIResult( &async ), E_NOTIMPL );
    }

    EXPECT_EQ( memcmp( &async, asyncBefore.ptr(), sizeof( async ) ), 0 );

    s->Release();
}
