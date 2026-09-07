/*
 * Xbox Game runtime Library
 *
 * Written by Weather.
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

#include <initguid.h>
#include "private.h"

#ifdef __cplusplus
extern "C" {
#endif

static volatile BOOL socketInitialized = FALSE;
static volatile LONG loggerInitialized = FALSE;

BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, void *reserved )
{
    if ( !loggerInitialized )
    {
        InitializeLogging();
        loggerInitialized = TRUE;
    }

    TRACE("inst %p, reason %lu, reserved %p.\n", hinst, reason, reserved);

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hinst);
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            if (reserved) break;
            break;
        }
    }

    return TRUE;
}

HRESULT WINAPI InitializeApiImplEx2( ULONG gdkVer, ULONG gsVer, char mode, const INITIALIZE_OPTIONS *options )
{
    HRESULT status = S_OK;

    TRACE( "gdkVer %ld, gsVer %ld, mode %d, options %p.\n", gdkVer, gsVer, mode, options );

    if ( !socketInitialized )
    {
        if ( SUCCEEDED( status = xodus_ipclayer->InitializeSocket() ) )
            socketInitialized = TRUE;
        else
            throw Exception( status, "ABI::Xodus::IIPCLayer->InitializeSocket() failed" );
    }

    return S_OK;
}

HRESULT WINAPI InitializeApiImplEx( ULONG gdkVer, ULONG gsVer, char mode )
{
    return InitializeApiImplEx2( gdkVer, gsVer, mode, NULL );
}

HRESULT WINAPI InitializeApiImpl( ULONG gdkVer, ULONG gsVer )
{
    return InitializeApiImplEx2( gdkVer, gsVer, 0, NULL );
}

HRESULT WINAPI QueryApiImpl( REFCLSID clsid, REFIID iid, void **out )
{
    // ------------ FOR FUTURE REFERENCES ------------
    //
    // Interfaces returned are COM interfaces and inherit IUnknown*
    //
    //  On MSDN, There's no official documentation on the order of these interfaces and functions.
    // However, we can hook a dummy `xgameruntime.dll` into test environments and individually query
    // each class and what signatures they posses. Once we've pass through an empty IUnknown* interface,
    // we can reconstruct the vtable of each class based on what function gets called.
    //
    //  Example: (e349bd1a-fc20-4e40-b99c-4178cc6b409f) corresponds to part of the `ISystem` class and implements
    // these functions in order:
    //
    //  /*** IUnknown methods ***/
    //  IXSystemImpl_QueryInterface,                    (offset 0)
    //  IXSystemImpl_AddRef,                            (offset 8)
    //  IXSystemImpl_Release,                           (offset 16)
    //  /*** IXSystemImpl methods ***/
    //  IXSystemImpl_XSystemGetConsoleId                (offset 24)
    //  IXSystemImpl_XSystemGetXboxLiveSandboxId        (offset 32)
    //  IXSystemImpl_XSystemGetAppSpecificDeviceId      (offset 40)
    //  IXSystemImpl_XSystemHandleTrack                 (offset 48)
    //  IXSystemImpl_XSystemIsHandleValid               (offset 56)
    //  IXSystemImpl_XSystemAllowFullDownloadBandwidth  (offset 64)
    //
    TRACE( "clsid %s, iid %s, out %p.\n", debugstr_guid( &clsid ), debugstr_guid( &iid ), out );

    if ( IsEqualGUID( clsid, CLSID_XThreadingImpl ) )
    {
        return x_threading_impl->QueryInterface( iid, out );
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &clsid ) );
    return E_NOTIMPL;
}

HRESULT WINAPI UninitializeApiImpl()
{
    TRACE("stub!\n");
    return E_NOTIMPL;
}

HRESULT WINAPI XErrorReport( HRESULT status, LPCSTR message )
{
    TRACE("stub!\n");
    return E_NOTIMPL;
}

#ifdef __cplusplus
}
#endif