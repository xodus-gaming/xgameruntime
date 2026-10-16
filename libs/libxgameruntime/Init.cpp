/*
 * Xbox Game runtime Library
 *  Static Library -> XGameRuntimeInit
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
#include "private.h"

HRESULT __stdcall
XGameRuntimeInitializeWithOptions(
    const XGameRuntimeOptions* options
) {
    HRESULT status = S_OK;
    INITIALIZE_OPTIONS initOptions{ .unknown = 1l };

    ensureLoggerInitialized();

    TRACE( "options %p\n", options );

    if ( options )
    {
        switch ( options->gameConfigSource )
        {
            case XGameRuntimeGameConfigSource::Default:
                break;

            case XGameRuntimeGameConfigSource::Inline:
                if ( !options->gameConfig )
                    return E_INVALIDARG;

                initOptions.isInlineConfig = TRUE;
                initOptions.gameConfig = options->gameConfig;
                break;

            case XGameRuntimeGameConfigSource::File:
                if ( !options->gameConfig )
                    return E_INVALIDARG;

                initOptions.isInlineConfig = FALSE;
                initOptions.gameConfig = options->gameConfig;
                break;

            default:
                return E_INVALIDARG;
        }
    }

    {
        std::lock_guard<std::mutex> lock( GlobalState::g_lock );
        GlobalState::xgameruntime = LoadLibraryA( "xgameruntime.dll" );
        if ( !GlobalState::xgameruntime )
        {
            ERR("xgameruntime.dll not found!\n");
            return E_GAMERUNTIME_DLL_NOT_FOUND;
        }

        /** These functions are base functions needed by every xgameruntime library **/
        if ( !GlobalState::InitializeApiImpl )
            GlobalState::InitializeApiImpl =
                reinterpret_cast<HRESULT (WINAPI *)( ULONG gdkVer, ULONG gsVer )>
                (GetProcAddress( GlobalState::xgameruntime, "InitializeApiImpl" ));

        if ( !GlobalState::QueryApiImpl )
            GlobalState::QueryApiImpl =
                reinterpret_cast<HRESULT (WINAPI *)( REFCLSID clsid, REFIID iid, void **out )>
                (GetProcAddress( GlobalState::xgameruntime, "QueryApiImpl" ));

        if ( !GlobalState::UninitializeApiImpl )
            GlobalState::UninitializeApiImpl =
                reinterpret_cast<HRESULT (WINAPI *)()>
                (GetProcAddress( GlobalState::xgameruntime, "UninitializeApiImpl" ));

        if ( !GlobalState::XErrorReport )
            GlobalState::XErrorReport =
                reinterpret_cast<HRESULT (WINAPI *)( HRESULT status, LPCSTR message )>
                (GetProcAddress( GlobalState::xgameruntime, "XErrorReport" ));

        /** Functions that have been added later to the library **/
        if ( !GlobalState::InitializeApiImplEx2 )
            GlobalState::InitializeApiImplEx2 =
                reinterpret_cast<HRESULT (WINAPI *)( ULONG gdkVer, ULONG gsVer, char mode, const INITIALIZE_OPTIONS *options) >
                (GetProcAddress( GlobalState::xgameruntime, "InitializeApiImplEx2" ));

        if ( !GlobalState::InitializeApiImplEx )
            GlobalState::InitializeApiImplEx =
                reinterpret_cast<HRESULT (WINAPI *)( ULONG gdkVer, ULONG gsVer, char mode )>
                (GetProcAddress( GlobalState::xgameruntime, "InitializeApiImplEx" ));

        if ( !GlobalState::InitializeApiImpl || !GlobalState::QueryApiImpl || !GlobalState::UninitializeApiImpl || !GlobalState::XErrorReport )
        {
            ERR("xgameruntime.dll found, but doesn't include correct signatures!\n");
            return E_GAMERUNTIME_DLL_NOT_FOUND;
        }

        if ( GlobalState::initialized )
        {
            if ( options )
            {
                if ( !GlobalState::InitializeApiImplEx2 )
                {
                    LPCSTR errorMessage = "XGameRuntime is outdated and does not support these initialization options.";
                    ERR("%s\n", errorMessage);
                    GlobalState::XErrorReport( E_GAMERUNTIME_OPTIONS_NOT_SUPPORTED, errorMessage );
                    return E_GAMERUNTIME_OPTIONS_NOT_SUPPORTED;
                }
                status = GlobalState::InitializeApiImplEx2( GDKC_VERSION, GAMING_SERVICES_VERSION, 6, &initOptions );
            }
        }
        else
        {
            if ( GlobalState::InitializeApiImplEx2 )
            {
                status = GlobalState::InitializeApiImplEx2( GDKC_VERSION, GAMING_SERVICES_VERSION, 2, &initOptions );
            } else if ( GlobalState::InitializeApiImplEx )
            {
                status = GlobalState::InitializeApiImplEx( GDKC_VERSION, GAMING_SERVICES_VERSION, 2 );
            } else
            {
                status = GlobalState::InitializeApiImpl( GDKC_VERSION, GAMING_SERVICES_VERSION );
            }

            if ( SUCCEEDED( status ) )
            {
                if ( GlobalState::InitializeApiImplEx2 || !options )
                {
                    GlobalState::initialized = TRUE;
                } else
                {
                    LPCSTR errorMessage = "XGameRuntime is outdated and does not support these initialization options.";
                    if ( FAILED( status = GlobalState::UninitializeApiImpl() ) )
                        return status;
                    ERR("%s\n", errorMessage);
                    // This path throws E_GAMERUNTIME_NOT_INITIALIZED instead.
                    GlobalState::XErrorReport( E_GAMERUNTIME_NOT_INITIALIZED, errorMessage );
                    return E_GAMERUNTIME_NOT_INITIALIZED;
                }
            }
        }
    }

    return status;
}

HRESULT __stdcall
XGameRuntimeInitialize()
{
    return XGameRuntimeInitializeWithOptions( nullptr );
}

void __stdcall
XGameRuntimeUninitialize()
{
    if ( GlobalState::initialized && GlobalState::UninitializeApiImpl )
        GlobalState::UninitializeApiImpl();
}