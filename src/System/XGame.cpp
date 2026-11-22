/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGame
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
#include "../GameConfig.h"

#include <atomic>

class XGameImpl :
    public IXGameImpl3
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IXGameImpl ) ||
             iid == __uuidof( IXGameImpl2 ) ||
             iid == __uuidof( IXGameImpl3 ) )
        {
            AddRef();
            *out = static_cast<IXGameImpl3 *>( this );
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI
    AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>( ++ref );
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>( --ref );
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );
        return curr; /* static object: never deleted */
    }

    HRESULT WINAPI XGameGetXboxTitleId( UINT32 *titleId ) override
    {
        TRACE( "titleId %p\n", titleId );
        if ( !titleId ) return E_INVALIDARG;
        return GameConfig::GetTitleId( titleId );
    }

    void WINAPI XLaunchNewGame( const char *exePath, const char *args, XUserHandle defaultUser ) override
    {
        FIXME( "exePath %p, args %p, defaultUser %p stub!\n", exePath, args, defaultUser );
    }

    HRESULT WINAPI XLaunchRestartOnCrash( const char *args, UINT32 reserved ) override
    {
        FIXME( "args %p, reserved %u stub!\n", args, reserved );
        return E_NOTIMPL;
    }

private:
    std::atomic_long ref{ 1 };
};

static XGameImpl g_x_game;

IXGameImpl3 *x_game_impl = static_cast<IXGameImpl3 *>( &g_x_game );
