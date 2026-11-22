/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XPackage
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

/*
 * This object is a C-style COM object built on the generated IXPackageImpl4Vtbl.
 * The C++ classes widl generates collapse the re-declared EnumeratePackages and
 * RegisterPackageInstalled methods into one slot each, which gives a 39 slot
 * table, while the real runtime (and the game) use the 42 slot layout.
 * The generated C interface is only available with CINTERFACE, so this file
 * does not include private.h (its C++ interfaces conflict with CINTERFACE).
 */

#define __WINESRC__ 1
#define CINTERFACE
#include <windows.h>
#include <xpackage.h>
#undef CINTERFACE

#include <private/logging.h>
#include <xgameerr.h>

#include <atomic>
#include <cstring>

struct XPackageImpl
{
    const IXPackageImpl4Vtbl *lpVtbl;
    std::atomic_long ref;
};

extern const IXPackageImpl4Vtbl x_package_vtbl;

static XPackageImpl g_x_package = { &x_package_vtbl, { 1 } };

IXPackageImpl4 *x_package_impl = reinterpret_cast<IXPackageImpl4 *>( &g_x_package );

static XPackageImpl *impl_from( IXPackageImpl4 *iface )
{
    return reinterpret_cast<XPackageImpl *>( iface );
}

static HRESULT STDMETHODCALLTYPE QueryInterface( IXPackageImpl4 *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( &iid ), out );

    if (!out) return E_POINTER;
    *out = nullptr;

    if ( IsEqualGUID( iid, IID_IUnknown ) ||
         IsEqualGUID( iid, IID_IXPackageImpl ) ||
         IsEqualGUID( iid, IID_IXPackageImpl2 ) ||
         IsEqualGUID( iid, IID_IXPackageImpl3 ) ||
         IsEqualGUID( iid, IID_IXPackageImpl4 ) )
    {
        iface->lpVtbl->AddRef( iface );
        *out = iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE AddRef( IXPackageImpl4 *iface )
{
    ULONG curr = static_cast<ULONG>( ++impl_from( iface )->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, curr );
    return curr;
}

static ULONG STDMETHODCALLTYPE Release( IXPackageImpl4 *iface )
{
    // The object is static, it is never deleted.
    ULONG curr = static_cast<ULONG>( --impl_from( iface )->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, curr );
    return curr;
}

static HRESULT STDMETHODCALLTYPE XPackageGetCurrentProcessPackageIdentifier( IXPackageImpl4 *iface, SIZE_T bufferSize, char *buffer )
{
    TRACE( "iface %p, bufferSize %Iu, buffer %p.\n", iface, bufferSize, buffer );
    // A loose-file install has no package identity.
    return E_GAMEPACKAGE_NO_PACKAGE_IDENTIFIER;
}

static BOOLEAN STDMETHODCALLTYPE XPackageIsPackagedProcess( IXPackageImpl4 *iface )
{
    TRACE( "iface %p.\n", iface );
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE XPackageCreateInstallationMonitor( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT32 minimumUpdateIntervalMs, XTaskQueueHandle queue, XPackageInstallationMonitorHandle *installationMonitor )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p, minimumUpdateIntervalMs %u, queue %p, installationMonitor %p stub!\n", iface, packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, queue, installationMonitor );
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE XPackageCloseInstallationMonitorHandle( IXPackageImpl4 *iface, XPackageInstallationMonitorHandle installationMonitor )
{
    FIXME( "iface %p, installationMonitor %p stub!\n", iface, installationMonitor );
}

static void STDMETHODCALLTYPE XPackageGetInstallationProgress( IXPackageImpl4 *iface, XPackageInstallationMonitorHandle installationMonitor, XPackageInstallationProgress *progress )
{
    FIXME( "iface %p, installationMonitor %p, progress %p stub!\n", iface, installationMonitor, progress );
}

static BOOLEAN STDMETHODCALLTYPE XPackageUpdateInstallationMonitor( IXPackageImpl4 *iface, XPackageInstallationMonitorHandle installationMonitor )
{
    FIXME( "iface %p, installationMonitor %p stub!\n", iface, installationMonitor );
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE XPackageRegisterInstallationProgressChanged( IXPackageImpl4 *iface, XPackageInstallationMonitorHandle installationMonitor, void *context, XPackageInstallationProgressCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, installationMonitor %p, context %p, callback %p, token %p stub!\n", iface, installationMonitor, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN STDMETHODCALLTYPE XPackageUnregisterInstallationProgressChanged( IXPackageImpl4 *iface, XPackageInstallationMonitorHandle installationMonitor, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, installationMonitor %p, wait %d stub!\n", iface, installationMonitor, wait );
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE XPackageGetUserLocale( IXPackageImpl4 *iface, SIZE_T localeSize, char *locale )
{
    WCHAR name[LOCALE_NAME_MAX_LENGTH];
    char utf8[LOCALE_NAME_MAX_LENGTH * 4];
    DWORD err;
    int len;

    TRACE( "iface %p, localeSize %Iu, locale %p.\n", iface, localeSize, locale );

    if (!GetUserDefaultLocaleName( name, ARRAYSIZE( name ) ))
    {
        err = GetLastError();
        ERR( "GetUserDefaultLocaleName failed, error %lu.\n", err );
        return err ? HRESULT_FROM_WIN32( err ) : E_FAIL;
    }

    // len includes the NUL.
    len = WideCharToMultiByte( CP_UTF8, 0, name, -1, utf8, sizeof( utf8 ), nullptr, nullptr );
    if (!len)
    {
        err = GetLastError();
        ERR( "WideCharToMultiByte failed, error %lu.\n", err );
        return err ? HRESULT_FROM_WIN32( err ) : E_FAIL;
    }

    if (localeSize < static_cast<SIZE_T>( len )) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
    if (!locale) return E_INVALIDARG;

    memcpy( locale, utf8, len );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE XPackageFindChunkAvailability( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], XPackageChunkAvailability *availability )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p, availability %p stub!\n", iface, packageIdentifier, selectorCount, selectors, availability );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageEnumerateChunkAvailability( IXPackageImpl4 *iface, const char *packageIdentifier, XPackageChunkSelectorType type, void *context, XPackageChunkAvailabilityCallback *callback )
{
    FIXME( "iface %p, packageIdentifier %p, type %d, context %p, callback %p stub!\n", iface, packageIdentifier, static_cast<int>( type ), context, callback );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageChangeChunkInstallOrder( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[] )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p stub!\n", iface, packageIdentifier, selectorCount, selectors );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageInstallChunks( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector *selectors, UINT32 minimumUpdateIntervalMs, BOOLEAN suppressUserConfirmation, XTaskQueueHandle queue, XPackageInstallationMonitorHandle *installationMonitor )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p, minimumUpdateIntervalMs %u, suppressUserConfirmation %d, queue %p, installationMonitor %p stub!\n", iface, packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, suppressUserConfirmation, queue, installationMonitor );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageInstallChunksAsync( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT32 minimumUpdateIntervalMs, BOOLEAN suppressUserConfirmation, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p, minimumUpdateIntervalMs %u, suppressUserConfirmation %d, asyncBlock %p stub!\n", iface, packageIdentifier, selectorCount, selectors, minimumUpdateIntervalMs, suppressUserConfirmation, asyncBlock );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageInstallChunksResult( IXPackageImpl4 *iface, XAsyncBlock *asyncBlock, XPackageInstallationMonitorHandle *installationMonitor )
{
    FIXME( "iface %p, asyncBlock %p, installationMonitor %p stub!\n", iface, asyncBlock, installationMonitor );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageEstimateDownloadSize( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[], UINT64 *downloadSize, BOOLEAN *shouldPresentUserConfirmation )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p, downloadSize %p, shouldPresentUserConfirmation %p stub!\n", iface, packageIdentifier, selectorCount, selectors, downloadSize, shouldPresentUserConfirmation );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageUninstallChunks( IXPackageImpl4 *iface, const char *packageIdentifier, UINT32 selectorCount, XPackageChunkSelector selectors[] )
{
    FIXME( "iface %p, packageIdentifier %p, selectorCount %u, selectors %p stub!\n", iface, packageIdentifier, selectorCount, selectors );
    return E_NOTIMPL;
}

// Slots 20, 21, 23, 27 and 31 are padding: the real runtime has methods there that no public API names.
template <int Slot>
static HRESULT STDMETHODCALLTYPE Padding( IXPackageImpl4 *iface )
{
    FIXME( "iface %p padding slot %d stub!\n", iface, Slot );
    return E_NOTIMPL;
}

static BOOLEAN STDMETHODCALLTYPE XPackageUnregisterPackageInstalled( IXPackageImpl4 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, wait %d stub!\n", iface, wait );
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE XPackageGetMountPathSize( IXPackageImpl4 *iface, XPackageMountHandle mount, SIZE_T *pathSize )
{
    FIXME( "iface %p, mount %p, pathSize %p stub!\n", iface, mount, pathSize );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageGetMountPath( IXPackageImpl4 *iface, XPackageMountHandle mount, SIZE_T pathSize, char *path )
{
    FIXME( "iface %p, mount %p, pathSize %Iu, path %p stub!\n", iface, mount, pathSize, path );
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE XPackageCloseMountHandle( IXPackageImpl4 *iface, XPackageMountHandle mount )
{
    FIXME( "iface %p, mount %p stub!\n", iface, mount );
}

// A loose-file install has no packages and no features: the enumerations report an empty set and call nothing back.
static HRESULT STDMETHODCALLTYPE XPackageEnumeratePackages( IXPackageImpl4 *iface, XPackageKind kind, XPackageEnumerationScope scope, void *context, XPackageEnumerationCallback *callback )
{
    TRACE( "iface %p, kind %d, scope %d, context %p, callback %p.\n", iface, static_cast<int>( kind ), static_cast<int>( scope ), context, callback );
    return callback ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE XPackageRegisterPackageInstalled( IXPackageImpl4 *iface, XTaskQueueHandle queue, void *context, XPackageInstalledCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageGetWriteStats( IXPackageImpl4 *iface, XPackageWriteStats *writeStats )
{
    FIXME( "iface %p, writeStats %p stub!\n", iface, writeStats );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageUninstallUWPInstance( IXPackageImpl4 *iface, const char *packageName )
{
    FIXME( "iface %p, packageName %p stub!\n", iface, packageName );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageEnumerateFeatures( IXPackageImpl4 *iface, const char *packageIdentifier, void *context, XPackageFeatureEnumerationCallback *callback )
{
    TRACE( "iface %p, packageIdentifier %p, context %p, callback %p.\n", iface, packageIdentifier, context, callback );
    return callback ? S_OK : E_INVALIDARG;
}

static BOOLEAN STDMETHODCALLTYPE XPackageUninstallPackage( IXPackageImpl4 *iface, const char *packageIdentifier )
{
    FIXME( "iface %p, packageIdentifier %p stub!\n", iface, packageIdentifier );
    return FALSE;
}

static HRESULT STDMETHODCALLTYPE XPackageMountWithUiAsync( IXPackageImpl4 *iface, const char *packageIdentifier, XAsyncBlock *async )
{
    FIXME( "iface %p, packageIdentifier %p, async %p stub!\n", iface, packageIdentifier, async );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageMountWithUiResult( IXPackageImpl4 *iface, XAsyncBlock *async, XPackageMountHandle *mount )
{
    FIXME( "iface %p, async %p, mount %p stub!\n", iface, async, mount );
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE XPackageGetKind( IXPackageImpl4 *iface, const char *packageIdentifier, XPackageKind *kind )
{
    FIXME( "iface %p, packageIdentifier %p, kind %p stub!\n", iface, packageIdentifier, kind );
    return E_NOTIMPL;
}

// One table serves every accepted IID: each interface's vtable is a prefix of the next.
// The re-declared methods (slots 35, 36, 39, 40) reuse the IXPackageImpl implementations.
const IXPackageImpl4Vtbl x_package_vtbl =
{
    QueryInterface,
    AddRef,
    Release,
    XPackageGetCurrentProcessPackageIdentifier,
    XPackageIsPackagedProcess,
    XPackageCreateInstallationMonitor,
    XPackageCloseInstallationMonitorHandle,
    XPackageGetInstallationProgress,
    XPackageUpdateInstallationMonitor,
    XPackageRegisterInstallationProgressChanged,
    XPackageUnregisterInstallationProgressChanged,
    XPackageGetUserLocale,
    XPackageFindChunkAvailability,
    XPackageEnumerateChunkAvailability,
    XPackageChangeChunkInstallOrder,
    XPackageInstallChunks,
    XPackageInstallChunksAsync,
    XPackageInstallChunksResult,
    XPackageEstimateDownloadSize,
    XPackageUninstallChunks,
    Padding<20>,
    Padding<21>,
    XPackageUnregisterPackageInstalled,
    Padding<23>,
    XPackageGetMountPathSize,
    XPackageGetMountPath,
    XPackageCloseMountHandle,
    Padding<27>,
    XPackageEnumeratePackages,
    XPackageRegisterPackageInstalled,
    XPackageGetWriteStats,
    Padding<31>,
    XPackageUninstallUWPInstance,
    XPackageEnumerateFeatures,
    XPackageUninstallPackage,
    XPackageEnumeratePackages,
    XPackageRegisterPackageInstalled,
    XPackageMountWithUiAsync,
    XPackageMountWithUiResult,
    XPackageEnumeratePackages,
    XPackageRegisterPackageInstalled,
    XPackageGetKind,
};
