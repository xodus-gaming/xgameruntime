# ------------------------------------------------------------
# xgameruntime
# ------------------------------------------------------------

add_library(xgameruntime SHARED
        main.cpp
        xgameruntime.def
        ${IDL_GENERATED_FILES} )

add_dependencies(xgameruntime
        xgameruntime-idl )

# Xodus
target_sources(xgameruntime PRIVATE
        Xodus/Structs.cpp
        Xodus/IPCLayer.cpp
        Xodus/XodusService.cpp
        Xodus/XodusXMLBuilder.cpp )

# System
target_sources(xgameruntime PRIVATE
        System/XThreading.cpp )

# System/Threading (based on libHttpClient)
target_sources(xgameruntime PRIVATE
        System/Threading/ThreadPool.cpp
        System/Threading/WaitTimer.cpp
        System/Threading/XAsync.cpp
        System/Threading/XTaskQueue.cpp )

if(WIN32)
    set_target_properties(xgameruntime PROPERTIES
            PREFIX "" )
endif()

set_target_properties(xgameruntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )

target_link_libraries(xgameruntime PRIVATE
        xgameruntime-common
        runtimeobject
        ws2_32
        LibXml2 )

target_link_options(xgameruntime PRIVATE
        -static
        -static-libgcc
        -static-libstdc++ )

target_include_directories(xgameruntime
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDL_BINARY_DIR} )

target_compile_features(xgameruntime
        PUBLIC
        cxx_std_23 )