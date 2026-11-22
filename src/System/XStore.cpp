/*
 * Xbox Game runtime Library
 *  GDK Component: Store API -> XStore (stub)
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

#include "../private.h"

#include <atomic>
#include <cstring>

class XStoreImpl :
    public IXStoreImpl6
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IXStoreImpl ) ||
             iid == __uuidof( IXStoreImpl2 ) ||
             iid == __uuidof( IXStoreImpl3 ) ||
             iid == __uuidof( IXStoreImpl4 ) ||
             iid == __uuidof( IXStoreImpl5 ) ||
             iid == __uuidof( IXStoreImpl6 ) )
        {
            AddRef();
            *out = static_cast<IXStoreImpl6 *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI
    AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>(++ref);
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

        // Static instance, never deleted.
        return curr;
    }

    HRESULT WINAPI XStoreCreateContext(
        const XUserHandle user,
        XStoreContextHandle *storeContextHandle ) override
    {
        FIXME( "XStoreCreateContext stub!\n" );
        if ( storeContextHandle ) *storeContextHandle = {};
        return E_NOTIMPL;
    }

    void WINAPI XStoreCloseContextHandle(
        XStoreContextHandle storeContextHandle ) override
    {
        FIXME( "XStoreCloseContextHandle stub!\n" );
    }

    HRESULT WINAPI XStoreQueryAssociatedProductsAsync(
        const XStoreContextHandle storeContextHandle,
        XStoreProductKind productKinds,
        UINT32 maxItemsToRetrievePerPage,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryAssociatedProductsAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAssociatedProductsResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryAssociatedProductsResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductsAsync(
        const XStoreContextHandle storeContextHandle,
        XStoreProductKind productKinds,
        const char **storeIds,
        SIZE_T storeIdsCount,
        const char **actionFilters,
        SIZE_T actionFiltersCount,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryProductsAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductsResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryProductsResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryEntitledProductsAsync(
        const XStoreContextHandle storeContextHandle,
        XStoreProductKind productKinds,
        UINT32 maxItemsToRetrievePerPage,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryEntitledProductsAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryEntitledProductsResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryEntitledProductsResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductForCurrentGameAsync(
        const XStoreContextHandle storeContextHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryProductForCurrentGameAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductForCurrentGameResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryProductForCurrentGameResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductForPackageAsync(
        const XStoreContextHandle storeContextHandle,
        XStoreProductKind productKinds,
        const char *packageIdentifier,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryProductForPackageAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryProductForPackageResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryProductForPackageResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreEnumerateProductsQuery(
        const XStoreProductQueryHandle productQueryHandle,
        void *context,
        XStoreProductQueryCallback *callback ) override
    {
        FIXME( "XStoreEnumerateProductsQuery stub!\n" );
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XStoreProductsQueryHasMorePages(
        const XStoreProductQueryHandle productQueryHandle ) override
    {
        FIXME( "XStoreProductsQueryHasMorePages stub!\n" );
        return FALSE;
    }

    HRESULT WINAPI XStoreProductsQueryNextPageAsync(
        const XStoreProductQueryHandle productQueryHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreProductsQueryNextPageAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreProductsQueryNextPageResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreProductsQueryNextPageResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    void WINAPI XStoreCloseProductsQueryHandle(
        XStoreProductQueryHandle productQueryHandle ) override
    {
        FIXME( "XStoreCloseProductsQueryHandle stub!\n" );
    }

    HRESULT WINAPI XStoreAcquireLicenseForPackageAsync(
        const XStoreProductQueryHandle productQueryHandle,
        const char *packageIdentifier,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreAcquireLicenseForPackageAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreAcquireLicenseForPackageResult(
        XAsyncBlock *async,
        XStoreLicenseHandle *storeLicenseHandle ) override
    {
        FIXME( "XStoreAcquireLicenseForPackageResult stub!\n" );
        if ( storeLicenseHandle ) *storeLicenseHandle = {};
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XStoreIsLicenseValid(
        const XStoreLicenseHandle storeLicenseHandle ) override
    {
        FIXME( "XStoreIsLicenseValid stub!\n" );
        return FALSE;
    }

    void WINAPI XStoreCloseLicenseHandle(
        XStoreLicenseHandle storeLicenseHandle ) override
    {
        FIXME( "XStoreCloseLicenseHandle stub!\n" );
    }

    HRESULT WINAPI XStoreCanAcquireLicenseForStoreIdAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeProductId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreCanAcquireLicenseForStoreIdAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreCanAcquireLicenseForStoreIdResult(
        XAsyncBlock *async,
        XStoreCanAcquireLicenseResult *storeCanAcquireLicense ) override
    {
        FIXME( "XStoreCanAcquireLicenseForStoreIdResult stub!\n" );
        if ( storeCanAcquireLicense ) *storeCanAcquireLicense = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreCanAcquireLicenseForPackageAsync(
        const XStoreContextHandle storeContextHandle,
        const char *packageIdentifier,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreCanAcquireLicenseForPackageAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreCanAcquireLicenseForPackageResult(
        XAsyncBlock *async,
        XStoreCanAcquireLicenseResult *storeCanAcquireLicense ) override
    {
        FIXME( "XStoreCanAcquireLicenseForPackageResult stub!\n" );
        if ( storeCanAcquireLicense ) *storeCanAcquireLicense = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryGameLicenseAsync(
        const XStoreContextHandle storeContextHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryGameLicenseAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryGameLicenseResult(
        XAsyncBlock *async,
        XStoreGameLicense *license ) override
    {
        FIXME( "XStoreQueryGameLicenseResult stub!\n" );
        if ( license ) *license = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAddOnLicensesAsync(
        const XStoreContextHandle storeContextHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryAddOnLicensesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAddOnLicensesResultCount(
        XAsyncBlock *async,
        UINT32 *count ) override
    {
        FIXME( "XStoreQueryAddOnLicensesResultCount stub!\n" );
        if ( count ) *count = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAddOnLicensesResult(
        XAsyncBlock *async,
        UINT32 count,
        XStoreAddonLicense addOnLicenses[] ) override
    {
        FIXME( "XStoreQueryAddOnLicensesResult stub!\n" );
        if ( addOnLicenses ) memset( addOnLicenses, 0, count * sizeof( *addOnLicenses ) );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryConsumableBalanceRemainingAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeProductId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryConsumableBalanceRemainingAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryConsumableBalanceRemainingResult(
        XAsyncBlock *async,
        XStoreConsumableResult *consumableResult ) override
    {
        FIXME( "XStoreQueryConsumableBalanceRemainingResult stub!\n" );
        if ( consumableResult ) *consumableResult = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreReportConsumableFulfillmentAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeProductId,
        UINT32 quantity,
        GUID trackingId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreReportConsumableFulfillmentAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreReportConsumableFulfillmentResult(
        XAsyncBlock *async,
        XStoreConsumableResult *consumableResult ) override
    {
        FIXME( "XStoreReportConsumableFulfillmentResult stub!\n" );
        if ( consumableResult ) *consumableResult = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserCollectionsIdAsync(
        const XStoreContextHandle storeContextHandle,
        const char *serviceTicket,
        const char *publisherUserId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreGetUserCollectionsIdAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserCollectionsIdResultSize(
        XAsyncBlock *async,
        SIZE_T *size ) override
    {
        FIXME( "XStoreGetUserCollectionsIdResultSize stub!\n" );
        if ( size ) *size = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserCollectionsIdResult(
        XAsyncBlock *async,
        SIZE_T size,
        char *result ) override
    {
        FIXME( "XStoreGetUserCollectionsIdResult stub!\n" );
        if ( result ) memset( result, 0, size );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserPurchaseIdAsync(
        const XStoreContextHandle storeContextHandle,
        const char *serviceTicket,
        const char *publisherUserId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreGetUserPurchaseIdAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserPurchaseIdResultSize(
        XAsyncBlock *async,
        SIZE_T *size ) override
    {
        FIXME( "XStoreGetUserPurchaseIdResultSize stub!\n" );
        if ( size ) *size = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreGetUserPurchaseIdResult(
        XAsyncBlock *async,
        SIZE_T size,
        char *result ) override
    {
        FIXME( "XStoreGetUserPurchaseIdResult stub!\n" );
        if ( result ) memset( result, 0, size );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryLicenseTokenAsync(
        const XStoreContextHandle storeContextHandle,
        const char **productIds,
        SIZE_T productIdsCount,
        const char *customDeveloperString,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryLicenseTokenAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryLicenseTokenResultSize(
        XAsyncBlock *async,
        SIZE_T *size ) override
    {
        FIXME( "XStoreQueryLicenseTokenResultSize stub!\n" );
        if ( size ) *size = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryLicenseTokenResult(
        XAsyncBlock *async,
        SIZE_T size,
        char *result ) override
    {
        FIXME( "XStoreQueryLicenseTokenResult stub!\n" );
        if ( result ) memset( result, 0, size );
        return E_NOTIMPL;
    }

    HRESULT WINAPI __PADDING__( ) override
    {
        FIXME( "__PADDING__ stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI __PADDING_2__( ) override
    {
        FIXME( "__PADDING_2__ stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI __PADDING_3__( ) override
    {
        FIXME( "__PADDING_3__ stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowPurchaseUIAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeId,
        const char *name,
        const char *extendedJsonData,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowPurchaseUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowPurchaseUIResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowPurchaseUIResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowRateAndReviewUIAsync(
        const XStoreContextHandle storeContextHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowRateAndReviewUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowRateAndReviewUIResult(
        XAsyncBlock *async,
        XStoreRateAndReviewResult *result ) override
    {
        FIXME( "XStoreShowRateAndReviewUIResult stub!\n" );
        if ( result ) *result = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowRedeemTokenUIAsync(
        const XStoreContextHandle storeContextHandle,
        const char *token,
        const char **allowedStoreIds,
        SIZE_T allowedStoreIdsCount,
        BOOLEAN disallowCsvRedemption,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowRedeemTokenUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowRedeemTokenUIResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowRedeemTokenUIResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryGameAndDlcPackageUpdatesAsync(
        const XStoreContextHandle storeContextHandle,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryGameAndDlcPackageUpdatesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryGameAndDlcPackageUpdatesResultCount(
        XAsyncBlock *async,
        UINT32 *count ) override
    {
        FIXME( "XStoreQueryGameAndDlcPackageUpdatesResultCount stub!\n" );
        if ( count ) *count = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryGameAndDlcPackageUpdatesResult(
        XAsyncBlock *async,
        UINT32 count,
        XStorePackageUpdate packageUpdates[] ) override
    {
        FIXME( "XStoreQueryGameAndDlcPackageUpdatesResult stub!\n" );
        if ( packageUpdates ) memset( packageUpdates, 0, count * sizeof( *packageUpdates ) );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadPackageUpdatesAsync(
        const XStoreContextHandle storeContextHandle,
        const char **packageIdentifiers,
        SIZE_T packageIdentifiersCount,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreDownloadPackageUpdatesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadPackageUpdatesResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreDownloadPackageUpdatesResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadAndInstallPackageUpdatesAsync(
        const XStoreContextHandle storeContextHandle,
        const char **packageIdentifiers,
        SIZE_T packageIdentifiersCount,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreDownloadAndInstallPackageUpdatesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadAndInstallPackageUpdatesResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreDownloadAndInstallPackageUpdatesResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadAndInstallPackagesAsync(
        const XStoreContextHandle storeContextHandle,
        const char **storeIds,
        SIZE_T storeIdsCount,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreDownloadAndInstallPackagesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadAndInstallPackagesResultCount(
        XAsyncBlock *async,
        UINT32 *count ) override
    {
        FIXME( "XStoreDownloadAndInstallPackagesResultCount stub!\n" );
        if ( count ) *count = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreDownloadAndInstallPackagesResult(
        XAsyncBlock *async,
        UINT32 count,
        char **packageIdentifiers ) override
    {
        FIXME( "XStoreDownloadAndInstallPackagesResult stub!\n" );
        if ( packageIdentifiers ) memset( packageIdentifiers, 0, count * 33 );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryPackageIdentifier(
        const char *storeId,
        SIZE_T size,
        char *packageIdentifier ) override
    {
        FIXME( "XStoreQueryPackageIdentifier stub!\n" );
        if ( packageIdentifier ) memset( packageIdentifier, 0, size );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreRegisterGameLicenseChanged(
        XStoreContextHandle storeContextHandle,
        XTaskQueueHandle queue,
        void *context,
        XStoreGameLicenseChangedCallback *callback,
        XTaskQueueRegistrationToken *token ) override
    {
        FIXME( "XStoreRegisterGameLicenseChanged stub!\n" );
        if ( token ) *token = {};
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XStoreUnregisterGameLicenseChanged(
        XStoreContextHandle storeContextHandle,
        XTaskQueueRegistrationToken token,
        BOOLEAN wait ) override
    {
        FIXME( "XStoreUnregisterGameLicenseChanged stub!\n" );
        return FALSE;
    }

    HRESULT WINAPI XStoreRegisterPackageLicenseLost(
        XStoreLicenseHandle licenseHandle,
        XTaskQueueHandle queue,
        void *context,
        XStorePackageLicenseLostCallback *callback,
        XTaskQueueRegistrationToken *token ) override
    {
        FIXME( "XStoreRegisterPackageLicenseLost stub!\n" );
        if ( token ) *token = {};
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XStoreUnregisterPackageLicenseLost(
        XStoreLicenseHandle licenseHandle,
        XTaskQueueRegistrationToken token,
        BOOLEAN wait ) override
    {
        FIXME( "XStoreUnregisterPackageLicenseLost stub!\n" );
        return FALSE;
    }

    BOOLEAN WINAPI XStoreIsAvailabilityPurchasable(
        const XStoreAvailability availability ) override
    {
        FIXME( "XStoreIsAvailabilityPurchasable stub!\n" );
        return FALSE;
    }

    HRESULT WINAPI XStoreAcquireLicenseForDurablesAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreAcquireLicenseForDurablesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreAcquireLicenseForDurablesResult(
        XAsyncBlock *async,
        XStoreLicenseHandle *storeLicenseHandle ) override
    {
        FIXME( "XStoreAcquireLicenseForDurablesResult stub!\n" );
        if ( storeLicenseHandle ) *storeLicenseHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowAssociatedProductsUIAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeId,
        XStoreProductKind productKinds,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowAssociatedProductsUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowAssociatedProductsUIResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowAssociatedProductsUIResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowProductPageUIAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeId,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowProductPageUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowProductPageUIResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowProductPageUIResult stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAssociatedProductsForStoreIdAsync(
        const XStoreContextHandle storeContextHandle,
        const char *storeId,
        XStoreProductKind productKinds,
        UINT32 maxItemsToRetrievePerPage,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryAssociatedProductsForStoreIdAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryAssociatedProductsForStoreIdResult(
        XAsyncBlock *async,
        XStoreProductQueryHandle *productQueryHandle ) override
    {
        FIXME( "XStoreQueryAssociatedProductsForStoreIdResult stub!\n" );
        if ( productQueryHandle ) *productQueryHandle = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryPackageUpdatesAsync(
        XStoreContextHandle storeContextHandle,
        const char **packageIdentifiers,
        SIZE_T packageIdentifiersCount,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreQueryPackageUpdatesAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryPackageUpdatesResultCount(
        XAsyncBlock *async,
        UINT32 *count ) override
    {
        FIXME( "XStoreQueryPackageUpdatesResultCount stub!\n" );
        if ( count ) *count = {};
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreQueryPackageUpdatesResult(
        XAsyncBlock *async,
        UINT32 count,
        XStorePackageUpdate packageUpdates[] ) override
    {
        FIXME( "XStoreQueryPackageUpdatesResult stub!\n" );
        if ( packageUpdates ) memset( packageUpdates, 0, count * sizeof( *packageUpdates ) );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowGiftingUIAsync(
        XStoreContextHandle storeContextHandle,
        const char *storeId,
        const char *name,
        const char *extendedJsonData,
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowGiftingUIAsync stub!\n" );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XStoreShowGiftingUIResult(
        XAsyncBlock *async ) override
    {
        FIXME( "XStoreShowGiftingUIResult stub!\n" );
        return E_NOTIMPL;
    }

private:
    std::atomic_long ref{ 1 };
};

static XStoreImpl g_x_store;

IXStoreImpl6 *x_store_impl = static_cast<IXStoreImpl6*>(&g_x_store);
