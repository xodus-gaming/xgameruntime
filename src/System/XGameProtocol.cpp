/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameProtocol
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
#include <mutex>
#include <set>

/*
 * Hands out tokens for protocol activation callbacks. Stock Wine has no
 * protocol activation source, so a registered callback is never invoked, which
 * matches Windows when the game is not started from a protocol URI. Only the
 * live tokens are tracked, since there is nothing to dispatch to.
 */
class XGameProtocolImpl :
    public IXGameProtocolImpl
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXGameProtocolImpl ) )
        {
            AddRef();
            *out = static_cast<IXGameProtocolImpl *>(this);
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

    HRESULT WINAPI XGameProtocolRegisterForActivation( XTaskQueueHandle queue, void *context, XGameProtocolActivationCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        TRACE( "queue %p, context %p, callback %p, token %p.\n", queue, context, callback, token );

        if ( !callback || !token ) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( mutex );
        UINT64 value = next_token++;
        registrations.insert( value );
        token->token = value;
        return S_OK;
    }

    BOOLEAN WINAPI XGameProtocolUnregisterForActivation( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        TRACE( "token %I64u, wait %d.\n", token.token, wait );

        std::lock_guard<std::mutex> lock( mutex );
        return registrations.erase( token.token ) != 0;
    }

private:
    std::atomic_long ref{ 1 };
    std::mutex mutex;
    std::set<UINT64> registrations;
    UINT64 next_token = 1;
};

static XGameProtocolImpl g_x_game_protocol;

IXGameProtocolImpl *x_game_protocol_impl = static_cast<IXGameProtocolImpl *>(&g_x_game_protocol);
