/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> IPCLayer
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

#include "../private.h"

#include <private/list.h>
#include <private/os.h>
#include <private/env.h>

#include "Structs.hpp"

#include <private/IXAsync.hpp>

#include <ntstatus.h>
#include <winstring.h>
#include <afunix.h>
#include <atomic>

using namespace ABI;
using namespace ABI::Xodus;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Storage::Streams;
using namespace ::Windows::Storage::Streams;

static int sockfd = 0;

LPSTR NormalizeUnixPathToWine( LPCSTR unix_path )
{
    LPSTR wine_path;
    SIZE_T len;
    SIZE_T iter;

    if ( !unix_path || unix_path[0] != '/' )
        return NULL;

    len = lstrlenA( unix_path );

    wine_path = (LPSTR)CoTaskMemAlloc( len + 1 );
    if ( !wine_path )
        return NULL;

    wine_path[0] = 'Z';
    wine_path[1] = ':';

    for ( iter = 0; iter < len; ++iter )
    {
        wine_path[iter + 2] = (unix_path[iter] == '/') ? '\\' : unix_path[iter];
    }

    wine_path[len + 2] = '\0';
    return wine_path;
}

class ABI::Xodus::IPCLayer :
    public IIPCLayer
{
public:
    /* IUnknown Methods */
    HRESULT WINAPI
    QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IIPCLayer ) )
        {
            AddRef();
            *out = static_cast<IIPCLayer *>(this);
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

    /* IInspectable Methods */
    HRESULT WINAPI
    GetIids( ULONG *iidCount, IID **iids ) override
    {
        FIXME("iface %p, iidCount %p, iids %p stub!\n", this, iidCount, iids);
        return E_NOTIMPL;
    }

    HRESULT WINAPI
    GetRuntimeClassName( HSTRING *className ) override
    {
        FIXME("iface %p, className %p stub!\n", this, className);
        return E_NOTIMPL;
    }

    HRESULT WINAPI
    GetTrustLevel( TrustLevel *trustLevel ) override
    {
        FIXME("iface %p, trustLevel %p stub!\n", this, trustLevel);
        return E_NOTIMPL;
    }

    /* IIPCLayer Methods */
    HRESULT WINAPI
    InitializeSocket() override
    {
        HRESULT hr = S_OK;
        TRACE("\n");
        return XAsync<IUnknown *>::Create( static_cast<IUnknown *>(this), nullptr, nullptr, InitializeSocketThread, nullptr );
    }

    HRESULT WINAPI
    SendRequestAsync( IXodusIPCPacket *packet, IXAsync<IXodusIPCPacket *> **operation ) override
    {
        HRESULT hr;

        TRACE("packet %p, operation %p.\n", packet, operation );

        packet->AddRef();
        return XAsync<IXodusIPCPacket *>::Create( static_cast<IUnknown *>(this), packet, nullptr, SendRequest, operation );
    }

    HRESULT WINAPI
    add_ResponseReceived( IIPCResponseHandler *handler, EventRegistrationToken *token ) override
    {
        response_received_callback *newCallback = new response_received_callback();
        newCallback->handler = handler;

        TRACE("handler %p, token %p.\n", handler, token);

        handler->AddRef();
        token->value = m_NextEventToken++;
        newCallback->token = token->value;
        list_add_head( &m_Callbacks, &newCallback->entry );

        return S_OK;
    }

    HRESULT WINAPI
    remove_ResponseReceived( EventRegistrationToken token ) override
    {
        response_received_callback *oldCallback;

        TRACE("token %lld.\n", token.value);

        LIST_FOR_EACH_ENTRY( oldCallback, &m_Callbacks, response_received_callback, entry )
        {
            if ( oldCallback->token == token.value )
            {
                oldCallback->handler->Release();
                list_remove( &oldCallback->entry );
                return S_OK;
            }
        }

        return E_BOUNDS;
    }

private:
    struct IPCHeader_CTYPE
    {
        MagicHeaderType Magic;
        UINT16 Message_Type;
        UINT16 MessageLength;
    };

    struct IPCFrame
    {
        UINT32 frameSize;
        BYTE* frame;
    };

    struct SendRequestContext
    {
        HANDLE event;
        IXodusIPCPacket *response;
    };

    typedef struct _POLL_SOCKET_ARGS
    {
        BYTE curr_buffer[POLL_BUFFER_SIZE];
        SIZE_T curr_buffer_size;
    } POLL_SOCKET_ARGS;

    // Native IPC Socket Implementation
    static NTSTATUS
    ConnectSocket( LPCSTR socket_suffix )
    {
        WSADATA wsa;
        struct sockaddr_un addr{};
        LPSTR normalized;
        CHAR runtime[MAX_ENV_BUFFER];

        TRACE( "socket_suffix %s\n", socket_suffix );

        switch ( xgameruntime_get_os() )
        {
            case RunningOS::OS_Linux:
                if ( FAILED( xgameruntime_get_env( "XDG_RUNTIME_DIR", runtime, sizeof( runtime ) ) ) )
                    throw new Exception( E_NOT_VALID_STATE, "Detected running under Wine on Linux, but XDG_RUNTIME_DIR was not defined!" );
                normalized = NormalizeUnixPathToWine( runtime );
                lstrcpyA( runtime, normalized );
                CoTaskMemFree( normalized );
                break;

            case RunningOS::OS_Windows:
                throw new Exception( E_NOTIMPL, "Windows support is not implemented!" );
                break;

            case RunningOS::OS_Darwin:
                lstrcpyA( runtime, "/tmp" );
                normalized = NormalizeUnixPathToWine( runtime );
                lstrcpyA( runtime, normalized );
                CoTaskMemFree( normalized );
                break;

            case RunningOS::OS_Other:
                throw new Exception( E_NOTIMPL, "OS or compatibility layer is not supported!" );
        }

        WSAStartup( MAKEWORD( 2, 2 ), &wsa );

        size_t len = strlen( runtime ) + strlen( socket_suffix ) + 1;
        LPSTR socket_path = (LPSTR)malloc( len );

        if ( !socket_path )
            return STATUS_NO_MEMORY;

        snprintf( socket_path, len + 1, "%s\\%s", runtime, socket_suffix );

        sockfd = socket( AF_UNIX, SOCK_STREAM, 0 );
        if ( sockfd == INVALID_SOCKET )
            return STATUS_ABANDONED;

        memset( &addr, 0, sizeof(addr) );
        addr.sun_family = AF_UNIX;
        lstrcpynA( addr.sun_path, socket_path, sizeof(addr.sun_path) - 1 );

        if ( connect( sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr) ) < 0 )
        {
            TRACE( "failed to load socket %s\n", socket_path );
            TRACE( "socket connection failed with %d\n", errno );
            return STATUS_CONNECTION_REFUSED;
        }

        return STATUS_SUCCESS;
    }

    static NTSTATUS
    PollSocket( POLL_SOCKET_ARGS *socket_args )
    {
        struct pollfd fds[1];
        int ret;
        ssize_t n;

        TRACE( "socket_args %p\n", socket_args );

        if ( !sockfd )
            return STATUS_CONNECTION_INVALID;

        fds[0].fd = sockfd;
        fds[0].events = POLLRDNORM;

        do {
            ret = WSAPoll( fds, 1, -1 );
        } while ( ret == SOCKET_ERROR && WSAGetLastError() == WSAEINTR );

        if ( ret == SOCKET_ERROR )
            return STATUS_CONNECTION_DISCONNECTED;

        if ( fds[0].revents & (POLLERR | POLLHUP | POLLNVAL) )
            return STATUS_CONNECTION_DISCONNECTED;

        if ( fds[0].revents & POLLRDNORM )
        {
            n = recv( sockfd, reinterpret_cast<char *>(socket_args->curr_buffer + socket_args->curr_buffer_size), static_cast<int>(POLL_BUFFER_SIZE - socket_args->curr_buffer_size), 0 );

            if ( n == 0 )
                return STATUS_CONNECTION_DISCONNECTED;

            if ( n == SOCKET_ERROR )
                return STATUS_CONNECTION_DISCONNECTED;

            socket_args->curr_buffer_size += n;
        }

        return STATUS_SUCCESS;
    }

    static NTSTATUS
    SendFrame( IPCFrame *frame )
    {
        SIZE_T sent = 0;
        SIZE_T n;
        SIZE_T remaining;

        TRACE( "frame %p\n", frame );

        UINT32 magic = *reinterpret_cast<UINT32 *>(frame->frame);
        UINT16 type  = *reinterpret_cast<UINT16 *>(frame->frame + sizeof(UINT32));
        UINT16 len   = *reinterpret_cast<UINT16 *>(frame->frame + sizeof(UINT32) + sizeof(UINT16));
        BYTE* body  = frame->frame + 8;

        TRACE("magic is %#x\n", magic);
        TRACE("type is %d\n", type);
        TRACE("len is %d\n", len);
        TRACE("body is %s\n", body);

        while ( sent < frame->frameSize )
        {
            remaining = static_cast<int>(frame->frameSize - sent);
            n = send( sockfd, reinterpret_cast<const char *>(frame->frame) + sent, remaining, 0 );

            if ( n == SOCKET_ERROR )
            {
                int error = WSAGetLastError();

                if (error == WSAEINTR)
                    continue;

                return STATUS_CONNECTION_RESET;
            }

            if ( n == 0 )
                return STATUS_CONNECTION_RESET;

            sent += n;
        }

        return STATUS_SUCCESS;
    }

    static HRESULT WINAPI
    SendRequest( IUnknown *invoker, PVOID param, PROPVARIANT *result )
    {
        auto iface = dynamic_cast<IPCLayer *>( invoker );
        auto packet = static_cast<IXodusIPCPacket *>( param );

        BYTE* messageBuffer;
        DWORD asyncres;
        HRESULT status = S_OK;
        NTSTATUS nts;
        IPCFrame frame{};
        IPCHeader_CTYPE header{};
        EventRegistrationToken token{};
        SendRequestContext context{ .event = CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        IPCResponseHandler *handler = new IPCResponseHandler( SendRequestResponseHandler, (PVOID)&context );

        IBuffer *message;
        IBufferByteAccess *messageBufferByteAccess;

        TRACE("invoker %p, param %p, result %p\n", invoker, param, result);

        packet->get_Magic( &header.Magic );
        packet->get_MessageType( &header.Message_Type );
        packet->get_Message( &message );
        packet->Release();

        status = message->get_Length( &frame.frameSize );
        if ( FAILED( status ) ) return status;
        status = message->QueryInterface<IBufferByteAccess>( &messageBufferByteAccess );
        message->Release();
        if ( FAILED( status ) ) return status;
        status = messageBufferByteAccess->Buffer( &messageBuffer );
        messageBufferByteAccess->Release();
        if ( FAILED( status ) ) return status;

        header.MessageLength = frame.frameSize;

        frame.frameSize += sizeof(IPCHeader_CTYPE);

        frame.frame = (PBYTE)CoTaskMemAlloc( sizeof(BYTE) * frame.frameSize );
        if ( !frame.frame )
            return E_OUTOFMEMORY;

        RtlCopyMemory( frame.frame, &header.Magic, sizeof(MagicHeaderType) );
        RtlCopyMemory( frame.frame + sizeof(MagicHeaderType), &header.Message_Type, sizeof(UINT16) );
        RtlCopyMemory( frame.frame + sizeof(MagicHeaderType) + sizeof(UINT16), &header.MessageLength, sizeof(UINT16) );
        RtlCopyMemory( frame.frame + sizeof(IPCHeader_CTYPE), messageBuffer, header.MessageLength );

        status = iface->add_ResponseReceived( handler, &token );
        if ( FAILED( status ) ) return status;

        nts = SendFrame( &frame );
        CoTaskMemFree( frame.frame );
        if ( FAILED( nts ) ) return HRESULT_FROM_NT( nts );

        asyncres = WaitForSingleObject( context.event, IPC_REQUEST_TIMEOUT_MS );
        status = iface->remove_ResponseReceived( token );
        if ( FAILED( status ) ) return status;
        if ( asyncres )
        {
            WARN("Timeout while waiting for %p to respond.\n", handler);
            return HRESULT_FROM_NT( STATUS_TIMEOUT );
        }

        result->vt = VT_UNKNOWN;
        result->punkVal = context.response;

        return S_OK;
    }

    static HRESULT WINAPI
    SendRequestResponseHandler( PVOID context, IXodusIPCPacket *packet )
    {
        auto ctx = static_cast<SendRequestContext *>( context );

        HRESULT status;
        UINT16 messageType;

        TRACE("context %p, packet %p\n", context, packet);

        status = packet->get_MessageType( &messageType );
        if ( FAILED( status ) ) return status;

        if ( messageType == 1 /* PING */ )
            return S_OK; //Skip the packet we sent.

        if ( messageType == 2 /* PONG */ )
            TRACE("Got PONGED!\n");

        ctx->response = packet;
        SetEvent( ctx->event );
        return S_OK;
    }

    static HRESULT WINAPI
    InitializeSocketThread( IUnknown *invoker, PVOID param, PROPVARIANT *result )
    {
        auto iface = dynamic_cast<IPCLayer *>( invoker );

        BYTE* messageBuffer = nullptr;
        SIZE_T offset = 0;
        HSTRING bufferClass;
        NTSTATUS status = STATUS_SUCCESS;
        HRESULT hstatus = S_OK;
        POLL_SOCKET_ARGS currentPoll{};
        response_received_callback *currCallback;

        IBuffer *message = nullptr;
        IBufferByteAccess *messageBufferAccess = nullptr;
        IBufferFactory *bufferFactory = nullptr;
        IXodusIPCPacket *xodusPacket = nullptr;

        TRACE("invoker %p, param %p, result %p\n", invoker, param, result);

        hstatus = WindowsCreateString( RuntimeClass_Windows_Storage_Streams_Buffer, lstrlenW( RuntimeClass_Windows_Storage_Streams_Buffer ), &bufferClass );
        if ( FAILED( hstatus ) ) return status;

        hstatus = RoGetActivationFactory( bufferClass, __uuidof( IBufferFactory ), (void **)&bufferFactory );
        WindowsDeleteString( bufferClass );
        if ( FAILED( hstatus ) ) return hstatus;

        status = ConnectSocket( XODUS_SOCKET_SUFFIX );
        if ( FAILED( status ) )
        {
            MessageBoxA( nullptr, "Xodus Socket is not available! Xbox functionality will be missing.", "Warning", MB_ICONWARNING );
            throw Exception( HRESULT_FROM_NT( status ), "Xodus Socket is not available! Xbox functionality will be missing" );
        }

        // Automatically broken when the DLL is detatched.
        while ( TRUE )
        {
            status = PollSocket( &currentPoll );
            if ( FAILED( status ) )
                break;

            // Multiple messages may arrive at the same time.
            // Try to parse them all
            offset = 0;

            while ( TRUE )
            {
                IPCHeader_CTYPE *header;

                if ( currentPoll.curr_buffer_size - offset < sizeof(IPCHeader_CTYPE) )
                    break; //Not received the full header yet.

                header = reinterpret_cast<IPCHeader_CTYPE *>( currentPoll.curr_buffer + offset );

                if ( header->Magic == MagicHeaderType_Proto )
                {
                    FIXME("Proto is not yet supported!\n");
                    break;
                }
                else if ( header->Magic != MagicHeaderType_XML )
                {
                    FIXME("Invalid magic header %#x received!\n", (int)header->Magic);
                    break;
                }

                if ( currentPoll.curr_buffer_size - offset < sizeof(IPCHeader_CTYPE) + header->MessageLength )
                    break; //We have not received the full message yet.
                TRACE("header->Message_Type is %d!\n", header->Message_Type);

                /**
                 * TODO: Should we ignore messages sent by ourselves?
                 * if ( header->Message_Type == MessageType::Ping ||
                 *     header->Message_Type == MessageType::XstsTokenRequest )
                 *    break;
                 */

                hstatus = bufferFactory->Create( header->MessageLength + 1, &message );
                if ( FAILED( hstatus ) ) return hstatus; //something went horribly wrong.
                hstatus = message->QueryInterface<IBufferByteAccess>( &messageBufferAccess );
                if ( FAILED( hstatus ) ) return hstatus; //something went horribly wrong.
                hstatus = messageBufferAccess->Buffer( &messageBuffer );
                if ( FAILED( hstatus ) ) return hstatus; //something went horribly wrong.

                if ( !messageBuffer )
                    return E_OUTOFMEMORY;

                RtlCopyMemory( (PVOID)messageBuffer, (PVOID)(currentPoll.curr_buffer + offset + sizeof(IPCHeader_CTYPE)), header->MessageLength );
                hstatus = message->put_Length( static_cast<UINT32>(header->MessageLength) );
                if ( FAILED( hstatus ) ) return hstatus; //something went horribly wrong.
                offset += sizeof(IPCHeader_CTYPE) + header->MessageLength;

                messageBuffer[header->MessageLength] = '\0';

                xodusPacket = new XodusIPCPacket( header->Magic, header->Message_Type, message );

                messageBufferAccess->Release();

                LIST_FOR_EACH_ENTRY( currCallback, &iface->m_Callbacks, response_received_callback, entry )
                {
                    currCallback->handler->Invoke( xodusPacket );
                }
                // ---- //

                xodusPacket->Release();
            }

            if ( offset > 0 )
            {
                memmove( currentPoll.curr_buffer, currentPoll.curr_buffer + offset, currentPoll.curr_buffer_size - offset );
                currentPoll.curr_buffer_size -= offset;
            }
        }

        MessageBoxA( nullptr, "Socket Thread unexpectedly exited!", "Critical Error Occurred!", MB_ICONERROR );
        bufferFactory->Release();
        throw Exception( HRESULT_FROM_NT( status ), "Socket Thread unexpectedly exited!" );
    }

    struct response_received_callback
    {
        struct list entry;
        IIPCResponseHandler *handler;
        INT64 token;
    };

    struct list m_Callbacks = LIST_INIT( m_Callbacks );
    std::atomic<INT64> m_NextEventToken{ 0 };
    std::atomic_long ref{ 1 };
};

static IPCLayer g_xodus_ipclayer;
IIPCLayer *xodus_ipclayer = static_cast<IIPCLayer*>(&g_xodus_ipclayer);
