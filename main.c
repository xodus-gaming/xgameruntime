/*
 * Xbox Game runtime Library
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

#include <initguid.h>
#include <libxml/parser.h>
#include <shlwapi.h>
#include <bcrypt.h>
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xgameruntime);

char *msaAppId = NULL;
UINT32 titleId = 0;
char *storeId = NULL;
char *packageFamilyName = NULL;

static char *compute_package_family_name( const char *name, const char *publisher )
{
    static const char alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
    UINT64 bits = 0;
    BYTE hash[32];
    WCHAR *wide;
    char *pfn;
    int len;

    if (!(len = MultiByteToWideChar( CP_UTF8, 0, publisher, -1, NULL, 0 ))) return NULL;
    if (!(wide = calloc( len, sizeof(WCHAR) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, publisher, -1, wide, len );
    if (BCryptHash( BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (BYTE *)wide, (len - 1) * sizeof(WCHAR), hash, sizeof(hash) ))
    {
        free( wide );
        return NULL;
    }
    free( wide );
    for (int i = 0; i < 8; i++) bits = (bits << 8) | hash[i];
    if (!(pfn = calloc( 1, strlen( name ) + 15 ))) return NULL;
    strcpy( pfn, name );
    strcat( pfn, "_" );
    for (int i = 0; i < 13; i++)
    {
        int shift = 64 - 5 * (i + 1);
        UINT32 value = shift >= 0 ? (bits >> shift) & 31 : (bits << -shift) & 31;
        pfn[strlen( name ) + 1 + i] = alphabet[value];
    }
    return pfn;
}
BOOLEAN fullTrust = FALSE;
BOOLEAN initializeCalled = FALSE;
XTaskQueueHandle processQueue = NULL;
CRITICAL_SECTION processQueueSection;
DWORD tlsIndex;

BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, void *reserved )
{
    XTaskQueueHandle queue;

    TRACE( "hinst %p, reason %lu, reserved %p.\n", hinst, reason, reserved );

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            if ((tlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES) return FALSE;
            InitializeCriticalSection( &processQueueSection );
            if (SUCCEEDED(IXThreadingImpl_XTaskQueueCreate( x_threading_impl, XTaskQueueDispatchMode_ThreadPool, XTaskQueueDispatchMode_ThreadPool, &queue )))
                IXThreadingImpl_XTaskQueueSetCurrentProcessTaskQueue( x_threading_impl, queue );
        case DLL_THREAD_ATTACH:
            TlsSetValue( tlsIndex, FALSE );
            break;
        case DLL_PROCESS_DETACH:
            if (msaAppId) free( msaAppId );
            TlsFree( tlsIndex );
            IXThreadingImpl_XTaskQueueSetCurrentProcessTaskQueue( x_threading_impl, NULL );
            DeleteCriticalSection( &processQueueSection );
            break;
    }
    return TRUE;
}

struct initialize_options
{
    UINT32 unk;
    BOOL isInline;
    const char *gameConfig;
};

HRESULT WINAPI InitializeApiImplEx2( ULONG gdkVer, ULONG gsVer, char mode, const struct initialize_options *options )
{
    char filename[MAX_PATH + 1], *last;
    xmlNodePtr child, root;
    xmlDocPtr config;

    TRACE( "gdkVer %ld, gsVer %ld, mode %d, options %p.\n", gdkVer, gsVer, mode, options );

    if (initializeCalled) return S_OK;
    initializeCalled = TRUE;

    if (options)
    {
        if (options->isInline && !(config = xmlReadMemory( options->gameConfig, strlen( options->gameConfig ), NULL, NULL, 0 )))
            return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
        else if (!(config = xmlReadFile( options->gameConfig, NULL, 0 )))
            return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
    }
    else
    {
        if (!GetModuleFileNameA( NULL, filename, MAX_PATH )) return HRESULT_FROM_WIN32( GetLastError() );
        /* executable can be in a subdirectory, search up the tree until we find MicrosoftGame.config */
        while ((last = strrchr( filename, '\\' )))
        {
            *(last + 1) = 0;
            if (strlen( filename ) + strlen( "MicrosoftGame.config" ) < MAX_PATH)
                strcat( filename, "MicrosoftGame.config" );
            else return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
            if (PathFileExistsA( filename )) break;
            *last = 0;
            if (!strrchr( filename, '\\' )) return E_GAME_MISSING_GAME_CONFIG;
        }
        if (!(config = xmlReadFile( filename, NULL, 0 ))) return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
    }

    if (!(root = xmlDocGetRootElement( config ))) goto badconfig;
    if (!strcmp( (char *)root->name, "Game" ))
    {
        for (child = root->children; child; child = child->next)
            if (child->type == XML_ELEMENT_NODE)
            {
                if (!strcmp( (char *)child->name, "MSAAppId" ))
                    msaAppId = (char *)xmlNodeGetContent( child );
                else if (!strcmp( (char *)child->name, "Identity" ))
                {
                    char *name = (char *)xmlGetProp( child, (const xmlChar *)"Name" );
                    char *publisher = (char *)xmlGetProp( child, (const xmlChar *)"Publisher" );
                    if (name && publisher) packageFamilyName = compute_package_family_name( name, publisher );
                    free( name );
                    free( publisher );
                }
                else if (!strcmp( (char *)child->name, "StoreId" ))
                    storeId = (char *)xmlNodeGetContent( child );
                else if (!strcmp( (char *)child->name, "TitleId" ))
                {
                    char *value = (char *)xmlNodeGetContent( child );
                    titleId = strtoul( value, NULL, 16 );
                    free( value );
                }
                else if (!strcmp( (char *)child->name, "MSAFullTrust" ))
                {
                    char *value = (char *)xmlNodeGetContent( child );
                    fullTrust = !strcmp( value, "true" ) || !strcmp( value, "1" );
                    free( value );
                }
            }
    }
    else goto badconfig;

    xmlFreeDoc( config );
    return S_OK;

badconfig:
    xmlFreeDoc( config );
    return E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
}

HRESULT WINAPI InitializeApiImplEx( ULONG gdkVer, ULONG gsVer, char mode )
{
    return InitializeApiImplEx2( gdkVer, gsVer, mode, NULL );
}

HRESULT WINAPI UninitializeApiImpl( void )
{
    TRACE( "\n" );
    return S_OK;
}

HRESULT WINAPI InitializeApiImpl( ULONG gdkVer, ULONG gsVer )
{
    return InitializeApiImplEx2( gdkVer, gsVer, 0, NULL );
}

HRESULT WINAPI QueryApiImpl( REFCLSID clsid, REFIID iid, void **out )
{
    TRACE( "clsid %s, iid %s, out %p.\n", debugstr_guid( clsid ), debugstr_guid( iid ), out );

    if (IsEqualGUID( clsid, &CLSID_XAccessibilityImpl ))
        return IXAccessibilityImpl_QueryInterface( x_accessibility_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XAppCaptureImpl ))
        return IXAppCaptureImpl_QueryInterface( x_app_capture_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XAppCaptureMetadataImpl ))
        return IXAppCaptureMetadataImpl_QueryInterface( x_app_capture_metadata_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XDisplayImpl ))
        return IXDisplayImpl_QueryInterface( x_display_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XErrorImpl ))
        return IXErrorImpl_QueryInterface( x_error_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameImpl ))
        return IXGameImpl_QueryInterface( x_game_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameActivationImpl ))
        return IXGameActivationImpl_QueryInterface( x_game_activation_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameEventImpl ))
        return IXGameEventImpl_QueryInterface( x_game_event_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameInviteImpl ))
        return IXGameInviteImpl_QueryInterface( x_game_invite_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameProtocolImpl ))
        return IXGameProtocolImpl_QueryInterface( x_game_protocol_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameRuntimeFeatureImpl ))
        return IXGameRuntimeFeatureImpl_QueryInterface( x_game_runtime_feature_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameSaveImpl ))
        return IXGameSaveImpl_QueryInterface( x_game_save_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameStreamingImpl ))
        return IXGameStreamingImpl_QueryInterface( x_game_streaming_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XGameUiImpl ))
        return IXGameUiImpl_QueryInterface( x_game_ui_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XLauncherImpl ))
        return IXLauncherImpl_QueryInterface( x_launcher_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XNetworkingImpl ))
        return IXNetworkingImpl_QueryInterface( x_networking_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XPackageImpl ))
        return IXPackageImpl_QueryInterface( x_package_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XPersistentLocalStorageImpl ))
        return IXPersistentLocalStorageImpl_QueryInterface( x_persistent_local_storage_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XStoreImpl ))
        return IXStoreImpl_QueryInterface( x_store_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XSystemImpl ))
        return IXSystemImpl_QueryInterface( x_system_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XSystemAnalyticsImpl ))
        return IXSystemAnalyticsImpl_QueryInterface( x_system_analytics_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XThreadingImpl ))
        return IXThreadingImpl_QueryInterface( x_threading_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XUserImpl ))
        return IXUserImpl_QueryInterface( x_user_impl, iid, out );
    if (IsEqualGUID( clsid, &CLSID_XUserDeviceImpl ))
        return IXUserDeviceImpl_QueryInterface( x_user_device_impl, iid, out );

    return HRESULT_FROM_WIN32( ERROR_NOT_SUPPORTED );
}
