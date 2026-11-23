/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XError
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
#include <cstdio>
#include <cstring>

class XErrorImpl :
    public IXErrorImpl
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IXErrorImpl ) )
        {
            AddRef();
            *out = static_cast<IXErrorImpl *>(this);
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

        // Polymorphic classes should not be deleted.
        return curr;
    }

    void WINAPI XErrorReport( HRESULT hr, LPCSTR message ) override
    {
        XErrorCallback *cb;
        void *ctx;
        XErrorOptions opts;

        TRACE( "iface %p, hr %#lx, message %s.\n", this, hr, message ? message : "(null)" );

        AcquireSRWLockShared( &lock );
        cb = callback;
        ctx = context;
        opts = IsDebuggerPresent() ? optionsDebuggerPresent : optionsDebuggerNotPresent;
        ReleaseSRWLockShared( &lock );

        // A callback that returns non-zero has handled the error.
        if ( cb && cb( hr, message, ctx ) ) return;

        // OutputDebugStringOnError (1) and DebugBreakOnError (2) both write the debug output.
        // DebugBreakOnError and FailFastOnError are stored but not acted on.
        if ( !(static_cast<UINT32>( opts ) & 3) ) return;

        const char *text = message ? message : "";
        size_t len = strlen( text );
        char buf[256];

        if ( len < 0xC0 )
        {
            snprintf( buf, sizeof( buf ), "XERROR: HR:%08X threadID:%d %s\n",
                      static_cast<unsigned int>( hr ), static_cast<int>( GetCurrentThreadId() ), text );
            OutputDebugStringA( buf );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "XERROR: HR:%08X threadID:%d",
                      static_cast<unsigned int>( hr ), static_cast<int>( GetCurrentThreadId() ) );
            OutputDebugStringA( buf );
            OutputDebugStringA( text );
            OutputDebugStringA( "\n" );
        }
    }

    void WINAPI XErrorSetCallback( XErrorCallback *cb, void *ctx ) override
    {
        TRACE( "iface %p, callback %p, context %p.\n", this, cb, ctx );

        AcquireSRWLockExclusive( &lock );
        callback = cb;
        context = ctx;
        ReleaseSRWLockExclusive( &lock );
    }

    void WINAPI XErrorSetOptions( XErrorOptions present, XErrorOptions notPresent ) override
    {
        TRACE( "iface %p, present %#x, notPresent %#x.\n", this,
               static_cast<UINT32>( present ), static_cast<UINT32>( notPresent ) );

        AcquireSRWLockExclusive( &lock );
        optionsDebuggerPresent = present;
        optionsDebuggerNotPresent = notPresent;
        ReleaseSRWLockExclusive( &lock );
    }

private:
    std::atomic_long ref{ 1 };
    SRWLOCK lock = SRWLOCK_INIT;
    XErrorCallback *callback = nullptr;
    void *context = nullptr;
    XErrorOptions optionsDebuggerPresent = XErrorOptions::OutputDebugStringOnError;
    XErrorOptions optionsDebuggerNotPresent = XErrorOptions::None;
};

static XErrorImpl g_x_error;

IXErrorImpl *x_error_impl = static_cast<IXErrorImpl*>(&g_x_error);
