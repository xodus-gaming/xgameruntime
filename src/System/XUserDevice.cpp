/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUserDevice
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

class XUserDeviceImpl :
    public IXUserDeviceImpl2
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
             iid == __uuidof( IXUserDeviceImpl ) ||
             iid == __uuidof( IXUserDeviceImpl2 ) )
        {
            AddRef();
            *out = static_cast<IXUserDeviceImpl2 *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>(++ref);
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );
        return curr;
    }

    HRESULT WINAPI XUserFindForDevice( const APP_LOCAL_DEVICE_ID *deviceId, XUserHandle *handle ) override
    {
        FIXME( "deviceId %p, handle %p; no user can exist, returning E_GAMEUSER_USER_NOT_FOUND.\n", deviceId, handle );
        if (!deviceId || !handle) return E_INVALIDARG;
        *handle = nullptr;
        return E_GAMEUSER_USER_NOT_FOUND;
    }

    HRESULT WINAPI XUserRegisterForDeviceAssociationChanged( XTaskQueueHandle queue, void *context, XUserDeviceAssociationChangedCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        FIXME( "queue %p, context %p, callback %p, token %p; no device-to-user association, returning E_NOTIMPL.\n", queue, context, callback, token );
        if (token) token->token = 0;
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XUserUnregisterForDeviceAssociationChanged( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        FIXME( "token %llu, wait %u; no device-to-user association, returning FALSE.\n", token.token, wait );
        return FALSE;
    }

    HRESULT WINAPI XUserGetDefaultAudioEndpointUtf16( XUserLocalId user, XUserDefaultAudioEndpointKind defaultAudioEndpointKind, SIZE_T endpointIdUtf16Count, WCHAR endpointIdUtf16[], SIZE_T *endpointIdUtf16Used ) override
    {
        FIXME( "user %llu, defaultAudioEndpointKind %u, endpointIdUtf16Count %zu, endpointIdUtf16 %p, endpointIdUtf16Used %p; no per-user audio endpoints, returning E_NOTIMPL.\n", user.value, (UINT32)defaultAudioEndpointKind, endpointIdUtf16Count, endpointIdUtf16, endpointIdUtf16Used );
        if (endpointIdUtf16Used) *endpointIdUtf16Used = 0;
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserRegisterForDefaultAudioEndpointUtf16Changed( XTaskQueueHandle queue, void *context, XUserDefaultAudioEndpointUtf16ChangedCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        FIXME( "queue %p, context %p, callback %p, token %p; no per-user audio endpoints, returning E_NOTIMPL.\n", queue, context, callback, token );
        if (token) token->token = 0;
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XUserUnregisterForDefaultAudioEndpointUtf16Changed( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        FIXME( "token %llu, wait %u; no per-user audio endpoints, returning FALSE.\n", token.token, wait );
        return FALSE;
    }

    HRESULT WINAPI XUserFindControllerForUserWithUiAsync( XUserHandle user, XAsyncBlock *async ) override
    {
        FIXME( "user %p, async %p; no system UI, returning E_NOTIMPL.\n", user, async );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XUserFindControllerForUserWithUiResult( XAsyncBlock *async, APP_LOCAL_DEVICE_ID *deviceId ) override
    {
        FIXME( "async %p, deviceId %p; no system UI, returning E_NOTIMPL.\n", async, deviceId );
        if (deviceId) *deviceId = {};
        return E_NOTIMPL;
    }

private:
    std::atomic_long ref{ 1 };
};

static XUserDeviceImpl g_x_user_device;

IXUserDeviceImpl2 *x_user_device_impl = &g_x_user_device;
