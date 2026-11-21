/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameRuntimeFeature
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

class XGameRuntimeFeatureImpl :
    public IXGameRuntimeFeatureImpl
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXGameRuntimeFeatureImpl ) )
        {
            AddRef();
            *out = static_cast<IXGameRuntimeFeatureImpl *>(this);
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

        // The object is static and must never be deleted.
        return curr;
    }

    BOOLEAN WINAPI XGameRuntimeIsFeatureAvailable( XGameRuntimeFeature feature ) override
    {
        // Every known feature group is reported as available. Revisit when the enum grows.
        static_assert( static_cast<UINT32>( XGameRuntimeFeature::XGameStreaming ) == 21 );

        TRACE( "feature %u.\n", static_cast<UINT32>( feature ) );
        return feature <= XGameRuntimeFeature::XGameStreaming;
    }

private:
    std::atomic_long ref{ 1 };
};

static XGameRuntimeFeatureImpl g_x_game_runtime_feature;

IXGameRuntimeFeatureImpl *x_game_runtime_feature = static_cast<IXGameRuntimeFeatureImpl*>(&g_x_game_runtime_feature);
