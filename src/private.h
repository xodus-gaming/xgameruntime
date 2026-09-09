/*
 * Xbox Game runtime Library
 *
 * Written by Weather.
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

#ifndef __XGAMERUNTIME_PRIVATE_H__
#define __XGAMERUNTIME_PRIVATE_H__

#define COBJMACROS

#include <winsock2.h>
#include <windows.h>
#include <minwindef.h>

#include <private/logging.h>

#ifdef __cplusplus
// Bug: WinRT in C++ within Wine lacks proper C++ type handling
// Redefine boolean as bool, and DOUBLE as double to prevent compile issues.
// Casting is purely handled by libstdc++, so it's not an issue here.
#undef boolean
#define boolean bool
#undef DOUBLE
#define DOUBLE double

// Bug: __WINESRC__ is not defined in C++ contexts.
#define __WINESRC__ 1
#include <cstdint>
#endif

#define WIDL_EXPLICIT_AGGREGATE_RETURNS

#include <xgameerr.h>
#include <xsystem.h>
#include <xgameruntimeinit.h>
#include <xgameruntimefeature.h>
#include <xnetworking.h>
#include <xuser.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xtaskqueue.h>

#include <xodusprovider.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Globalization
#include "windows.globalization.h"
#define WIDL_using_Windows_System_Profile
#include "windows.system.profile.h"

#define XODUS_SOCKET_SUFFIX "xodus.sock"
#define POLL_BUFFER_SIZE 2048
#define IPC_REQUEST_TIMEOUT_MS 5000

#define E_ILLEGAL_METHOD_CALL                              _HRESULT_TYPEDEF_(0x8000000E)

extern IXThreadingImpl *x_threading_impl;

#ifdef __cplusplus
extern ABI::Xodus::IIPCLayer *xodus_ipclayer;
extern ABI::Xodus::IXodusService *xodus_service;
extern ABI::Xodus::IXodusXMLBuilder *xodus_xml_builder;
#else
extern IIPCLayer *xodus_ipclayer;
extern IXodusService *xodus_service;
extern IXodusXMLBuilder *xodus_xml_builder;
#endif

#endif
