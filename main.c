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
#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xgameruntime);

DWORD tlsIndex;

/* Report exceptions before the title gets to decide what they mean.
 *
 * A title's own handler catches a fault and turns it into whatever message it
 * feels like -- Deep Rock Galactic reduces one to "Fatal error!" with no
 * detail, and writes no log, which leaves nothing to work from. A vectored
 * handler runs ahead of every frame-based one, so the original exception is
 * still intact here: its code, where it came from, and for an access violation
 * the address that could not be touched.
 *
 * Off unless XODUS_TRACE_EXCEPTIONS is set. This reports and declines to
 * handle, so nothing about the title's own behaviour changes -- but a handler
 * on the exception path is not free, and it must not be in the way when nobody
 * is reading it.
 */
static LONG CALLBACK trace_exception( EXCEPTION_POINTERS *info )
{
    const EXCEPTION_RECORD *rec = info->ExceptionRecord;

    switch (rec->ExceptionCode)
    {
    /* Ordinary traffic, not faults: C++ throws, a thread naming itself, and
     * the breakpoints a debugger-aware title plants in its own code. */
    case 0xe06d7363:
    case 0x406d1388:
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
    {
        void *addr = (void *)rec->ExceptionInformation[1];
        MEMORY_BASIC_INFORMATION mbi = { 0 };
        const char *how;

        /* 0 read, 1 write, 8 execute -- and the last is the one worth naming,
         * because it means the code jumped somewhere it was not allowed to run
         * rather than touched something it should not have. */
        switch (rec->ExceptionInformation[0])
        {
        case 0:  how = "read of"; break;
        case 1:  how = "write to"; break;
        case 8:  how = "execute at"; break;
        default: how = "access to"; break;
        }

        /* Faulting on the first byte of a thunk means the call that jumped
         * here has just pushed its return address, so who did it is sitting at
         * the top of the stack. */
        if (rec->ExceptionInformation[0] == 8 && (void *)info->ContextRecord->Rip == addr)
        {
            const void **sp = (const void **)info->ContextRecord->Rsp;
            MEMORY_BASIC_INFORMATION caller = { 0 };

            if (VirtualQuery( sp, &caller, sizeof(caller) ) && caller.State == MEM_COMMIT)
            {
                VirtualQuery( *sp, &caller, sizeof(caller) );
                TRACE( "EXC jumped here from %p (in a region based at %p, type %#lx)\n",
                     *sp, caller.AllocationBase, caller.Type );
            }
        }

        if (VirtualQuery( addr, &mbi, sizeof(mbi) ))
            TRACE( "EXC %#lx at %p: %s %p -- page state %#lx protect %#lx type %#lx (rip %p)\n",
                 rec->ExceptionCode, rec->ExceptionAddress, how, addr,
                 mbi.State, mbi.Protect, mbi.Type, (void *)info->ContextRecord->Rip );
        else
            TRACE( "EXC %#lx at %p: %s %p -- not mapped (rip %p)\n", rec->ExceptionCode,
                 rec->ExceptionAddress, how, addr, (void *)info->ContextRecord->Rip );
    }
    else
        TRACE( "EXC %#lx at %p (rip %p, rsp %p)\n", rec->ExceptionCode, rec->ExceptionAddress,
             (void *)info->ContextRecord->Rip, (void *)info->ContextRecord->Rsp );

    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, void *reserved )
{
    TRACE( "hinst %p, reason %lu, reserved %p.\n", hinst, reason, reserved );

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            if ((tlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES) return FALSE;
            /* The unix side is only needed to reach xodus-service, so a failure
             * here must not take the whole DLL down: everything except the
             * account calls works without it. */
            if (__wine_init_unix_call()) WARN( "no unix library; xodus-service is unreachable.\n" );
            if (getenv( "XODUS_TRACE_EXCEPTIONS" )) AddVectoredExceptionHandler( TRUE, trace_exception );
        case DLL_THREAD_ATTACH:
            TlsSetValue( tlsIndex, FALSE );
            break;
        case DLL_PROCESS_DETACH:
            TlsFree( tlsIndex );
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

/* Bring up the process's WinRT apartment.
 *
 * Initializing the runtime is what gives a title an apartment to activate WinRT
 * classes in; titles rely on that rather than calling RoInitialize themselves.
 * Without it combase has no MTA to join and refuses the activation:
 * Deep Rock Galactic asks for Windows.System.Profile.AnalyticsInfo, gets
 * CO_E_NOTINITIALIZED out of ensure_mta, and puts up its own crash dialog with
 * no message in it. Expedition 33 activates the same class sixty-two times
 * without trouble because it initializes COM itself.
 *
 * Done once and never undone. The apartment is process-wide and outlives any
 * one initialize/uninitialize pair, and other threads may be inside a call --
 * the same reasoning that keeps UninitializeApiImpl from tearing anything down.
 */
static void ensure_winrt_apartment(void)
{
    static LONG initialized;
    static CO_MTA_USAGE_COOKIE mta_cookie;
    HRESULT hr;

    if (InterlockedExchange( &initialized, 1 )) return;

    /* CoIncrementMTAUsage, not RoInitialize: this must not put the calling
     * thread into an apartment of our choosing. RoInitialize does, and that
     * broke Expedition 33 -- a title that initializes COM itself found the
     * decision already made and stopped before opening a window. Incrementing
     * the usage count keeps a process-wide MTA alive for whoever needs one and
     * leaves every thread's own apartment to the title. */
    hr = CoIncrementMTAUsage( &mta_cookie );
    if (FAILED(hr)) WARN( "could not keep a process MTA alive: %#lx.\n", hr );
}

HRESULT WINAPI InitializeApiImplEx2( ULONG gdkVer, ULONG gsVer, char mode, const struct initialize_options *options )
{
    TRACE( "gdkVer %ld, gsVer %ld, mode %d, options %p.\n", gdkVer, gsVer, mode, options );
    ensure_winrt_apartment();
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

/*
 * XGameRuntimeUninitialize.
 *
 * A spec-level stub is not a harmless placeholder here: calling one raises
 * EXCEPTION_WINE_STUB and *aborts the process*. Beast of Reincarnation calls
 * this while still running -- it had a window up and a swapchain presenting --
 * and died on the spot.
 *
 * There is nothing this has to tear down. The process task queue and the
 * signed-in user are process-wide and outlive any one initialize/uninitialize
 * pair, and other threads may still be inside a call; releasing them here would
 * trade an abort for a use-after-free.
 */
HRESULT WINAPI UninitializeApiImpl(void)
{
    TRACE( "()\n" );
    return S_OK;
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
