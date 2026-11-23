# ------------------------------------------------------------
# xgameruntime-impl
# ------------------------------------------------------------

add_library(xgameruntime-impl STATIC
        main.cpp
        ${IDL_GENERATED_FILES} )

add_dependencies(xgameruntime-impl
        xgameruntime-idl )

# System
target_sources(xgameruntime-impl PRIVATE
        GameConfig.cpp
        System/XGame.cpp
        System/XThreading.cpp
        System/XUser.cpp
        System/XUserDevice.cpp
        System/XUserIdentity.cpp )

# System/Threading (based on libHttpClient)
target_sources(xgameruntime-impl PRIVATE
        System/Threading/ThreadPool.cpp
        System/Threading/WaitTimer.cpp
        System/Threading/XAsync.cpp
        System/Threading/XTaskQueue.cpp )

# Xodus
target_sources(xgameruntime-impl PRIVATE
        Xodus/IpcFraming.cpp
        Xodus/SharedClient.cpp
        Xodus/UnixTransport.cpp
        Xodus/XodusClient.cpp
        Xodus/XodusXml.cpp )

# Host library for the xodus socket, deployed next to xgameruntime.dll
xodus_add_host_target( xgameruntime-unix SHARED
        OUTPUT ${CMAKE_BINARY_DIR}/bin/xgameruntime-unix.so
        SOURCES ${PROJECT_SOURCE_DIR}/unix/xodus_socket.c )

target_link_libraries( xgameruntime-impl PRIVATE
        xgameruntime-common
        xmllite
        ole32 )

target_include_directories(xgameruntime-impl
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDL_BINARY_DIR} )

target_compile_features(xgameruntime-impl
        PUBLIC
        cxx_std_23 )

add_library(xgameruntime SHARED
        xgameruntime.def
        ${IDL_GENERATED_FILES} )

if(WIN32)
    set_target_properties(xgameruntime PROPERTIES
            PREFIX "" )
endif()

target_link_libraries( xgameruntime PUBLIC
        xgameruntime-impl )

set_target_properties(xgameruntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LINKER_LANGUAGE CXX )

target_link_options(xgameruntime PRIVATE
        -static
        -static-libgcc
        -static-libstdc++ )
