/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameEvent
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

namespace
{
    // Null-safe text for log lines.
    const char *Str( const char *s ) { return s ? s : "(null)"; }
}

class XGameEventImpl :
    public IXGameEventImpl
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IXGameEventImpl ) )
        {
            AddRef();
            *out = static_cast<IXGameEventImpl *>( this );
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>( ++ref );
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI Release() noexcept override
    {
        // Process-lifetime static: never deleted.
        ULONG curr = static_cast<ULONG>( --ref );
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );
        return curr;
    }

    /*
     * Validates the arguments, then DISCARDS the event: nothing is sent to
     * Xbox Live. The user handle is opaque and is only checked for null.
     */
    HRESULT WINAPI XGameEventWrite( XUserHandle user, const char *serviceConfigId, const char *playSessionId,
                                    const char *eventName, const char *dimensionsJson, const char *measurementsJson ) noexcept override
    {
        TRACE( "iface %p, user %p, serviceConfigId %s, playSessionId %s, eventName %s, dimensionsJson %s, measurementsJson %s.\n",
               this, user, Str( serviceConfigId ), Str( playSessionId ), Str( eventName ),
               Str( dimensionsJson ), Str( measurementsJson ) );

        if ( !user || !serviceConfigId || !playSessionId || !eventName ) return E_INVALIDARG;
        if ( !*serviceConfigId || !*eventName ) return E_INVALIDARG;

        if ( !warned.test_and_set() )
            FIXME( "events are validated and discarded, not sent to Xbox Live.\n" );

        return S_OK;
    }

private:
    std::atomic_long ref{ 1 };
    std::atomic_flag warned = ATOMIC_FLAG_INIT;
};

static XGameEventImpl g_x_game_event;

IXGameEventImpl *x_game_event_impl = static_cast<IXGameEventImpl *>( &g_x_game_event );
