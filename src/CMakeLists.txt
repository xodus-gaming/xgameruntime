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
        System/XThreading.cpp )

# System/Threading (based on libHttpClient)
target_sources(xgameruntime-impl PRIVATE
        System/Threading/ThreadPool.cpp
        System/Threading/WaitTimer.cpp
        System/Threading/XAsync.cpp
        System/Threading/XTaskQueue.cpp )

target_link_libraries( xgameruntime-impl PRIVATE
        xgameruntime-common )

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
