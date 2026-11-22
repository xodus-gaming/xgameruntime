/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameInvite
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
 * There is no Xbox Live social service behind this runtime, so no invite ever
 * arrives: registrations are tracked so they can be removed, callbacks are
 * never invoked, and there is never a pending invite to accept.
 */
class XGameInviteImpl :
    public IXGameInviteImpl2
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
             iid == __uuidof( IXGameInviteImpl ) ||
             iid == __uuidof( IXGameInviteImpl2 ) )
        {
            AddRef();
            *out = static_cast<IXGameInviteImpl2 *>(this);
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

    HRESULT WINAPI XGameInviteRegisterForEvent( XTaskQueueHandle queue, void *context, XGameInviteEventCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        TRACE( "queue %p, context %p, callback %p, token %p.\n", queue, context, callback, token );
        return Register( events, callback, token );
    }

    BOOLEAN WINAPI XGameInviteUnregisterForEvent( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        TRACE( "token %I64u, wait %d.\n", token.token, wait );
        return Unregister( events, token );
    }

    HRESULT WINAPI XGameInviteRegisterForPendingEvent( XTaskQueueHandle queue, void *context, XGameInviteEventCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        TRACE( "queue %p, context %p, callback %p, token %p.\n", queue, context, callback, token );
        return Register( pending, callback, token );
    }

    BOOLEAN WINAPI XGameInviteUnregisterForPendingEvent( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        TRACE( "token %I64u, wait %d.\n", token.token, wait );
        return Unregister( pending, token );
    }

    HRESULT WINAPI XGameInviteAcceptPendingInvite( const char *inviteUri ) override
    {
        TRACE( "inviteUri %p.\n", inviteUri );

        if ( !inviteUri || !*inviteUri )
            return E_INVALIDARG;

        // No invite ever becomes pending.
        return HRESULT_FROM_WIN32( ERROR_NOT_FOUND );
    }

private:
    HRESULT Register( std::set<UINT64> &set, XGameInviteEventCallback *callback, XTaskQueueRegistrationToken *token )
    {
        if ( !callback || !token )
            return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( mutex );
        UINT64 id = next++;
        set.insert( id );
        token->token = id;
        return S_OK;
    }

    BOOLEAN Unregister( std::set<UINT64> &set, XTaskQueueRegistrationToken token )
    {
        std::lock_guard<std::mutex> lock( mutex );
        return set.erase( token.token ) != 0;
    }

    std::atomic_long ref{ 1 };
    std::mutex mutex;
    std::set<UINT64> events;
    std::set<UINT64> pending;
    UINT64 next{ 1 };
};

static XGameInviteImpl g_x_game_invite;

IXGameInviteImpl2 *x_game_invite_impl = static_cast<IXGameInviteImpl2 *>(&g_x_game_invite);
