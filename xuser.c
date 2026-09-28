/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUser
 *
 * Copyright 2026 Olivia Ryan
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

#include <winsock2.h>
#include <afunix.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "private.h"
#include "userprovider.h"
#include "util.h"
#include <bcrypt.h>
#include <dbghelp.h>
#include <ntdef.h>
#include <wininet.h>

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

static const WCHAR *ACCEPT_JSON[] = { L"application/json", NULL };

static HRESULT parse_json( const char *json, SIZE_T jsonLen, IJsonObject **object )
{
    static const WCHAR *name = RuntimeClass_Windows_Data_Json_JsonValue;
    IJsonValueStatics *statics;
    HSTRING_HEADER header;
    IJsonValue *value;
    UINT32 wJsonLen;
    HSTRING string;
    WCHAR *wJson;
    HRESULT hr;

    TRACE( "json %s, object %p.\n", debugstr_an( json, jsonLen ), object );

    if (!(wJsonLen = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, json, jsonLen, NULL, 0 ))) return HRESULT_FROM_WIN32( GetLastError() );
    if (FAILED(hr = WindowsCreateStringReference( name, wcslen( name ), &header, &string ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( string, &IID_IJsonValueStatics, (void **)&statics ))) return hr;
    if (!(wJson = calloc( wJsonLen + 1, sizeof(WCHAR) )))
    {
        IJsonValueStatics_Release( statics );
        return E_OUTOFMEMORY;
    }

    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, json, jsonLen, wJson, wJsonLen )) goto error;
    if (FAILED(hr = WindowsCreateStringReference( wJson, wJsonLen, &header, &string ))) goto cleanup;
    if (FAILED(hr = IJsonValueStatics_Parse( statics, string, &value ))) goto cleanup;
    hr = IJsonValue_GetObject( value, object );
    IJsonValue_Release( value );
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    IJsonValueStatics_Release( statics );
    free( wJson );
    return hr;
}

struct policy
{
    UINT32 version;
    UINT32 maxBodyBytes;
};

struct endpoint
{
    HSTRING protocol;
    HSTRING host;
    HSTRING path;
    HSTRING relyingParty;
    HSTRING tokenType;
    struct policy *policy;
    BOOL wildcard;
};

struct XUser
{
    IUser IUser_iface;
    LONG ref;

    BCRYPT_KEY_HANDLE key;
    char *proofKey;
    char *userToken;
    char *deviceToken;
    char *titleToken;
    char *deviceAuth;
    ULONGLONG deviceAuthTime;
    CRITICAL_SECTION deviceAuthSection;
    UINT64 xuid;
    UINT32 policiesLen;
    UINT32 endpointsLen;
    struct policy *policies;
    struct endpoint *endpoints;

    char gamertag[16];
    char modernGamertag[97];
    char modernGamertagSuffix[15];
    char uniqueModernGamertag[101];
};

static inline struct XUser *impl_from_IUser( IUser *iface )
{
    return CONTAINING_RECORD( iface, struct XUser, IUser_iface );
}

static ULONG WINAPI user_AddRef( IUser *iface )
{
    struct XUser *impl = impl_from_IUser( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI user_Release( IUser *iface )
{
    struct XUser *impl = impl_from_IUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref)
    {
        if (impl->key) BCryptDestroyKey( impl->key );
        if (impl->proofKey) free( impl->proofKey );
        if (impl->userToken) free( impl->userToken );
        free( impl->deviceToken );
        free( impl->titleToken );
        free( impl->deviceAuth );
        if (impl->policies) free( impl->policies );
        if (impl->endpoints) {
            for(UINT32 i = 0; i < impl->endpointsLen; i++) {
                WindowsDeleteString(impl->endpoints[i].protocol);
                WindowsDeleteString(impl->endpoints[i].host);
                WindowsDeleteString(impl->endpoints[i].path);
                WindowsDeleteString(impl->endpoints[i].relyingParty);
                WindowsDeleteString(impl->endpoints[i].tokenType);
            }
            free( impl->endpoints );
        }
    }
    return ref;
}

extern char *msaAppId;
extern BOOLEAN fullTrust;

#define XODUS_XML_MAGIC 0x58445358
#define XODUS_MSA_TOKEN_REQUEST 3

static HRESULT xodus_socket_path( char *path, SIZE_T size )
{
    char unixPath[MAX_PATH];
    WCHAR *dosPath;
    DWORD len;

    if (!(len = GetEnvironmentVariableA( "XODUS_SOCKET", unixPath, sizeof(unixPath) )) || len >= sizeof(unixPath))
    {
        if (!(len = GetEnvironmentVariableA( "WINE_HOST_XDG_RUNTIME_DIR", unixPath, sizeof(unixPath) )) || len >= sizeof(unixPath))
            len = GetEnvironmentVariableA( "XDG_RUNTIME_DIR", unixPath, sizeof(unixPath) );
        if (!len || len + strlen( "/xodus.sock" ) >= sizeof(unixPath)) return HRESULT_FROM_WIN32( ERROR_ENVVAR_NOT_FOUND );
        strcat( unixPath, "/xodus.sock" );
    }

    TRACE( "socket %s.\n", debugstr_a( unixPath ) );

    if (!(dosPath = wine_get_dos_file_name( unixPath ))) return HRESULT_FROM_WIN32( ERROR_PATH_NOT_FOUND );
    len = WideCharToMultiByte( CP_ACP, 0, dosPath, -1, path, size, NULL, NULL );
    HeapFree( GetProcessHeap(), 0, dosPath );
    return len ? S_OK : HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
}

static BOOL xodus_send( SOCKET s, const char *data, int size )
{
    int sent;

    while (size > 0)
    {
        if ((sent = send( s, data, size, 0 )) <= 0) return FALSE;
        data += sent;
        size -= sent;
    }
    return TRUE;
}

static BOOL xodus_recv( SOCKET s, char *data, int size )
{
    int received;

    while (size > 0)
    {
        if ((received = recv( s, data, size, 0 )) <= 0) return FALSE;
        data += received;
        size -= received;
    }
    return TRUE;
}

static HRESULT xodus_request( UINT16 type, const char *body, UINT16 size, char **response, UINT16 *responseSize )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    UINT32 magic = XODUS_XML_MAGIC;
    UINT16 responseType;
    SOCKET s = INVALID_SOCKET;
    char header[8];
    WSADATA wsa;
    HRESULT hr;

    if (FAILED(hr = xodus_socket_path( addr.sun_path, sizeof(addr.sun_path) ))) return hr;
    if (WSAStartup( MAKEWORD( 2, 2 ), &wsa )) return E_FAIL;

    memcpy( header, &magic, 4 );
    memcpy( header + 4, &type, 2 );
    memcpy( header + 6, &size, 2 );

    if ((s = socket( AF_UNIX, SOCK_STREAM, 0 )) == INVALID_SOCKET) goto error;
    if (connect( s, (struct sockaddr *)&addr, sizeof(addr) )) goto error;
    if (!xodus_send( s, header, sizeof(header) ) || !xodus_send( s, body, size )) goto error;
    if (!xodus_recv( s, header, sizeof(header) )) goto error;

    memcpy( &responseType, header + 4, 2 );
    memcpy( responseSize, header + 6, 2 );
    if (memcmp( header, &magic, 4 ) || responseType != type + 1 || !*responseSize)
    {
        hr = E_UNEXPECTED;
        goto done;
    }
    if (!(*response = calloc( 1, *responseSize + 1 )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!xodus_recv( s, *response, *responseSize ))
    {
        free( *response );
        *response = NULL;
        goto error;
    }
    hr = S_OK;
    goto done;

error:
    hr = HRESULT_FROM_WIN32( WSAGetLastError() );
    if (SUCCEEDED(hr)) hr = E_FAIL;
done:
    if (s != INVALID_SOCKET) closesocket( s );
    WSACleanup();
    return hr;
}

static char *xml_child_content( xmlNodePtr root, const char *name )
{
    xmlNodePtr child;
    xmlChar *content;
    char *value;

    for (child = root ? root->children : NULL; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE || strcmp( (const char *)child->name, name )) continue;
        if (!(content = xmlNodeGetContent( child ))) return NULL;
        value = strdup( (const char *)content );
        xmlFree( content );
        return value;
    }
    return NULL;
}

static HRESULT get_rps_tickets( BOOLEAN allowUi, char **userTicket, char **deviceTicket )
{
    UINT16 responseSize = 0;
    char *response = NULL;
    xmlChar *body = NULL;
    xmlNodePtr root;
    xmlDocPtr doc;
    int bodySize;
    HRESULT hr;

    TRACE( "allowUi %d, userTicket %p, deviceTicket %p, msaAppId %s, fullTrust %d.\n", allowUi, userTicket, deviceTicket, debugstr_a( msaAppId ), fullTrust );

    if (!msaAppId) return E_GAME_MISSING_GAME_CONFIG;

    if (!(doc = xmlNewDoc( BAD_CAST "1.0" ))) return E_OUTOFMEMORY;
    root = xmlNewNode( NULL, BAD_CAST "MSATokenRequest" );
    xmlDocSetRootElement( doc, root );
    xmlNewTextChild( root, NULL, BAD_CAST "ClientId", BAD_CAST msaAppId );
    xmlNewTextChild( root, NULL, BAD_CAST "AllowUi", BAD_CAST (allowUi ? "true" : "false") );
    xmlNewTextChild( root, NULL, BAD_CAST "MSAFullTrust", BAD_CAST (fullTrust ? "true" : "false") );
    xmlDocDumpMemory( doc, &body, &bodySize );
    xmlFreeDoc( doc );
    if (!body) return E_OUTOFMEMORY;
    if (bodySize > 0xffff)
    {
        xmlFree( body );
        return E_INVALIDARG;
    }

    hr = xodus_request( XODUS_MSA_TOKEN_REQUEST, (const char *)body, bodySize, &response, &responseSize );
    xmlFree( body );
    if (FAILED(hr))
    {
        ERR( "xodus-service request failed, hr %#lx. Is xodus-service running?\n", hr );
        return hr;
    }

    if (!(doc = xmlReadMemory( response, responseSize, NULL, NULL, 0 )))
    {
        free( response );
        return E_UNEXPECTED;
    }
    root = xmlDocGetRootElement( doc );
    *userTicket = xml_child_content( root, "Token" );
    *deviceTicket = xml_child_content( root, "DeviceRps" );
    xmlFreeDoc( doc );
    free( response );

    if (!*userTicket || !**userTicket || !*deviceTicket || !**deviceTicket)
    {
        ERR( "xodus-service returned incomplete tickets.\n" );
        free( *userTicket );
        free( *deviceTicket );
        *userTicket = *deviceTicket = NULL;
        return E_UNEXPECTED;
    }

    TRACE( "got user and device tickets.\n" );
    return S_OK;
}

static HRESULT device_auth( XUserHandle user, const char *deviceTicket, char **deviceToken )
{
    static const char template[] = "{\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\",\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"ProofKey\":";
    SIZE_T size = ARRAY_SIZE( template ) + strlen( user->proofKey ) + strlen( ",\"RpsTicket\":\"\"}}" ) + strlen( deviceTicket );
    WCHAR header[116] = { 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', ':', ' ' };
    IJsonObject *object = NULL;
    const WCHAR *tokenBuffer;
    HSTRING token = NULL;
    BYTE *buffer = NULL;
    char signature[104];
    HRESULT hr;
    char *body;

    TRACE( "user %p, deviceTicket %p, deviceToken %p.\n", user, deviceTicket, deviceToken );

    if (!(body = calloc( 1, size ))) return E_OUTOFMEMORY;
    strcpy( body, template );
    strcat( body, user->proofKey );
    strcat( body, ",\"RpsTicket\":\"" );
    strcat( body, deviceTicket );
    strcat( body, "\"}}" );
    if (FAILED(hr = IUser_GetSignature( &user->IUser_iface, 1, "POST", "https://device.auth.xboxlive.com/device/authenticate", "", size - 1, body, signature ))) goto cleanup;
    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, signature, 104, header + 11, 104 )) goto error;
    if (FAILED(hr = http_request( L"POST", L"https://device.auth.xboxlive.com/device/authenticate", body, header, ACCEPT_JSON, &buffer, &size ))) goto cleanup;
    if (FAILED(hr = parse_json( (char *)buffer, size, &object ))) goto cleanup;
    if (FAILED(hr = get_json_string( object, L"Token", &token ))) goto cleanup;
    tokenBuffer = WindowsGetStringRawBuffer( token, NULL );
    if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, tokenBuffer, -1, NULL, 0, NULL, NULL ))) goto error;
    if (!(*deviceToken = calloc( 1, size )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, tokenBuffer, -1, *deviceToken, size, NULL, NULL )) goto error;
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if (FAILED(hr) && *deviceToken) free( *deviceToken );
    if (object) IJsonObject_Release( object );
    if (token) WindowsDeleteString( token );
    if (buffer) free( buffer );
    free( body );
    return hr;
}

static HRESULT xsts_authorize( XUserHandle user, const char *relyingParty, BOOL title, char **auth );
static char *hstring_to_utf8( HSTRING string );
static WCHAR *utf8_to_wide( const char *string );

static HRESULT sisu_auth( XUserHandle user, const char *userTicket, const char *deviceToken, WCHAR **auth )
{
    static const char template[] = "{\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true,\"DeviceToken\":\"";
    SIZE_T size = ARRAY_SIZE( template ) + strlen( deviceToken ) + strlen( "\",\"AccessToken\":\"\"}" ) + strlen( userTicket );
    HSTRING authToken = NULL, gtg = NULL, mgs = NULL, mgt = NULL, umg = NULL, userToken = NULL, xid = NULL, titleToken = NULL;
    IJsonObject *authObject = NULL, *claims = NULL, *identity = NULL, *object = NULL, *userObject = NULL, *titleObject = NULL;
    WCHAR header[116] = { 'S', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', ':', ' ' };
    char *body, signature[104];
    const WCHAR *stringBuffer;
    IJsonArray *xui = NULL;
    BYTE *buffer = NULL;
    HRESULT hr;

    TRACE( "user %p, userTicket %p, deviceToken %p, auth %p.\n", user, userTicket, deviceToken, auth );

    if (!(body = calloc( 1, size ))) return E_OUTOFMEMORY;
    strcpy( body, template );
    strcat( body, deviceToken );
    strcat( body, "\",\"AccessToken\":\"" );
    strcat( body, userTicket );
    strcat( body, "\"}" );
    if (FAILED(hr = IUser_GetSignature( &user->IUser_iface, 1, "POST", "https://sisu.xboxlive.com/authorize", "", size - 1, body, signature ))) goto cleanup;
    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, signature, 104, header + 11, 104 )) goto error;
    if (FAILED(hr = http_request( L"POST", L"https://sisu.xboxlive.com/authorize", body, header, ACCEPT_JSON, &buffer, &size ))) goto cleanup;

    if (FAILED(hr = parse_json( (char *)buffer, size, &object ))) goto cleanup;
    if (FAILED(hr = get_json_object( object, L"UserToken", &userObject ))) goto cleanup;
    if (FAILED(hr = get_json_string( userObject, L"Token", &userToken ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( userToken, NULL );
    if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, NULL, 0, NULL, NULL ))) goto error;
    if (!(user->userToken = calloc( 1, size )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, user->userToken, size, NULL, NULL )) goto error;
    if (SUCCEEDED(get_json_object( object, L"TitleToken", &titleObject )) && SUCCEEDED(get_json_string( titleObject, L"Token", &titleToken )))
    {
        free( user->titleToken );
        user->titleToken = hstring_to_utf8( titleToken );
    }
    if (FAILED(hr = get_json_object( object, L"AuthorizationToken", &authObject ))) goto cleanup;
    if (FAILED(hr = get_json_object( authObject, L"DisplayClaims", &claims ))) goto cleanup;
    if (FAILED(hr = get_json_array( claims, L"xui", &xui ))) goto cleanup;
    if (FAILED(hr = IJsonArray_GetObjectAt( xui, 0, &identity ))) goto cleanup;
    if (FAILED(hr = get_json_string( identity, L"xid", &xid ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( xid, NULL );
    user->xuid = wcstoull( stringBuffer, NULL, 10 );
    if (FAILED(hr = get_json_string( identity, L"gtg", &gtg ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( gtg, NULL );
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, user->gamertag, 16, NULL, NULL )) goto error;
    if (FAILED(hr = get_json_string( identity, L"mgt", &mgt ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( mgt, NULL );
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, user->modernGamertag, 97, NULL, NULL )) goto error;
    if (SUCCEEDED(hr = get_json_string( identity, L"mgs", &mgs )))
    {
        stringBuffer = WindowsGetStringRawBuffer( mgs, NULL );
        if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, user->modernGamertagSuffix, 15, NULL, NULL )) goto error;
    }
    else if (hr != WEB_E_JSON_VALUE_NOT_FOUND) goto cleanup;
    if (FAILED(hr = get_json_string( identity, L"umg", &umg ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( umg, NULL );
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, stringBuffer, -1, user->uniqueModernGamertag, 101, NULL, NULL )) goto error;
    if (FAILED(hr = get_json_string( authObject, L"Token", &authToken ))) goto cleanup;
    stringBuffer = WindowsGetStringRawBuffer( authToken, NULL );
    if (!(*auth = calloc( wcslen( L"Authorization: XBL3.0 x=-;" ) + wcslen( stringBuffer ) + 1, sizeof(WCHAR) )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    wcscpy( *auth, L"Authorization: XBL3.0 x=-;" );
    wcscat( *auth, stringBuffer );
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if (authObject) IJsonObject_Release( authObject );
    if (userObject) IJsonObject_Release( userObject );
    if (titleObject) IJsonObject_Release( titleObject );
    if (titleToken) WindowsDeleteString( titleToken );
    if (authToken) WindowsDeleteString( authToken );
    if (userToken) WindowsDeleteString( userToken );
    if (identity) IJsonObject_Release( identity );
    if (claims) IJsonObject_Release( claims );
    if (object) IJsonObject_Release( object );
    if (gtg) WindowsDeleteString( gtg );
    if (mgs) WindowsDeleteString( mgs );
    if (mgt) WindowsDeleteString( mgt );
    if (umg) WindowsDeleteString( umg );
    if (xid) WindowsDeleteString( xid );
    if (xui) IJsonArray_Release( xui );
    if (buffer) free( buffer );
    free( body );
    return hr;
}

static HRESULT load_endpoints( XUserHandle user, BYTE *buffer, SIZE_T size )
{
    IJsonObject *payload = NULL, *tmpObject = NULL;
    IJsonArray *jsonEndpoints = NULL, *jsonPolicies = NULL;
    IVector_IJsonValue *jsonEndpointsVector = NULL, *jsonPoliciesVector = NULL;
    struct policy *policyCursor = NULL;
    struct endpoint *endpointCursor = NULL;
    DOUBLE jsonNumber;
    UINT32 arraySize;
    HSTRING hostType = NULL;
    const WCHAR* hostTypeRaw = NULL;
    HRESULT hr = S_OK;

    TRACE( "user %p, buffer %p, size %Iu \n", user, buffer, size );

    if (FAILED(hr = parse_json((char *)buffer, size, &payload))) return hr;
    if (FAILED(hr = get_json_array(payload, L"EndPoints", &jsonEndpoints))) goto cleanup;
    if (FAILED(get_json_array(payload, L"SignaturePolicies", &jsonPolicies))) jsonPolicies = NULL;

    if (jsonPolicies) {
        if (FAILED(hr = IJsonArray_QueryInterface(jsonPolicies, &IID_IVector_IJsonValue, (void **)&jsonPoliciesVector))) goto cleanup;
        if (FAILED(hr = IVector_IJsonValue_get_Size(jsonPoliciesVector, &arraySize))) goto cleanup;

        if (!(policyCursor = realloc(user->policies, sizeof(struct policy) * (user->policiesLen + arraySize)))) {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }
        user->policies = policyCursor;
        policyCursor = user->policies + user->policiesLen;
        user->policiesLen = user->policiesLen + arraySize;

        for (UINT32 i = 0; i < arraySize; i++) {
            if (FAILED(hr = IJsonArray_GetObjectAt(jsonPolicies, i, &tmpObject))) goto cleanup;

            if (FAILED(hr = get_json_number(tmpObject, L"Version", &jsonNumber))) goto cleanup;
            (*(policyCursor + i)).version = (UINT32)jsonNumber;
            if (FAILED(hr = get_json_number(tmpObject, L"MaxBodyBytes", &jsonNumber))) goto cleanup;
            (*(policyCursor + i)).maxBodyBytes = (UINT32)jsonNumber;

            IJsonObject_Release(tmpObject);
            tmpObject = NULL;
        }
    }

    if (FAILED(hr = IJsonArray_QueryInterface(jsonEndpoints, &IID_IVector_IJsonValue, (void **)&jsonEndpointsVector))) goto cleanup;
    if (FAILED(hr = IVector_IJsonValue_get_Size(jsonEndpointsVector, &arraySize))) goto cleanup;

    if (!(endpointCursor = realloc(user->endpoints, sizeof(struct endpoint) * (user->endpointsLen + arraySize)))) {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    user->endpoints = endpointCursor;
    endpointCursor = user->endpoints + user->endpointsLen;
    user->endpointsLen = user->endpointsLen + arraySize;
    memset(endpointCursor, 0, sizeof(struct endpoint) * arraySize);

    for (UINT32 i = 0; i < arraySize; i++) {
        if (FAILED(hr = IJsonArray_GetObjectAt(jsonEndpoints, i, &tmpObject))) goto cleanup;

        if (FAILED(hr = get_json_string(tmpObject, L"Protocol", &(endpointCursor + i)->protocol))) goto cleanup;
        if (FAILED(hr = get_json_string(tmpObject, L"Host", &(endpointCursor + i)->host) )) goto cleanup;
        if (FAILED(get_json_string(tmpObject, L"Path", &(endpointCursor + i)->path))) (endpointCursor + i)->path = NULL;

        if (FAILED(get_json_string(tmpObject, L"RelyingParty", &(endpointCursor + i)->relyingParty))) (endpointCursor + i)->relyingParty = NULL;
        if (FAILED(get_json_string(tmpObject, L"TokenType", &(endpointCursor + i)->tokenType))) (endpointCursor + i)->tokenType = NULL;
        if (SUCCEEDED(get_json_number(tmpObject, L"SignaturePolicyIndex", &jsonNumber)) && policyCursor && jsonNumber < (user->policiesLen - (policyCursor - user->policies))) {
            (endpointCursor + i)->policy = policyCursor + (UINT32)jsonNumber;
        } else {
            (endpointCursor + i)->policy = NULL;
        }
        if (SUCCEEDED(get_json_string(tmpObject, L"HostType", &hostType))) {
            hostTypeRaw = WindowsGetStringRawBuffer(hostType, NULL);
            (endpointCursor + i)->wildcard = wcscmp(hostTypeRaw, L"wildcard") == 0;
            WindowsDeleteString(hostType);
            hostType = NULL;
        } else {
            (endpointCursor + i)->wildcard = FALSE;
        }

        TRACE( "endpoint %s://%s%s rp %s type %s.\n", debugstr_w( WindowsGetStringRawBuffer( (endpointCursor + i)->protocol, NULL ) ),
               debugstr_w( WindowsGetStringRawBuffer( (endpointCursor + i)->host, NULL ) ), debugstr_w( WindowsGetStringRawBuffer( (endpointCursor + i)->path, NULL ) ),
               debugstr_w( WindowsGetStringRawBuffer( (endpointCursor + i)->relyingParty, NULL ) ), debugstr_w( WindowsGetStringRawBuffer( (endpointCursor + i)->tokenType, NULL ) ) );
        IJsonObject_Release(tmpObject);
        tmpObject = NULL;
    }

cleanup:
    if (jsonPoliciesVector) IVector_IJsonValue_Release(jsonPoliciesVector);
    if (jsonEndpointsVector) IVector_IJsonValue_Release(jsonEndpointsVector);
    if (jsonEndpoints) IJsonArray_Release(jsonEndpoints);
    if (tmpObject) IJsonObject_Release(tmpObject);
    if (jsonPolicies) IJsonArray_Release(jsonPolicies);
    if (payload) IJsonObject_Release(payload);

    return hr;
}

static HRESULT WINAPI user_Initialize( IUser *iface, const XUserAddOptions options )
{
    static char proofKeyTemplate[] = "{\"alg\":\"ES256\",\"crv\":\"P-256\",\"kty\":\"EC\",\"use\":\"sig\",\"x\":\"";
    static const SIZE_T proofKeySize = ARRAY_SIZE( proofKeyTemplate ) + sizeof( "\",\"y\":\"\"}" ) + 85;
    BYTE blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64], *currentBuffer = NULL, *defaultBuffer = NULL;
    char *deviceTicket = NULL, *deviceToken = NULL, *userTicket = NULL, *x, *y;
    struct XUser *impl = impl_from_IUser( iface );
    WCHAR *auth = NULL;
    NTSTATUS status;
    SIZE_T size;
    ULONG dummy;
    HRESULT hr;

    TRACE( "iface %p.\n", iface );

    /* generate signing key pair and convert public key to jwk */
    if (!NT_SUCCESS(status = BCryptGenerateKeyPair( BCRYPT_ECDSA_P256_ALG_HANDLE, &impl->key, 256, 0 ))) return HRESULT_FROM_NT( status );
    if (!NT_SUCCESS(status = BCryptFinalizeKeyPair( impl->key, 0 ))) return HRESULT_FROM_NT( status );
    if (!NT_SUCCESS(status = BCryptExportKey( impl->key, NULL, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof(blob), &dummy, 0 ))) return HRESULT_FROM_NT( status );
    if (!(impl->proofKey = calloc( 1, proofKeySize ))) return E_OUTOFMEMORY;
    x = impl->proofKey + ARRAY_SIZE( proofKeyTemplate ) - 1;
    y = x + 43 + strlen( "\",\"y\":\"" );
    strcpy( impl->proofKey, proofKeyTemplate );
    if (FAILED(hr = encode_base64_url( 32, blob + sizeof(BCRYPT_ECCKEY_BLOB), 43, x, FALSE ))) return hr;
    strcat( impl->proofKey, "\",\"y\":\"" );
    if (FAILED(hr = encode_base64_url( 32, blob + sizeof(BCRYPT_ECCKEY_BLOB) + 32, 43, y, FALSE ))) return hr;
    strcat( impl->proofKey, "\"}" );

    if (FAILED(hr = http_request( L"GET", L"https://title.mgt.xboxlive.com/titles/default/endpoints?type=1", NULL, NULL, ACCEPT_JSON, &defaultBuffer, &size ))) return hr;
    if (FAILED(hr = load_endpoints( impl, defaultBuffer, size ))) goto cleanup;
    if (FAILED(hr = get_rps_tickets( options & XUserAddOptions_AddDefaultUserAllowingUI, &userTicket, &deviceTicket ))) goto cleanup;
    if (FAILED(hr = device_auth( impl, deviceTicket, &deviceToken ))) goto cleanup;
    free( impl->deviceToken );
    if (!(impl->deviceToken = strdup( deviceToken )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    if (FAILED(hr = sisu_auth( impl, userTicket, deviceToken, &auth ))) goto cleanup;
    if (FAILED(hr = http_request( L"GET", L"https://title.mgt.xboxlive.com/titles/current/endpoints", NULL, auth, ACCEPT_JSON, &currentBuffer, &size ))) goto cleanup;
    if (FAILED(hr = load_endpoints( impl, currentBuffer, size ))) goto cleanup;

cleanup:
    if (currentBuffer) free( currentBuffer );
    if (defaultBuffer) free( defaultBuffer );
    if (deviceTicket) free( deviceTicket );
    if (deviceToken) free( deviceToken );
    if (userTicket) free( userTicket );
    if (auth) free( auth );
    return hr;
}

static BOOLEAN match_wildcard( const WCHAR *pat, UINT32 patLen, const WCHAR *inp, UINT32 inpLen )
{
    UINT32 lastMatch = -1, lastWildcard = -1, patOff = 0, inpOff = 0;

    TRACE( "pat %s, inp %s.\n", debugstr_wn( pat, patLen ), debugstr_wn( inp, inpLen ) );

    while (inpOff < inpLen && patOff < patLen)
    {
        if (pat[patOff] == '*')
        {
            lastWildcard = patOff++;
            lastMatch = inpOff;
        }
        else if (pat[patOff] == '?' || pat[patOff] == inp[inpOff])
        {
            patOff++;
            inpOff++;
        }
        else if (lastWildcard != -1)
        {
            patOff = lastWildcard + 1;
            inpOff = ++lastMatch;
        }
        else return FALSE;
    }

    return inpOff == inpLen && patOff == patLen;
}

static HRESULT WINAPI user_GetEndpointInfo( IUser *iface, const char *url, struct endpoint *info )
{
    URL_COMPONENTSW uc = { .dwStructSize = sizeof(URL_COMPONENTSW), .dwSchemeLength = -1, .dwHostNameLength = -1, .dwUrlPathLength = -1 };
    struct XUser *impl = impl_from_IUser( iface );
    struct endpoint *match = NULL;
    INT32 urlWSize;
    WCHAR *urlW;

    TRACE( "iface %p, url %s, info %p.\n", iface, debugstr_a( url ), info );

    if (!(urlWSize = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, NULL, 0 ))) return HRESULT_FROM_WIN32( GetLastError() );
    if (!(urlW = calloc( urlWSize, sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, url, -1, urlW, urlWSize )) goto error;
    if (!InternetCrackUrlW( urlW, 0, 0, &uc )) goto error;

    for (UINT32 i = 0; i < impl->endpointsLen; i++)
    {
        UINT32 protocolLen, hostLen, pathLen;
        const WCHAR *protocol = WindowsGetStringRawBuffer( impl->endpoints[i].protocol, &protocolLen );
        const WCHAR *host = WindowsGetStringRawBuffer( impl->endpoints[i].host, &hostLen );
        const WCHAR *path = WindowsGetStringRawBuffer( impl->endpoints[i].path, &pathLen );
        if (uc.dwSchemeLength != protocolLen || wcsnicmp( uc.lpszScheme, protocol, protocolLen )) continue;
        if (impl->endpoints[i].path && (uc.dwUrlPathLength < pathLen || wcsncmp( uc.lpszUrlPath, path, pathLen ))) continue;
        if (impl->endpoints[i].wildcard)
        {
            if (!match && match_wildcard( host, hostLen, uc.lpszHostName, uc.dwHostNameLength )) match = &impl->endpoints[i];
            continue;
        }
        if (uc.dwHostNameLength != hostLen || wcsnicmp( uc.lpszHostName, host, hostLen )) continue;
        match = &impl->endpoints[i];
        break;
    }
    free( urlW );
    if (!match) return E_FAIL;
    *info = *match;
    return S_OK;

error:
    free(urlW);
    return HRESULT_FROM_WIN32( GetLastError() );
}

static HRESULT WINAPI user_GetAuthorization( IUser *iface, const WCHAR *relyingParty, WCHAR **auth )
{
    struct XUser *impl = impl_from_IUser( iface );
    char *relyingPartyA, *authA = NULL;
    SIZE_T size;
    HRESULT hr;

    TRACE( "iface %p, relyingParty %s, auth %p.\n", iface, debugstr_w( relyingParty ), auth );

    if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, relyingParty, -1, NULL, 0, NULL, NULL ))) return HRESULT_FROM_WIN32( GetLastError() );
    if (!(relyingPartyA = calloc( 1, size ))) return E_OUTOFMEMORY;
    WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, relyingParty, -1, relyingPartyA, size, NULL, NULL );
    if (SUCCEEDED(hr = xsts_authorize( impl, relyingPartyA, TRUE, &authA )) && !(*auth = utf8_to_wide( authA ))) hr = E_OUTOFMEMORY;
    free( relyingPartyA );
    free( authA );
    return hr;
}

static HRESULT WINAPI user_GetSignature( IUser *iface, UINT32 version, const char *method, const char *url, const char *auth, UINT32 bodySize, const void *body, char signature[104] )
{
    BYTE hash[32], rawSignature[76] = { (version >> 24) & 0xff, (version >> 16) & 0xff, (version >> 8) & 0xff, version & 0xff };
    struct XUser *impl = impl_from_IUser( iface );
    const char *pathAndQuery = "/", *scheme;
    ULONG dataBufferSize, dummy, pathLength;
    BYTE *dataBuffer, *ptr;
    FILETIME timestamp;
    NTSTATUS status;
    HRESULT hr;

    TRACE( "iface %p, version %d, method %s, url %s, auth %p, bodySize %d, body %p, signature %p.\n", iface, version, debugstr_a( method ), debugstr_a( url ), auth, bodySize, body, signature );

    if (!url || !(scheme = strstr( url, "://" ))) return E_INVALIDARG;
    if (strchr( scheme + 3, '/' )) pathAndQuery = strchr( scheme + 3, '/' );
    pathLength = strlen( pathAndQuery );
    dataBufferSize = 18 + strlen( method ) + pathLength + strlen( auth ) + bodySize;

    /* filetime */
    GetSystemTimeAsFileTime( &timestamp );
    rawSignature[4]  = (timestamp.dwHighDateTime >> 24) & 0xff;
    rawSignature[5]  = (timestamp.dwHighDateTime >> 16) & 0xff;
    rawSignature[6]  = (timestamp.dwHighDateTime >> 8 ) & 0xff;
    rawSignature[7]  =  timestamp.dwHighDateTime        & 0xff;
    rawSignature[8]  = (timestamp.dwLowDateTime  >> 24) & 0xff;
    rawSignature[9]  = (timestamp.dwLowDateTime  >> 16) & 0xff;
    rawSignature[10] = (timestamp.dwLowDateTime  >> 8 ) & 0xff;
    rawSignature[11] =  timestamp.dwLowDateTime         & 0xff;

    /* signature content */
    if (!(dataBuffer = calloc( 1, dataBufferSize ) )) return E_OUTOFMEMORY;
    memcpy( dataBuffer, rawSignature, 4 );
    memcpy( dataBuffer + 5, rawSignature + 4, 8 );
    ptr = dataBuffer + 14;
    while (*method) *(ptr++) = toupper( *(method++) );
    memcpy( ptr + 1, pathAndQuery, pathLength );
    ptr += pathLength + 2;
    memcpy( ptr, auth, strlen( auth ) );
    ptr += strlen( auth ) + 1;
    memcpy( ptr, body, bodySize );

    /* sign hash of signature content */
    if (!NT_SUCCESS(status = BCryptHash( BCRYPT_SHA256_ALG_HANDLE, NULL, 0, dataBuffer, dataBufferSize, hash, 32 ))) goto error;
    if (!NT_SUCCESS(status = BCryptSignHash( impl->key, NULL, hash, 32, rawSignature + 12, 64, &dummy, 0 ))) goto error;

    hr = encode_base64( 76, rawSignature, 104, signature, TRUE );
    goto cleanup;

error:
    hr = HRESULT_FROM_NT( status );
cleanup:
    free( dataBuffer );
    return hr;
}

static const struct IUserVtbl user_vtbl =
{
    NULL,
    user_AddRef,
    user_Release,
    /* IUser methods */
    user_Initialize,
    user_GetEndpointInfo,
    user_GetAuthorization,
    user_GetSignature,
};

struct x_user
{
    IXUserImpl6 IXUserImpl6_iface;
    IXUserGamertagImpl IXUserGamertagImpl_iface;
    IXUserDeviceImpl2 IXUserDeviceImpl2_iface;
    LONG ref;
};

static inline struct x_user *impl_from_IXUserImpl6( IXUserImpl6 *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_QueryInterface( IXUserImpl6 *iface, REFIID iid, void **out )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown    ) ||
        IsEqualGUID( iid, &IID_IXUserImpl  ) ||
        IsEqualGUID( iid, &IID_IXUserImpl2 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl3 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl4 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl5 ) ||
        IsEqualGUID( iid, &IID_IXUserImpl6 ))
    {
        IXUserImpl6_AddRef( *out = &impl->IXUserImpl6_iface );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IXUserGamertagImpl ))
    {
        IXUserGamertagImpl_AddRef( *out = &impl->IXUserGamertagImpl_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_user_AddRef( IXUserImpl6 *iface )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_user_Release( IXUserImpl6 *iface )
{
    struct x_user *impl = impl_from_IXUserImpl6( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI x_user_XUserDuplicateHandle( IXUserImpl6 *iface, XUserHandle handle, XUserHandle *duplicatedHandle )
{
    TRACE( "iface %p, handle %p, duplicatedHandle %p.\n", iface, handle, duplicatedHandle );
    IUser_AddRef( &handle->IUser_iface );
    *duplicatedHandle = handle;
    return S_OK;
}

static void WINAPI x_user_XUserCloseHandle( IXUserImpl6 *iface, XUserHandle user )
{
    TRACE( "iface %p, user %p.\n", iface, user );
    IUser_Release( &user->IUser_iface );
}

static INT32 WINAPI x_user_XUserCompare( IXUserImpl6 *iface, XUserHandle user1, XUserHandle user2 )
{
    UINT64 a = user1 ? user1->xuid : 0, b = user2 ? user2->xuid : 0;
    TRACE( "iface %p, user1 %p, user2 %p.\n", iface, user1, user2 );
    if (user1 == user2) return 0;
    return a < b ? -1 : a > b ? 1 : 0;
}

static HRESULT WINAPI x_user_XUserGetMaxUsers( IXUserImpl6 *iface, UINT32 *maxUsers )
{
    TRACE( "iface %p, maxUsers %p.\n", iface, maxUsers );
    *maxUsers = 1;
    return S_OK;
}

struct XUserAddContext
{
    XUserAddOptions options;
    XUserHandle user;
};

static struct XUser *signed_in_user;
static CRITICAL_SECTION signed_in_section;
static CRITICAL_SECTION_DEBUG signed_in_section_debug =
{
    0, 0, &signed_in_section,
    { &signed_in_section_debug.ProcessLocksList, &signed_in_section_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": signed_in_section") }
};
static CRITICAL_SECTION signed_in_section = { &signed_in_section_debug, -1, 0, 0, 0, 0 };

static void set_signed_in_user( struct XUser *user )
{
    EnterCriticalSection( &signed_in_section );
    IUser_AddRef( &user->IUser_iface );
    if (signed_in_user) IUser_Release( &signed_in_user->IUser_iface );
    signed_in_user = user;
    LeaveCriticalSection( &signed_in_section );
}

static HRESULT find_signed_in_user( UINT64 xuid, UINT64 localId, XUserHandle *handle )
{
    HRESULT hr = E_GAMEUSER_USER_NOT_FOUND;
    if (!handle) return E_INVALIDARG;
    EnterCriticalSection( &signed_in_section );
    if (signed_in_user && ((xuid && signed_in_user->xuid == xuid) || (localId && localId == 1)))
    {
        IUser_AddRef( &signed_in_user->IUser_iface );
        *handle = signed_in_user;
        hr = S_OK;
    }
    LeaveCriticalSection( &signed_in_section );
    return hr;
}

static HRESULT WINAPI XUserAddProvider( XAsyncOp op, const XAsyncProviderData *data )
{
    struct XUserAddContext *context;
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "op %d, data %p.\n", op, data );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    context = (struct XUserAddContext *)data->context;

    switch (op)
    {
        case XAsyncOp_Begin:
            hr = IXThreadingImpl_XAsyncSchedule( xthreading, data->async, 0 );
            break;

        case XAsyncOp_GetResult:
            memcpy( data->buffer, &context->user, sizeof(XUserHandle) );
            break;

        case XAsyncOp_DoWork:
            if (!(context->user = calloc( 1, sizeof(*context->user) )))
            {
                hr = E_OUTOFMEMORY;
                goto complete;
            }
            context->user->IUser_iface.lpVtbl = &user_vtbl;
            context->user->ref = 1;
            InitializeCriticalSection( &context->user->deviceAuthSection );
            {
                HRESULT init = CoInitializeEx( NULL, COINIT_MULTITHREADED );
                hr = IUser_Initialize( &context->user->IUser_iface, context->options );
                if (SUCCEEDED(init)) CoUninitialize();
            }

        complete:
            if (SUCCEEDED(hr)) set_signed_in_user( context->user );
            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? sizeof(XUserHandle) : 0 );
            if (FAILED(hr) && context->user) IUser_Release( &context->user->IUser_iface );
            hr = S_OK;
            break;

        case XAsyncOp_Cleanup:
            free( context );
            break;

        case XAsyncOp_Cancel:
            break;
    }

    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserAddAsync( IXUserImpl6 *iface, XUserAddOptions options, XAsyncBlock *async )
{
    struct XUserAddContext *context;
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, options %d, async %p.\n", iface, options, async );

    if (!async) return E_POINTER;
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    if (!(context = calloc( 1, sizeof(*context) )))
    {
        IXThreadingImpl_Release( xthreading );
        return E_OUTOFMEMORY;
    }

    context->options = options;
    hr = IXThreadingImpl_XAsyncBegin( xthreading, async, context, NULL, "XUserAddAsync", XUserAddProvider );
    IXThreadingImpl_Release( xthreading );
    if (FAILED(hr)) free( context );
    return hr;
}

static HRESULT WINAPI x_user_XUserAddResult( IXUserImpl6 *iface, XAsyncBlock *async, XUserHandle *newUser )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, newUser %p.\n", iface, async, newUser );

    if (!async || !newUser) return E_POINTER;
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResult( xthreading, async, NULL, sizeof(*newUser), newUser, NULL );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetLocalId( IXUserImpl6 *iface, XUserHandle user, XUserLocalId *userLocalId )
{
    TRACE( "iface %p, user %p, userLocalId %p.\n", iface, user, userLocalId );
    if (!user || !userLocalId) return E_INVALIDARG;
    userLocalId->value = 1;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserFindUserByLocalId( IXUserImpl6 *iface, XUserLocalId userLocalId, XUserHandle *handle )
{
    TRACE( "iface %p, userLocalId %llu, handle %p.\n", iface, userLocalId.value, handle );
    return find_signed_in_user( 0, userLocalId.value, handle );
}

static HRESULT WINAPI x_user_XUserGetId( IXUserImpl6 *iface, XUserHandle user, UINT64 *userId )
{
    TRACE( "iface %p, user %p, userId %p.\n", iface, user, userId );
    *userId = user->xuid;
    return S_OK;
}

static HRESULT WINAPI x_user_XUserFindUserById( IXUserImpl6 *iface, UINT64 userId, XUserHandle *handle )
{
    TRACE( "iface %p, userId %llu, handle %p.\n", iface, userId, handle );
    return find_signed_in_user( userId, 0, handle );
}

static HRESULT WINAPI x_user_XUserGetIsGuest( IXUserImpl6 *iface, XUserHandle user, BOOLEAN *isGuest )
{
    FIXME( "iface %p, user %p, isGuest %p stub!\n", iface, user, isGuest );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetState( IXUserImpl6 *iface, XUserHandle user, XUserState *state )
{
    FIXME( "iface %p, user %p, state %p stub!\n", iface, user, state );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING__( IXUserImpl6 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

struct XUserGetGamerPictureContext
{
    XUserHandle user;
    XUserGamerPictureSize pictureSize;
    SIZE_T bufferSize;
    BYTE *buffer;
};

static HRESULT WINAPI XUserGetGamerPictureProvider( XAsyncOp op, const XAsyncProviderData *data )
{
    IJsonObject *object = NULL, *profile = NULL, *setting = NULL;
    static const WCHAR *accept[] = { L"image/png", NULL };
    WCHAR *auth = NULL, *headers = NULL, *url = NULL;
    IJsonArray *profiles = NULL, *settings = NULL;
    struct XUserGetGamerPictureContext *context;
    const WCHAR *suffix, *valueBuffer;
    IXThreadingImpl *xthreading;
    HSTRING value = NULL;
    BYTE *buffer = NULL;
    SIZE_T size;
    HRESULT hr;

    TRACE( "op %d, data %p.\n", op, data );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    context = (struct XUserGetGamerPictureContext *)data->context;

    switch (op)
    {
        case XAsyncOp_Begin:
            hr = IXThreadingImpl_XAsyncSchedule( xthreading, data->async, 0 );
            break;

        case XAsyncOp_GetResult:
            memcpy( data->buffer, context->buffer, context->bufferSize );
            break;

        case XAsyncOp_DoWork:
            switch (context->pictureSize)
            {
                case XUserGamerPictureSize_Small:
                    suffix = L"&format=png&w=64&h=64";
                    break;
                case XUserGamerPictureSize_Medium:
                    suffix = L"&format=png&w=208&h=208";
                    break;
                case XUserGamerPictureSize_Large:
                    suffix = L"&format=png&w=424&h=424";
                    break;
                case XUserGamerPictureSize_ExtraLarge:
                    suffix = L"&format=png&w=1080&h=1080";
                    break;
                default:
                    hr = E_INVALIDARG;
                    goto cleanup;
            }

            if (FAILED(hr = IUser_GetAuthorization( &context->user->IUser_iface, L"http://xboxlive.com", &auth ))) goto cleanup;
            if (!(headers = calloc( wcslen( L"Authorization: " ) + wcslen( auth ) + 1, sizeof(WCHAR) )))
            {
                hr = E_OUTOFMEMORY;
                goto cleanup;
            }
            wcscpy( headers, L"Authorization: " );
            wcscat( headers, auth );
            if (FAILED(hr = http_request( L"GET", L"https://profile.xboxlive.com/users/me/profile/settings?settings=PublicGamerpic", NULL, headers, ACCEPT_JSON, &buffer, &size ))) goto cleanup;
            if (FAILED(hr = parse_json( (char *)buffer, size, &object ))) goto cleanup;
            if (FAILED(hr = get_json_array( object, L"profileUsers", &profiles ))) goto cleanup;
            if (FAILED(hr = IJsonArray_GetObjectAt( profiles, 0, &profile ))) goto cleanup;
            if (FAILED(hr = get_json_array( profile, L"settings", &settings ))) goto cleanup;
            if (FAILED(hr = IJsonArray_GetObjectAt( settings, 0, &setting ))) goto cleanup;
            if (FAILED(hr = get_json_string( setting, L"value", &value ))) goto cleanup;
            valueBuffer = WindowsGetStringRawBuffer( value, NULL );
            if (!(url = calloc( wcslen( valueBuffer ) + wcslen( suffix ) + 1, sizeof(WCHAR) )))
            {
                hr = E_OUTOFMEMORY;
                goto cleanup;
            }

            wcscpy( url, valueBuffer );
            wcscat( url, suffix );
            hr = http_request( L"GET", url, NULL, NULL, accept, &context->buffer, &context->bufferSize );

        cleanup:
            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? context->bufferSize : 0 );
            if (profiles) IJsonArray_Release( profiles );
            if (settings) IJsonArray_Release( settings );
            if (profile) IJsonObject_Release( profile );
            if (setting) IJsonObject_Release( setting );
            if (object) IJsonObject_Release( object );
            if (value) WindowsDeleteString( value );
            if (headers) free( headers );
            if (buffer) free( buffer );
            if (auth) free( auth );
            if (url) free( url );
            hr = S_OK;
            break;

        case XAsyncOp_Cleanup:
            IUser_Release( &context->user->IUser_iface );
            if (context->buffer) free( context->buffer );
            free( context );
            break;

        case XAsyncOp_Cancel:
            break;
    }

    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureAsync( IXUserImpl6 *iface, XUserHandle user, XUserGamerPictureSize pictureSize, XAsyncBlock *async )
{
    struct XUserGetGamerPictureContext *context;
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, user %p, pictureSize %d, async %p.\n", iface, user, pictureSize, async );

    if (!user || !async) return E_POINTER;
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    if (!(context = calloc( 1, sizeof(*context) )))
    {
        IXThreadingImpl_Release( xthreading );
        return E_OUTOFMEMORY;
    }

    context->pictureSize = pictureSize;
    if (FAILED(hr = IXUserImpl6_XUserDuplicateHandle( iface, user, &context->user )))
    {
        IXThreadingImpl_Release( xthreading );
        return hr;
    }

    hr = IXThreadingImpl_XAsyncBegin( xthreading, async, context, NULL, "XUserGetGamerPictureAsync", XUserGetGamerPictureProvider );
    IXThreadingImpl_Release( xthreading );
    if (FAILED(hr)) free( context );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %p.\n", iface, async, bufferSize );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResultSize( xthreading, async, bufferSize );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetGamerPictureResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %Iu, buffer %p, bufferUsed %p.\n", iface, async, bufferSize, buffer, bufferUsed );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResult( xthreading, async, NULL, bufferSize, buffer, bufferUsed );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetAgeGroup( IXUserImpl6 *iface, XUserHandle user, XUserAgeGroup *ageGroup )
{
    FIXME( "iface %p, user %p, ageGroup %p stub!\n", iface, user, ageGroup );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserCheckPrivilege( IXUserImpl6 *iface, XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, BOOLEAN *hasPrivilege, XUserPrivilegeDenyReason *reason )
{
    FIXME( "iface %p, user %p, options %d, privilege %d, hasPrivilege %p, reason %p stub!\n", iface, user, options, privilege, hasPrivilege, reason );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolvePrivilegeWithUiAsync( IXUserImpl6 *iface, XUserHandle user, XUserPrivilegeOptions options, XUserPrivilege privilege, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, options %d, privilege %d, async %p stub!\n", iface, user, options, privilege, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolvePrivilegeWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

struct XUserGetTokenAndSignatureContext
{
    XUserHandle user;
    XUserGetTokenAndSignatureOptions options;
    char *url;
    char *method;
    SIZE_T headersSize;
    char *headers;
    SIZE_T bodySize;
    void *bodyBuffer;
    BOOLEAN isUtf16;
    SIZE_T dataSize;
    union
    {
        XUserGetTokenAndSignatureData *data;
        XUserGetTokenAndSignatureUtf16Data *dataUtf16;
    };
};

static HRESULT WINAPI XUserGetTokenAndSignatureProvider( XAsyncOp op, const XAsyncProviderData *data )
{
    struct XUserGetTokenAndSignatureContext *context;
    char *buffer = NULL, signature[104];
    IXThreadingImpl *xthreading;
    struct endpoint info;
    WCHAR *auth = NULL;
    SIZE_T size = 0;
    HRESULT hr;

    TRACE( "op %d, data %p.\n", op, data );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    context = (struct XUserGetTokenAndSignatureContext *)data->context;

    switch (op)
    {
        case XAsyncOp_Begin:
            hr = IXThreadingImpl_XAsyncSchedule( xthreading, data->async, 0 );
            break;

        case XAsyncOp_GetResult:
        {
            char *base = (char *)context->data, *out = data->buffer;
            memcpy( out, base, context->dataSize );
            if (context->isUtf16)
            {
                XUserGetTokenAndSignatureUtf16Data *result = data->buffer;
                if (result->token) result->token = (WCHAR *)(out + ((char *)result->token - base));
                if (result->signature) result->signature = (WCHAR *)(out + ((char *)result->signature - base));
            }
            else
            {
                XUserGetTokenAndSignatureData *result = data->buffer;
                if (result->token) result->token = out + (result->token - base);
                if (result->signature) result->signature = out + (result->signature - base);
            }
            break;
        }

        case XAsyncOp_DoWork:
            if (FAILED(hr = IUser_GetEndpointInfo( &context->user->IUser_iface, context->url, &info ))) goto complete;
            if (info.tokenType && FAILED(hr = IUser_GetAuthorization( &context->user->IUser_iface, WindowsGetStringRawBuffer(info.relyingParty, NULL), &auth ))) goto complete;

            if (context->isUtf16)
            {
                context->dataSize = sizeof(*context->dataUtf16) + (auth ? (wcslen( auth ) + 1) * sizeof(WCHAR) : 0) + (info.policy ? 104 * sizeof(WCHAR) : 0);
                if (!(context->dataUtf16 = calloc( 1, context->dataSize )))
                {
                    hr = E_OUTOFMEMORY;
                    goto complete;
                }
                if (auth)
                {
                    context->dataUtf16->tokenCount = wcslen( auth );
                    context->dataUtf16->token = (WCHAR *)(context->dataUtf16 + 1);
                    wcscpy( (WCHAR *)context->dataUtf16->token, auth );
                }
                if (info.policy)
                {
                    if (auth)
                    {
                        if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, auth, -1, NULL, 0, NULL, NULL ))) goto error;
                        if (!(buffer = calloc( 1, size )))
                        {
                            hr = E_OUTOFMEMORY;
                            goto complete;
                        }
                        if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, auth, -1, buffer, size, NULL, NULL )) goto error;
                    }
                    context->dataUtf16->signatureCount = 104;
                    context->dataUtf16->signature = (WCHAR *)(context->dataUtf16 + 1) + (auth ? wcslen( auth ) + 1 : 0);
                    if (FAILED(hr = IUser_GetSignature( &context->user->IUser_iface, info.policy->version, context->method, context->url, (auth ? buffer : ""), min( context->bodySize, info.policy->maxBodyBytes ), context->bodyBuffer, signature ))) goto complete;
                    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, signature, 104, (WCHAR *)context->dataUtf16->signature, 104 )) goto error;
                }
            }
            else
            {
                if (auth && !(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, auth, wcslen( auth ), NULL, 0, NULL, NULL ))) goto error;
                context->dataSize = sizeof(*context->data) + (auth ? size + 1 : 0) + (info.policy ? 104 : 0);
                if (!(context->data = calloc( 1, context->dataSize )))
                {
                    hr = E_OUTOFMEMORY;
                    goto complete;
                }
                if (auth)
                {
                    context->data->tokenSize = size;
                    context->data->token = (char *)(context->data + 1);
                    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, auth, wcslen( auth ), (char *)context->data->token, size, NULL, NULL )) goto error;
                }
                if (info.policy)
                {
                    context->data->signatureSize = 104;
                    context->data->signature = (char *)(context->data + 1) + (auth ? size + 1 : 0);
                    if (FAILED(hr = IUser_GetSignature( &context->user->IUser_iface, info.policy->version, context->method, context->url, (auth ? context->data->token : ""), min( context->bodySize, info.policy->maxBodyBytes ), context->bodyBuffer, (char *)context->data->signature ))) goto complete;
                }
            }
            goto complete;

        error:
            hr = HRESULT_FROM_WIN32( GetLastError() );
        complete:
            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? context->dataSize : 0 );
            if (FAILED(hr)) { free( context->data ); context->data = NULL; }
            if (buffer) free( buffer );
            if (auth) free( auth );
            hr = S_OK;
            break;

        case XAsyncOp_Cleanup:
            IUser_Release( &context->user->IUser_iface );
            free( context->data );
            free( context );
            break;

        case XAsyncOp_Cancel:
            break;
    }

    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureAsync( IXUserImpl6 *iface, XUserHandle user, XUserGetTokenAndSignatureOptions options, const char *method, const char *url, SIZE_T headerCount, const XUserGetTokenAndSignatureHttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async )
{
    struct XUserGetTokenAndSignatureContext *context;
    SIZE_T contextSize, headersSize = 0;
    IXThreadingImpl *xthreading;
    HRESULT hr;
    char *ptr;

    TRACE( "iface %p, user %p, options %d, method %s, url %s, headerCount %Iu, headers %p, bodySize %Iu, bodyBuffer %p, async %p.\n", iface, user, options, debugstr_a( method ), debugstr_a( url ), headerCount, headers, bodySize, bodyBuffer, async );

    contextSize = sizeof(*context) + strlen( url ) + strlen( method ) + 2 + bodySize;
    for (SIZE_T i = 0; i < headerCount; i++)
        headersSize += (strlen( headers[i].value ) + 1);

    if (!(context = calloc( 1, contextSize + headersSize ))) return E_OUTOFMEMORY;
    IUser_AddRef( &user->IUser_iface );
    context->user = user;
    context->options = options;
    context->headersSize = headersSize;
    context->bodySize = bodySize;
    context->isUtf16 = FALSE;

    /* url */
    ptr = (char *)context + sizeof(*context);
    ptr += strlen( strcpy( (context->url = ptr), url ) ) + 1;
    /* method */
    ptr += (strlen( strcpy( (context->method = ptr), method ) ) + 1);
    /* headers */
    context->headers = ptr;
    for (SIZE_T i = 0; i < headerCount; i++)
        ptr += (strlen( strcpy( ptr, headers[i].value ) ) + 1);
    /* body */
    memcpy( (context->bodyBuffer = ptr), bodyBuffer, bodySize );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) goto error;
    hr = IXThreadingImpl_XAsyncBegin( xthreading, async, context, NULL, "XUserGetTokenAndSignatureAsync", XUserGetTokenAndSignatureProvider );
    IXThreadingImpl_Release( xthreading );
    if (FAILED(hr)) goto error;
    return hr;
error:
    free( context );
    IUser_Release( &user->IUser_iface );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %p.\n", iface, async, bufferSize );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResultSize( xthreading, async, bufferSize );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureData **ptrToBuffer, SIZE_T *bufferUsed )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %Iu, buffer %p, ptrToBuffer %p, bufferUsed %p.\n", iface, async, bufferSize, buffer, ptrToBuffer, bufferUsed );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResult( xthreading, async, NULL, bufferSize, buffer, bufferUsed );
    *ptrToBuffer = (XUserGetTokenAndSignatureData *)buffer;
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16Async( IXUserImpl6 *iface, XUserHandle user, XUserGetTokenAndSignatureOptions options, const WCHAR *method, const WCHAR *url, SIZE_T headerCount, const XUserGetTokenAndSignatureUtf16HttpHeader *headers, SIZE_T bodySize, const void *bodyBuffer, XAsyncBlock *async )
{
    SIZE_T contextSize, headersSize = 0, methodLen, urlLen;
    struct XUserGetTokenAndSignatureContext *context;
    IXThreadingImpl *xthreading;
    HRESULT hr;
    char *ptr;
    int size;

    TRACE( "iface %p, user %p, options %d, method %s, url %s, headerCount %Iu, headers %p, bodySize %Iu, bodyBuffer %p, async %p.\n", iface, user, options, debugstr_w( method ), debugstr_w( url ), headerCount, headers, bodySize, bodyBuffer, async );

    if (!(methodLen = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, method, -1, NULL, 0, NULL, NULL ))) return HRESULT_FROM_WIN32( GetLastError() );
    if (!(urlLen = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, url, -1, NULL, 0, NULL, NULL ))) return HRESULT_FROM_WIN32( GetLastError() );
    contextSize = sizeof(*context) + urlLen + methodLen + bodySize;
    for (SIZE_T i = 0; i < headerCount; i++)
    {
        if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, headers[i].value, -1, NULL, 0, NULL, NULL ))) return HRESULT_FROM_WIN32( GetLastError() );
        headersSize += size;
    }

    if (!(context = calloc( 1, contextSize + headersSize ))) return E_OUTOFMEMORY;
    IUser_AddRef( &user->IUser_iface );
    context->user = user;
    context->options = options;
    context->headersSize = headersSize;
    context->bodySize = bodySize;
    context->isUtf16 = TRUE;

    /* url */
    ptr = (char *)context + sizeof(*context);
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, url, -1, (context->url = ptr), urlLen, NULL, NULL )) goto error_win32;
    ptr += urlLen;
    /* method */
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, method, -1, (context->method = ptr), methodLen, NULL, NULL )) goto error_win32;
    ptr += methodLen;
    /* headers */
    context->headers = ptr;
    for (SIZE_T i = 0; i < headerCount; i++)
    {
        if (!(size = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, headers[i].value, -1, NULL, 0, NULL, NULL ))) goto error_win32;
        if(!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, headers[i].value, -1, ptr, size, NULL, NULL )) goto error_win32;
        ptr += size;
    }
    /* body */
    memcpy( (context->bodyBuffer = ptr), bodyBuffer, bodySize );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) goto error;
    hr = IXThreadingImpl_XAsyncBegin( xthreading, async, context, NULL, "XUserGetTokenAndSignatureUtf16Async", XUserGetTokenAndSignatureProvider );
    IXThreadingImpl_Release( xthreading );
    if (FAILED(hr)) goto error;
    return hr;

error_win32:
    hr = HRESULT_FROM_WIN32( GetLastError() );
error:
    free( context );
    IUser_Release( &user->IUser_iface );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16ResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %p.\n", iface, async, bufferSize );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResultSize( xthreading, async, bufferSize );
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserGetTokenAndSignatureUtf16Result( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XUserGetTokenAndSignatureUtf16Data **ptrToBuffer, SIZE_T *bufferUsed )
{
    IXThreadingImpl *xthreading;
    HRESULT hr;

    TRACE( "iface %p, async %p, bufferSize %Iu, buffer %p, ptrToBuffer %p, bufferUsed %p.\n", iface, async, bufferSize, buffer, ptrToBuffer, bufferUsed );

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&xthreading ))) return hr;
    hr = IXThreadingImpl_XAsyncGetResult( xthreading, async, NULL, bufferSize, buffer, bufferUsed );
    *ptrToBuffer = (XUserGetTokenAndSignatureUtf16Data *)buffer;
    IXThreadingImpl_Release( xthreading );
    return hr;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiAsync( IXUserImpl6 *iface, XUserHandle user, const char *url, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, url %s, async %p stub!\n", iface, user, debugstr_a( url ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiUtf16Async( IXUserImpl6 *iface, XUserHandle user, const WCHAR *url, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, url %s, async %p stub!\n", iface, user, debugstr_w( url ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserResolveIssueWithUiUtf16Result( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserRegisterForChangeEvent( IXUserImpl6 *iface, XTaskQueueHandle queue, void *context, XUserChangeEventCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_XUserUnregisterForChangeEvent( IXUserImpl6 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

static HRESULT WINAPI x_user_XUserGetSignOutDeferral( IXUserImpl6 *iface, XUserSignOutDeferralHandle *deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );
    *deferral = NULL;
    return E_GAMEUSER_DEFERRAL_NOT_AVAILABLE;
}

static void WINAPI x_user_XUserCloseSignOutDeferralHandle( IXUserImpl6 *iface, XUserSignOutDeferralHandle deferral )
{
    TRACE( "iface %p, deferral %p.\n", iface, deferral );
}

static HRESULT WINAPI x_user_XUserAddByIdWithUiAsync( IXUserImpl6 *iface, UINT64 userId, XAsyncBlock *async )
{
    FIXME( "iface %p, userId %llu, async %p stub!\n", iface, userId, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserAddByIdWithUiResult( IXUserImpl6 *iface, XAsyncBlock *async, XUserHandle *newUser )
{
    FIXME( "iface %p, async %p, newUser %p stub!\n", iface, async, newUser );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyAsync( IXUserImpl6 *iface, XUserHandle user, XUserGetMsaTokenSilentlyOptions options, const char *scope, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, options %u, scope %s, async %p stub!\n", iface, user, options, debugstr_a( scope ), async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyResult( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T resultTokenSize, char *resultToken, SIZE_T *resultTokenUsed )
{
    FIXME( "iface %p, async %p, resultTokenSize %Iu, resultToken %p, resultTokenUsed %p stub!\n", iface, async, resultTokenSize, resultToken, resultTokenUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserGetMsaTokenSilentlyResultSize( IXUserImpl6 *iface, XAsyncBlock *async, SIZE_T *tokenSize )
{
    FIXME( "iface %p, async %p, tokenSize %p stub!\n", iface, async, tokenSize );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_XUserIsStoreUser( IXUserImpl6 *iface, XUserHandle user )
{
    FIXME( "iface %p, user %p stub!\n", iface, user );
    return TRUE;
}

static HRESULT WINAPI x_user_XUserPlatformRemoteConnectSetEventHandlers( IXUserImpl6 *iface, XTaskQueueHandle queue, XUserPlatformRemoteConnectEventHandlers *handlers )
{
    FIXME( "iface %p, queue %p, handlers %p stub!\n", iface, queue, handlers );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformRemoteConnectCancelPrompt( IXUserImpl6 *iface, XUserPlatformOperation operation )
{
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformSpopPromptSetEventHandlers( IXUserImpl6 *iface, XTaskQueueHandle queue, XUserPlatformSpopPromptEventHandler *handler, void *context )
{
    FIXME( "iface %p, queue %p, handler %p, context %p stub!\n", iface, queue, handler, context );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserPlatformSpopPromptComplete( IXUserImpl6 *iface, XUserPlatformOperation operation, XUserPlatformOperationResult result )
{
    FIXME( "iface %p, operation %p, result %d stub!\n", iface, operation, result );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_XUserIsSignOutPresent( IXUserImpl6 *iface )
{
    TRACE( "iface %p.\n", iface );
    return FALSE;
}

static HRESULT WINAPI x_user_XUserSignOutAsync( IXUserImpl6 *iface, XUserHandle user, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, async %p stub!\n", iface, user, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_XUserSignOutResult( IXUserImpl6 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static const struct IXUserImpl6Vtbl x_user_vtbl =
{
    x_user_QueryInterface,
    x_user_AddRef,
    x_user_Release,
    /* IXUserImpl methods */
    x_user_XUserDuplicateHandle,
    x_user_XUserCloseHandle,
    x_user_XUserCompare,
    x_user_XUserGetMaxUsers,
    x_user_XUserAddAsync,
    x_user_XUserAddResult,
    x_user_XUserGetLocalId,
    x_user_XUserFindUserByLocalId,
    x_user_XUserGetId,
    x_user_XUserFindUserById,
    x_user_XUserGetIsGuest,
    x_user_XUserGetState,
    __PADDING__,
    x_user_XUserGetGamerPictureAsync,
    x_user_XUserGetGamerPictureResultSize,
    x_user_XUserGetGamerPictureResult,
    x_user_XUserGetAgeGroup,
    x_user_XUserCheckPrivilege,
    x_user_XUserResolvePrivilegeWithUiAsync,
    x_user_XUserResolvePrivilegeWithUiResult,
    x_user_XUserGetTokenAndSignatureAsync,
    x_user_XUserGetTokenAndSignatureResultSize,
    x_user_XUserGetTokenAndSignatureResult,
    x_user_XUserGetTokenAndSignatureUtf16Async,
    x_user_XUserGetTokenAndSignatureUtf16ResultSize,
    x_user_XUserGetTokenAndSignatureUtf16Result,
    x_user_XUserResolveIssueWithUiAsync,
    x_user_XUserResolveIssueWithUiResult,
    x_user_XUserResolveIssueWithUiUtf16Async,
    x_user_XUserResolveIssueWithUiUtf16Result,
    x_user_XUserRegisterForChangeEvent,
    x_user_XUserUnregisterForChangeEvent,
    x_user_XUserGetSignOutDeferral,
    x_user_XUserCloseSignOutDeferralHandle,
    /* IXUserImpl2 methods */
    x_user_XUserAddByIdWithUiAsync,
    x_user_XUserAddByIdWithUiResult,
    /* IXUserImpl3 methods */
    x_user_XUserGetMsaTokenSilentlyAsync,
    x_user_XUserGetMsaTokenSilentlyResult,
    x_user_XUserGetMsaTokenSilentlyResultSize,
    /* IXUserImpl4 methods */
    x_user_XUserIsStoreUser,
    /* IXUserImpl5 methods */
    x_user_XUserPlatformRemoteConnectSetEventHandlers,
    x_user_XUserPlatformRemoteConnectCancelPrompt,
    x_user_XUserPlatformSpopPromptSetEventHandlers,
    x_user_XUserPlatformSpopPromptComplete,
    /* IXUserImpl6 methods */
    x_user_XUserIsSignOutPresent,
    x_user_XUserSignOutAsync,
    x_user_XUserSignOutResult,
};

static inline struct x_user *impl_from_IXUserGamertagImpl( IXUserGamertagImpl *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserGamertagImpl_iface );
}

static HRESULT WINAPI x_user_gamertag_QueryInterface( IXUserGamertagImpl *iface, REFIID riid, void **out )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_QueryInterface( &impl->IXUserImpl6_iface, riid, out );
}

static ULONG WINAPI x_user_gamertag_AddRef( IXUserGamertagImpl *iface )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_AddRef( &impl->IXUserImpl6_iface );
}

static ULONG WINAPI x_user_gamertag_Release( IXUserGamertagImpl *iface )
{
    struct x_user *impl = impl_from_IXUserGamertagImpl( iface );
    return IXUserImpl6_Release( &impl->IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_gamertag_XUserGetGamertag( IXUserGamertagImpl *iface, XUserHandle user, XUserGamertagComponent gamertagComponent, SIZE_T gamertagSize, char *gamertag, SIZE_T *gamertagUsed )
{
    TRACE( "iface %p, user %p, gamertagComponent %d, gamertagSize %Iu, gamertag %p, gamertagUsed %p.\n", iface, user, gamertagComponent, gamertagSize, gamertag, gamertagUsed );

    switch (gamertagComponent)
    {
        case XUserGamertagComponent_Classic:
            if (gamertagSize <= strlen( user->gamertag )) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
            strcpy( gamertag, user->gamertag );
            if (gamertagUsed) *gamertagUsed = strlen( user->gamertag ) + 1;
            return S_OK;

        case XUserGamertagComponent_Modern:
            if (gamertagSize <= strlen( user->modernGamertag )) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
            strcpy( gamertag, user->modernGamertag );
            if (gamertagUsed) *gamertagUsed = strlen( user->modernGamertag ) + 1;
            return S_OK;

        case XUserGamertagComponent_ModernSuffix:
            if (gamertagSize <= strlen( user->modernGamertagSuffix )) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
            strcpy( gamertag, user->modernGamertagSuffix );
            if (gamertagUsed) *gamertagUsed = strlen( user->modernGamertagSuffix ) + 1;
            return S_OK;

        case XUserGamertagComponent_UniqueModern:
            if (gamertagSize <= strlen( user->uniqueModernGamertag )) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
            strcpy( gamertag, user->uniqueModernGamertag );
            if (gamertagUsed) *gamertagUsed = strlen( user->uniqueModernGamertag ) + 1;
            return S_OK;
    }

    return E_INVALIDARG;
}

static const struct IXUserGamertagImplVtbl x_user_gamertag_vtbl =
{
    x_user_gamertag_QueryInterface,
    x_user_gamertag_AddRef,
    x_user_gamertag_Release,
    /* IXUserGamertag methods */
    x_user_gamertag_XUserGetGamertag,
};

static inline struct x_user *impl_from_IXUserDeviceImpl2( IXUserDeviceImpl2 *iface )
{
    return CONTAINING_RECORD( iface, struct x_user, IXUserDeviceImpl2_iface );
}

static HRESULT WINAPI x_user_device_QueryInterface( IXUserDeviceImpl2 *iface, REFIID iid, void **out )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown          ) ||
        IsEqualGUID( iid, &IID_IXUserDeviceImpl  ) ||
        IsEqualGUID( iid, &IID_IXUserDeviceImpl2 ))
    {
        IXUserDeviceImpl2_AddRef( *out = &impl->IXUserDeviceImpl2_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_user_device_AddRef( IXUserDeviceImpl2 *iface )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );
    return IXUserImpl6_AddRef( &impl->IXUserImpl6_iface );
}

static ULONG WINAPI x_user_device_Release( IXUserDeviceImpl2 *iface )
{
    struct x_user *impl = impl_from_IXUserDeviceImpl2( iface );
    return IXUserImpl6_Release( &impl->IXUserImpl6_iface );
}

static HRESULT WINAPI x_user_device_XUserFindForDevice( IXUserDeviceImpl2 *iface, const APP_LOCAL_DEVICE_ID *deviceId, XUserHandle *handle )
{
    FIXME( "iface %p, deviceId %p, handle %p stub!\n", iface, deviceId, handle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserRegisterForDeviceAssociationChanged( IXUserDeviceImpl2 *iface, XTaskQueueHandle queue, void *context, XUserDeviceAssociationChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_device_XUserUnregisterForDeviceAssociationChanged( IXUserDeviceImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

static HRESULT WINAPI x_user_device_XUserGetDefaultAudioEndpointUtf16( IXUserDeviceImpl2 *iface, XUserLocalId user, XUserDefaultAudioEndpointKind defaultAudioEndpointKind, SIZE_T endpointIdUtf16Count, WCHAR *endpointIdUtf16, SIZE_T *endpointIdUtf16Used )
{
    FIXME( "iface %p, user %p, defaultAudioEndpointKind %d, endpointIdUtf16Count %Iu, endpointIdUtf16 %p, endpointIdUtf16Used %p stub!\n", iface, &user, defaultAudioEndpointKind, endpointIdUtf16Count, endpointIdUtf16, endpointIdUtf16Used );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserRegisterForDefaultAudioEndpointUtf16Changed( IXUserDeviceImpl2 *iface, XTaskQueueHandle queue, void *context, XUserDefaultAudioEndpointUtf16ChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_user_device_XUserUnregisterForDefaultAudioEndpointUtf16Changed( IXUserDeviceImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

static HRESULT WINAPI x_user_device_XUserFindControllerForUserWithUiAsync( IXUserDeviceImpl2 *iface, XUserHandle user, XAsyncBlock *async )
{
    FIXME( "iface %p, user %p, async %p stub!\n", iface, user, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_user_device_XUserFindControllerForUserWithUiResult( IXUserDeviceImpl2 *iface, XAsyncBlock *async, APP_LOCAL_DEVICE_ID *deviceId )
{
    FIXME( "iface %p, async %p, deviceId %p stub!\n", iface, async, deviceId );
    return E_NOTIMPL;
}

static const struct IXUserDeviceImpl2Vtbl x_user_device_vtbl =
{
    x_user_device_QueryInterface,
    x_user_device_AddRef,
    x_user_device_Release,
    /* IXUserDeviceImpl/IXUserDeviceImpl2 methods */
    x_user_device_XUserFindForDevice,
    x_user_device_XUserRegisterForDeviceAssociationChanged,
    x_user_device_XUserUnregisterForDeviceAssociationChanged,
    x_user_device_XUserGetDefaultAudioEndpointUtf16,
    x_user_device_XUserRegisterForDefaultAudioEndpointUtf16Changed,
    x_user_device_XUserUnregisterForDefaultAudioEndpointUtf16Changed,
    x_user_device_XUserFindControllerForUserWithUiAsync,
    x_user_device_XUserFindControllerForUserWithUiResult,
};

static struct x_user x_user =
{
    {&x_user_vtbl},
    {&x_user_gamertag_vtbl},
    {&x_user_device_vtbl},
    0,
};

IXUserImpl *x_user_impl = (IXUserImpl *)&x_user.IXUserImpl6_iface;
IXUserDeviceImpl *x_user_device_impl = (IXUserDeviceImpl *)&x_user.IXUserDeviceImpl2_iface;

static char *hstring_to_utf8( HSTRING string )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( string, NULL );
    int size = WideCharToMultiByte( CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL );
    char *out;

    if (!size || !(out = calloc( 1, size ))) return NULL;
    WideCharToMultiByte( CP_UTF8, 0, buffer, -1, out, size, NULL, NULL );
    return out;
}

static WCHAR *utf8_to_wide( const char *string )
{
    int size = MultiByteToWideChar( CP_UTF8, 0, string, -1, NULL, 0 );
    WCHAR *out;

    if (!size || !(out = calloc( size, sizeof(WCHAR) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, string, -1, out, size );
    return out;
}

UINT64 xuser_get_xuid( XUserHandle user )
{
    return user->xuid;
}

HRESULT xuser_signed_request( XUserHandle user, const char *method, const char *url, const char *auth, const char *extraHeaders,
                              const void *body, DWORD bodySize, DWORD *status, BYTE **buffer, SIZE_T *bufferSize )
{
    WCHAR *methodW = NULL, *urlW = NULL, *headersW = NULL;
    char signature[105] = { 0 }, *headers = NULL;
    SIZE_T headersSize;
    HRESULT hr;

    TRACE( "user %p, method %s, url %s, bodySize %lu.\n", user, debugstr_a( method ), debugstr_a( url ), bodySize );

    if (FAILED(hr = IUser_GetSignature( &user->IUser_iface, 1, method, url, auth ? auth : "", min( bodySize, 8192 ), body, signature ))) return hr;
    headersSize = strlen( "Authorization: \r\nSignature: \r\n" ) + (auth ? strlen( auth ) : 0) + 104 + (extraHeaders ? strlen( extraHeaders ) : 0) + 1;
    if (!(headers = calloc( 1, headersSize ))) return E_OUTOFMEMORY;
    if (auth) snprintf( headers, headersSize, "Authorization: %s\r\nSignature: %s\r\n%s", auth, signature, extraHeaders ? extraHeaders : "" );
    else snprintf( headers, headersSize, "Signature: %s\r\n%s", signature, extraHeaders ? extraHeaders : "" );

    if (!(methodW = utf8_to_wide( method )) || !(urlW = utf8_to_wide( url )) || !(headersW = utf8_to_wide( headers ))) hr = E_OUTOFMEMORY;
    else
    {
        for (int attempt = 0; attempt < 3; attempt++)
        {
            if (attempt)
            {
                WARN( "request %s %s failed with hr %#lx, retrying.\n", debugstr_a( method ), debugstr_a( url ), hr );
                Sleep( 500 * attempt );
            }
            if (SUCCEEDED(hr = http_request_raw( methodW, urlW, headersW, body, bodySize, status, buffer, bufferSize )) && *status < 500) break;
            if (SUCCEEDED(hr))
            {
                free( *buffer );
                *buffer = NULL;
                *bufferSize = 0;
            }
        }
    }

    free( methodW );
    free( urlW );
    free( headersW );
    free( headers );
    return hr;
}

static HRESULT xsts_authorize( XUserHandle user, const char *relyingParty, BOOL title, char **auth )
{
    static const char url[] = "https://xsts.auth.xboxlive.com/xsts/authorize";
    IJsonObject *object = NULL, *claims = NULL, *identity = NULL;
    HSTRING token = NULL, uhs = NULL;
    char *body = NULL, *tokenA = NULL, *uhsA = NULL;
    IJsonArray *xui = NULL;
    BYTE *buffer = NULL;
    SIZE_T size, bufferSize;
    HRESULT hr, comInit = E_FAIL;
    DWORD status;

    TRACE( "user %p, relyingParty %s, title %d.\n", user, debugstr_a( relyingParty ), title );

    if (!user->deviceToken || !user->userToken || !user->proofKey) return E_UNEXPECTED;

    if (!title || !user->titleToken) title = FALSE;
    size = strlen( relyingParty ) + strlen( user->deviceToken ) + strlen( user->userToken ) + strlen( user->proofKey ) + (title ? strlen( user->titleToken ) : 0) + 256;
    if (!(body = calloc( 1, size )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    snprintf( body, size, "{\"RelyingParty\":\"%s\",\"TokenType\":\"JWT\",\"Properties\":{\"SandboxId\":\"RETAIL\",\"DeviceToken\":\"%s\",%s%s%s\"UserTokens\":[\"%s\"],\"ProofKey\":%s}}",
              relyingParty, user->deviceToken, title ? "\"TitleToken\":\"" : "", title ? user->titleToken : "", title ? "\"," : "", user->userToken, user->proofKey );

    if (FAILED(hr = xuser_signed_request( user, "POST", url, NULL, "x-xbl-contract-version: 1\r\nContent-Type: application/json\r\n",
                                          body, strlen( body ), &status, &buffer, &bufferSize ))) goto cleanup;
    if (status != 200)
    {
        ERR( "xsts authorization failed, status %lu, body %s.\n", status, debugstr_an( (char *)buffer, bufferSize ) );
        hr = E_FAIL;
        goto cleanup;
    }

    comInit = CoInitializeEx( NULL, COINIT_MULTITHREADED );
    if (FAILED(hr = parse_json( (char *)buffer, bufferSize, &object ))) goto cleanup;
    if (FAILED(hr = get_json_string( object, L"Token", &token ))) goto cleanup;
    if (FAILED(hr = get_json_object( object, L"DisplayClaims", &claims ))) goto cleanup;
    if (FAILED(hr = get_json_array( claims, L"xui", &xui ))) goto cleanup;
    if (FAILED(hr = IJsonArray_GetObjectAt( xui, 0, &identity ))) goto cleanup;
    if (FAILED(hr = get_json_string( identity, L"uhs", &uhs ))) goto cleanup;
    if (!(tokenA = hstring_to_utf8( token )) || !(uhsA = hstring_to_utf8( uhs )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    size = strlen( "XBL3.0 x=;" ) + strlen( uhsA ) + strlen( tokenA ) + 1;
    if (!(*auth = calloc( 1, size )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    snprintf( *auth, size, "XBL3.0 x=%s;%s", uhsA, tokenA );

cleanup:
    if (identity) IJsonObject_Release( identity );
    if (claims) IJsonObject_Release( claims );
    if (object) IJsonObject_Release( object );
    if (xui) IJsonArray_Release( xui );
    if (token) WindowsDeleteString( token );
    if (uhs) WindowsDeleteString( uhs );
    free( tokenA );
    free( uhsA );
    free( buffer );
    free( body );
    if (SUCCEEDED(comInit)) CoUninitialize();
    return hr;
}

HRESULT xuser_device_authorization( XUserHandle user, const char *relyingParty, char **auth )
{
    HRESULT hr;

    EnterCriticalSection( &user->deviceAuthSection );
    if (user->deviceAuth && GetTickCount64() - user->deviceAuthTime < 3600 * 1000)
        hr = (*auth = strdup( user->deviceAuth )) ? S_OK : E_OUTOFMEMORY;
    else if (SUCCEEDED(hr = xsts_authorize( user, relyingParty, FALSE, auth )))
    {
        free( user->deviceAuth );
        user->deviceAuth = strdup( *auth );
        user->deviceAuthTime = GetTickCount64();
    }
    LeaveCriticalSection( &user->deviceAuthSection );
    return hr;
}
