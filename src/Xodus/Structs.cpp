/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> XodusIPCPacket
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

#include "Structs.hpp"

#include <winstring.h>

/**
 * XodusIPCPacket: Wraps IPC Packets sent to and received from Xodus.
 */
XodusIPCPacket::XodusIPCPacket(
    MagicHeaderType type,
    UINT16 messageType,
    ABI::Windows::Storage::Streams::IBuffer *message )
: Magic(type),
  Message_Type(messageType)
{
    Message = message;
    message->AddRef();
}

HRESULT WINAPI
XodusIPCPacket::QueryInterface( REFIID iid, void **out ) noexcept
{
    TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

    if (!out) return E_POINTER;
    *out = nullptr;

    if ( iid == __uuidof( IUnknown ) ||
         iid == __uuidof( IInspectable ) ||
         iid == __uuidof( IXodusIPCPacket ) )
    {
        AddRef();
        *out = static_cast<IXodusIPCPacket *>(this);
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI
XodusIPCPacket::AddRef() noexcept
{
    ULONG curr = static_cast<ULONG>(++ref);
    TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
    return curr;
}

ULONG WINAPI
XodusIPCPacket::Release() noexcept
{
    ULONG curr = static_cast<ULONG>(--ref);
    TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

    if ( !curr )
    {
        Message->Release();
        delete this;
    }

    return curr;
}

HRESULT WINAPI
XodusIPCPacket::GetIids( ULONG *iidCount, IID **iids )
{
    FIXME("iface %p, iidCount %p, iids %p stub!\n", this, iidCount, iids);
    return E_NOTIMPL;
}

HRESULT WINAPI
XodusIPCPacket::GetRuntimeClassName( HSTRING *className )
{
    FIXME("iface %p, className %p stub!\n", this, className);
    return E_NOTIMPL;
}

HRESULT WINAPI
XodusIPCPacket::GetTrustLevel( TrustLevel *trustLevel )
{
    FIXME("iface %p, trustLevel %p stub!\n", this, trustLevel);
    return E_NOTIMPL;
}


HRESULT WINAPI
XodusIPCPacket::get_Magic( MagicHeaderType *out )
{
    TRACE("iface %p, out %p.\n", this, out);
    *out = Magic;
    return S_OK;
}

HRESULT WINAPI
XodusIPCPacket::get_MessageType( UINT16 *out )
{
    TRACE("iface %p, out %p.\n", this, out);
    *out = Message_Type;
    return S_OK;
}

HRESULT WINAPI
XodusIPCPacket::get_Message( ABI::Windows::Storage::Streams::IBuffer **out )
{
    TRACE("iface %p, out %p.\n", this, out);
    *out = Message;
    Message->AddRef();
    return S_OK;
}

class XodusIPCPacketImpl
    : public IActivationFactory
    , public IXodusIPCPacketStatics
{
public:
    virtual ~XodusIPCPacketImpl() = default;

    HRESULT WINAPI
    QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IActivationFactory ) )
        {
            AddRef();
            *out = static_cast<IActivationFactory *>(this);
            return S_OK;
        }

        if ( iid == __uuidof( IXodusIPCPacketStatics ) )
        {
            AddRef();
            *out = static_cast<IXodusIPCPacketStatics *>(this);
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        *out = nullptr;
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
        /*
        if ( !curr )
            delete this;
        */

        return curr;
    }

    HRESULT WINAPI
    GetIids( ULONG *iid_count, IID **iids ) noexcept override
    {
        TRACE( "iface %p, iid_count %p, iids %p\n", this, iid_count, iids );

        if ( !iid_count || !iids )
            return E_POINTER;

        *iid_count = 2;
        IID* allocated = static_cast<IID*>( CoTaskMemAlloc( sizeof(IID) * (*iid_count) ) );

        if ( !allocated )
            return E_OUTOFMEMORY;

        allocated[0] = __uuidof( IActivationFactory );
        allocated[1] = __uuidof( IXodusIPCPacketStatics );

        *iids = allocated;
        return S_OK;
    }

    HRESULT WINAPI
    GetRuntimeClassName( HSTRING *class_name ) noexcept override
    {
        TRACE( "iface %p, class_name %p\n", this, class_name );
        return WindowsCreateString( (LPCWSTR)L"Xodus.XodusIPCPacket", 30, class_name );
    }

    HRESULT WINAPI
    GetTrustLevel( TrustLevel *trust_level ) noexcept override
    {
        FIXME( "iface %p, trust_level %p stub!\n", this, trust_level );
        return E_NOTIMPL;
    }

    HRESULT WINAPI
    ActivateInstance( IInspectable **instance ) noexcept override
    {
        ERR( "iface %p, This factory is not activatable!\n", this );
        return E_NOTIMPL;
    }

    HRESULT WINAPI
    ConstructXodusIPCPacket( ABI::Xodus::MagicHeaderType magic, UINT16 messageType, ABI::Windows::Storage::Streams::IBuffer* buffer, ABI::Xodus::IXodusIPCPacket** out ) noexcept override
    {
        // AddRef is done in constructor.
        *out = new XodusIPCPacket( magic, messageType, buffer );
        return S_OK;
    }

private:
    std::atomic_long ref{ 1 };
};

static XodusIPCPacketImpl g_xodus_ipc_packet_statics;

IActivationFactory* xodus_ipc_packet_factory =
    static_cast<IActivationFactory*>(&g_xodus_ipc_packet_statics);

/**
 * IPCResponseHandler: Handler interface for IPC Response events.
 */
IPCResponseHandler::IPCResponseHandler(
    IPCResponseHandlerCallback callback,
    PVOID context )
:   m_callback(callback),
    m_context(context)
{
}

HRESULT WINAPI
IPCResponseHandler::QueryInterface( REFIID iid, void **out ) noexcept
{
    TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

    if (!out) return E_POINTER;
    *out = nullptr;

    if ( iid == __uuidof( IUnknown ) ||
         iid == __uuidof( IInspectable ) ||
         iid == __uuidof( IIPCResponseHandler ) )
    {
        AddRef();
        *out = static_cast<IIPCResponseHandler *>(this);
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI
IPCResponseHandler::AddRef() noexcept
{
    ULONG curr = static_cast<ULONG>(++ref);
    TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
    return curr;
}

ULONG WINAPI
IPCResponseHandler::Release() noexcept
{
    ULONG curr = static_cast<ULONG>(--ref);
    TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

    if ( !curr )
    {
        delete this;
    }

    return curr;
}

HRESULT WINAPI
IPCResponseHandler::Invoke( IXodusIPCPacket *response )
{
    TRACE("iface %p, response %p\n", this, response);
    return m_callback( m_context, response );
}


/**
 * MsaTokenResponse: Wraps Msa Token response packets sent by Xodus
 */
MsaTokenResponse::MsaTokenResponse(
    HSTRING token,
    ABI::Windows::Foundation::DateTime expiry )
: Expiry(expiry)
{
    WindowsDuplicateString( token, &Token );
}

HRESULT WINAPI
MsaTokenResponse::QueryInterface( REFIID iid, void **out ) noexcept
{
    TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

    if (!out) return E_POINTER;
    *out = nullptr;

    if ( iid == __uuidof( IUnknown ) ||
         iid == __uuidof( IInspectable ) ||
         iid == __uuidof( IMsaTokenResponse ) )
    {
        AddRef();
        *out = static_cast<IMsaTokenResponse *>(this);
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG WINAPI
MsaTokenResponse::AddRef() noexcept
{
    ULONG curr = static_cast<ULONG>(++ref);
    TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
    return curr;
}

ULONG WINAPI
MsaTokenResponse::Release() noexcept
{
    ULONG curr = static_cast<ULONG>(--ref);
    TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

    if ( !curr )
    {
        WindowsDeleteString( Token );
        delete this;
    }

    return curr;
}

HRESULT WINAPI
MsaTokenResponse::GetIids( ULONG *iidCount, IID **iids )
{
    FIXME("iface %p, iidCount %p, iids %p stub!\n", this, iidCount, iids);
    return E_NOTIMPL;
}

HRESULT WINAPI
MsaTokenResponse::GetRuntimeClassName( HSTRING *className )
{
    FIXME("iface %p, className %p stub!\n", this, className);
    return E_NOTIMPL;
}

HRESULT WINAPI
MsaTokenResponse::GetTrustLevel( TrustLevel *trustLevel )
{
    FIXME("iface %p, trustLevel %p stub!\n", this, trustLevel);
    return E_NOTIMPL;
}

HRESULT WINAPI
MsaTokenResponse::get_Token( HSTRING *out )
{
    TRACE( "iface %p, out %p\n", this, out );
    WindowsDuplicateString( Token, out );
    return S_OK;
}

HRESULT WINAPI
MsaTokenResponse::get_Expiry( ABI::Windows::Foundation::DateTime *out )
{
    TRACE( "iface %p, out %p\n", this, out );
    *out = Expiry;
    return S_OK;
}