/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XSystemAnalytics
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
#include "XSystemAnalytics.h"

#include <atomic>
#include <cwchar>
#include <roapi.h>
#include <winstring.h>

using namespace ABI::Windows::System::Profile;

namespace
{
    // Owns an HSTRING and deletes it.
    class HStringHolder
    {
    public:
        HStringHolder() = default;
        HStringHolder( const HStringHolder & ) = delete;
        HStringHolder &operator=( const HStringHolder & ) = delete;
        ~HStringHolder() { WindowsDeleteString( string ); }

        HSTRING *put() { return &string; }
        HSTRING get() const { return string; }

    private:
        HSTRING string = nullptr;
    };

    // Releases a COM interface.
    template <typename T>
    class ComHolder
    {
    public:
        ComHolder() = default;
        ComHolder( const ComHolder & ) = delete;
        ComHolder &operator=( const ComHolder & ) = delete;
        ~ComHolder() { if ( ptr ) ptr->Release(); }

        T **put() { return &ptr; }
        T *operator->() const { return ptr; }

    private:
        T *ptr = nullptr;
    };

    // Borrows the handle: the holder keeps ownership.
    HRESULT Utf8FromHString( const HStringHolder &string, std::string &out )
    {
        UINT32 length = 0;
        const WCHAR *wide = WindowsGetStringRawBuffer( string.get(), &length );

        out.clear();
        if ( !length )
            return S_OK;

        int bytes = WideCharToMultiByte( CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr );
        if ( bytes <= 0 )
        {
            DWORD error = GetLastError();
            return error ? HRESULT_FROM_WIN32( error ) : E_FAIL;
        }

        out.resize( bytes );
        if ( !WideCharToMultiByte( CP_UTF8, 0, wide, length, out.data(), bytes, nullptr, nullptr ) )
        {
            DWORD error = GetLastError();
            return error ? HRESULT_FROM_WIN32( error ) : E_FAIL;
        }

        return S_OK;
    }

    HRESULT ReadDeviceFamily( std::string &family, std::string &version )
    {
        HStringHolder className;
        ComHolder<IAnalyticsInfoStatics> statics;
        ComHolder<IAnalyticsVersionInfo> versionInfo;
        HStringHolder deviceFamily;
        HStringHolder deviceFamilyVersion;
        HRESULT hr;

        hr = WindowsCreateString( RuntimeClass_Windows_System_Profile_AnalyticsInfo,
                                  static_cast<UINT32>( wcslen( RuntimeClass_Windows_System_Profile_AnalyticsInfo ) ),
                                  className.put() );
        if ( FAILED( hr ) ) { ERR( "WindowsCreateString failed, hr %#lx.\n", hr ); return hr; }

        hr = RoGetActivationFactory( className.get(), __uuidof( IAnalyticsInfoStatics ),
                                     reinterpret_cast<void **>( statics.put() ) );
        if ( FAILED( hr ) ) { ERR( "RoGetActivationFactory for AnalyticsInfo failed, hr %#lx.\n", hr ); return hr; }

        hr = statics->get_VersionInfo( versionInfo.put() );
        if ( FAILED( hr ) ) { ERR( "AnalyticsInfo::VersionInfo failed, hr %#lx.\n", hr ); return hr; }

        hr = versionInfo->get_DeviceFamily( deviceFamily.put() );
        if ( FAILED( hr ) ) { ERR( "DeviceFamily failed, hr %#lx.\n", hr ); return hr; }

        hr = versionInfo->get_DeviceFamilyVersion( deviceFamilyVersion.put() );
        if ( FAILED( hr ) ) { ERR( "DeviceFamilyVersion failed, hr %#lx.\n", hr ); return hr; }

        hr = Utf8FromHString( deviceFamily, family );
        if ( FAILED( hr ) ) { ERR( "Converting DeviceFamily failed, hr %#lx.\n", hr ); return hr; }

        hr = Utf8FromHString( deviceFamilyVersion, version );
        if ( FAILED( hr ) ) { ERR( "Converting DeviceFamilyVersion failed, hr %#lx.\n", hr ); return hr; }

        return S_OK;
    }

    /*
     * The game's thread may or may not have a COM apartment. RoInitialize makes
     * sure it has one and is undone afterwards; a thread already in another
     * apartment type (RPC_E_CHANGED_MODE) is left alone.
     */
    HRESULT ReadDeviceFamilyInApartment( std::string &family, std::string &version )
    {
        HRESULT init = RoInitialize( RO_INIT_MULTITHREADED );
        if ( FAILED( init ) && init != RPC_E_CHANGED_MODE )
        {
            ERR( "RoInitialize failed, hr %#lx.\n", init );
            return init;
        }

        HRESULT hr = ReadDeviceFamily( family, version );

        if ( SUCCEEDED( init ) )
            RoUninitialize();

        return hr;
    }
}

class XSystemAnalyticsImpl :
    public IXSystemAnalyticsImpl
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
             iid == __uuidof( IXSystemAnalyticsImpl ) )
        {
            AddRef();
            *out = static_cast<IXSystemAnalyticsImpl *>(this);
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

    // The struct is returned by value, so a caller cannot see an error code:
    // on failure it gets an all zero struct and the cause is logged.
    XSystemAnalyticsInfo * WINAPI XSystemGetAnalyticsInfo( XSystemAnalyticsInfo *__ret ) override
    {
        TRACE( "iface %p, __ret %p.\n", this, __ret );

        HRESULT hr = FillAnalyticsInfo( ReadDeviceFamilyInApartment, *__ret );
        if ( FAILED( hr ) )
            ERR( "Reading the analytics info failed, hr %#lx, returning a zero struct.\n", hr );

        return __ret;
    }

private:
    std::atomic_long ref{ 1 };
};

static XSystemAnalyticsImpl g_x_system_analytics;

IXSystemAnalyticsImpl *x_system_analytics = static_cast<IXSystemAnalyticsImpl *>(&g_x_system_analytics);
