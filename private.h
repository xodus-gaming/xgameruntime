/*
 * Xbox Game runtime Library
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

#define COBJMACROS

#include <roapi.h>
#include <wine/debug.h>
#include <winstring.h>
#include <xgameruntime.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include <windows.foundation.h>
#define WIDL_using_Windows_Globalization
#include <windows.globalization.h>
#define WIDL_using_Windows_System_Profile
#include <windows.system.profile.h>

extern DWORD tlsIndex;
extern XTaskQueueHandle processQueue;
extern CRITICAL_SECTION processQueueSection;

extern IXAccessibilityImpl *x_accessibility_impl;
extern IXAppCaptureImpl *x_app_capture_impl;
extern IXAppCaptureMetadataImpl *x_app_capture_metadata_impl;
extern IXDisplayImpl *x_display_impl;
extern IXErrorImpl *x_error_impl;
extern IXGameImpl *x_game_impl;
extern IXGameActivationImpl *x_game_activation_impl;
extern IXGameEventImpl *x_game_event_impl;
extern IXGameInviteImpl *x_game_invite_impl;
extern IXGameProtocolImpl *x_game_protocol_impl;
extern IXGameRuntimeFeatureImpl *x_game_runtime_feature_impl;
extern IXGameSaveImpl *x_game_save_impl;
extern IXGameStreamingImpl *x_game_streaming_impl;
extern IXGameUiImpl *x_game_ui_impl;
extern IXLauncherImpl *x_launcher_impl;
extern IXNetworkingImpl *x_networking_impl;
extern IXPackageImpl *x_package_impl;
extern IXPersistentLocalStorageImpl *x_persistent_local_storage_impl;
extern IXStoreImpl *x_store_impl;
extern IXSystemImpl *x_system_impl;
extern IXSystemAnalyticsImpl *x_system_analytics_impl;
extern IXThreadingImpl *x_threading_impl;
extern IXUserImpl *x_user_impl;
extern IXUserDeviceImpl *x_user_device_impl;

HRESULT WINAPI QueryApiImpl( const GUID *classId, REFIID interfaceId, void **out );
