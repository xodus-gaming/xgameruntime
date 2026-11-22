/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XSystem
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

namespace
{
    // On a Windows PC the console id is always all zeroes (39 bytes with the NUL).
    constexpr char kConsoleId[] = "00000000.00000000.00000000.00000000.00";
    constexpr INT32 kConsoleIdMinSize = 39;
    static_assert( sizeof( kConsoleId ) == kConsoleIdMinSize );

    // Wine has no sandbox concept, so report the retail one.
    constexpr char kSandboxId[] = "RETAIL";
    constexpr INT32 kSandboxIdMinSize = 16;

    /*
     * Copies an id to the caller. The buffer must hold at least minSize bytes
     * (the documented minimum, not just the id), otherwise nothing is written
     * and *used reports that minimum so the caller can size the buffer. A
     * short buffer is checked before a null pointer so the common size query
     * (size 0, null buffer) reports the size. Only idBytes (with the NUL) are
     * written, never the whole buffer.
     */
    HRESULT CopyId( const char *id, SIZE_T idBytes, INT32 minSize, INT32 size, char *buffer, SIZE_T *used )
    {
        if ( size < minSize )
        {
            if ( used ) *used = minSize;
            return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
        }

        if ( !buffer )
            return E_INVALIDARG;

        std::memcpy( buffer, id, idBytes );
        if ( used ) *used = idBytes;
        return S_OK;
    }
}

class XSystemImpl :
    public IXSystemImpl5
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        // IXSystemImpl2 and IXSystemImpl5 add no methods, and each interface's
        // vtable is a prefix of the next, so one object serves all of them.
        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXSystemImpl ) ||
             iid == __uuidof( IXSystemImpl2 ) ||
             iid == __uuidof( IXSystemImpl3 ) ||
             iid == __uuidof( IXSystemImpl4 ) ||
             iid == __uuidof( IXSystemImpl5 ) )
        {
            AddRef();
            *out = static_cast<IXSystemImpl5 *>(this);
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

    // Like XThreadingImpl, this is a static object that is never deleted.
    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );
        return curr;
    }

    HRESULT WINAPI XSystemGetConsoleId( INT32 consoleIdSize, char *consoleId, SIZE_T *consoleIdUsed ) override
    {
        TRACE( "consoleIdSize %d, consoleId %p, consoleIdUsed %p.\n", consoleIdSize, consoleId, consoleIdUsed );
        return CopyId( kConsoleId, sizeof( kConsoleId ), kConsoleIdMinSize, consoleIdSize, consoleId, consoleIdUsed );
    }

    HRESULT WINAPI XSystemGetXboxLiveSandboxId( INT32 sandboxIdSize, char *sandboxId, SIZE_T *sandboxIdUsed ) override
    {
        TRACE( "sandboxIdSize %d, sandboxId %p, sandboxIdUsed %p.\n", sandboxIdSize, sandboxId, sandboxIdUsed );
        return CopyId( kSandboxId, sizeof( kSandboxId ), kSandboxIdMinSize, sandboxIdSize, sandboxId, sandboxIdUsed );
    }

    // The real id is a 45 character string of a format that cannot be verified
    // here, and an invented one could change how a game identifies the device.
    // Games tolerate the failure, so it is reported rather than made up.
    HRESULT WINAPI XSystemGetAppSpecificDeviceId( INT32 appSpecificDeviceIdSize, char *appSpecificDeviceId, SIZE_T *appSpecificDeviceIdUsed ) override
    {
        FIXME( "appSpecificDeviceIdSize %d, appSpecificDeviceId %p, appSpecificDeviceIdUsed %p: not implemented, the id format is unverified.\n",
               appSpecificDeviceIdSize, appSpecificDeviceId, appSpecificDeviceIdUsed );
        return E_NOTIMPL;
    }

    // Nothing in the runtime creates tracked handles, so there is nothing to report.
    HRESULT WINAPI XSystemHandleTrack( XSystemHandleCallback callback, void *context ) override
    {
        FIXME( "callback %p, context %p: not implemented, no handles are tracked.\n", callback, context );
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XSystemIsHandleValid( XSystemHandle handle ) override
    {
        FIXME( "handle %p: not implemented, no handles are tracked.\n", handle );
        return FALSE;
    }

    // Wine has no download bandwidth policy to relax.
    void WINAPI XSystemAllowFullDownloadBandwidth( BOOLEAN enable ) override
    {
        TRACE( "enable %d: no-op.\n", enable );
    }

private:
    std::atomic_long ref{ 1 };
};

static XSystemImpl g_x_system;

IXSystemImpl5 *x_system_impl = static_cast<IXSystemImpl5 *>(&g_x_system);
