# ------------------------------------------------------------
# IDL generation
# ------------------------------------------------------------

file(GLOB_RECURSE IDL_FILES
        CONFIGURE_DEPENDS
        ${IDL_SOURCE_DIR}/*.idl )

set(IDL_GENERATED_FILES)

foreach(IDL_FILE ${IDL_FILES})
    file(RELATIVE_PATH IDL_RELATIVE_PATH
            ${IDL_SOURCE_DIR}
            ${IDL_FILE} )

    get_filename_component(IDL_RELATIVE_DIR
            ${IDL_RELATIVE_PATH}
            DIRECTORY )

    get_filename_component(IDL_NAME
            ${IDL_RELATIVE_PATH}
            NAME_WE )

    set(GENERATED_DIR
            ${IDL_BINARY_DIR}/${IDL_RELATIVE_DIR} )

    set(GENERATED_H
            ${GENERATED_DIR}/${IDL_NAME}.h )

    add_custom_command(
            OUTPUT
            ${GENERATED_H}

            COMMAND
            ${CMAKE_COMMAND} -E make_directory
            ${GENERATED_DIR}

            COMMAND
            ${WIDL}
            -h
            -o ${GENERATED_H}
            ${IDL_FILE}

            DEPENDS
            ${IDL_FILE}

            VERBATIM )

    list(APPEND IDL_GENERATED_FILES
            ${GENERATED_H})
endforeach()

# ------------------------------------------------------------
# xgameruntime-impl
# ------------------------------------------------------------

add_library(xgameruntime-impl STATIC
        main.cpp
        ${IDL_GENERATED_FILES} )

# System
target_sources(xgameruntime-impl PRIVATE
        System/XThreading.cpp )

# System/Threading (based on libHttpClient)
target_sources(xgameruntime-impl PRIVATE
        System/Threading/ThreadPool.cpp
        System/Threading/WaitTimer.cpp
        System/Threading/XAsync.cpp
        System/Threading/XTaskQueue.cpp )

if(WIN32)
    set_target_properties(xgameruntime-impl PROPERTIES
            PREFIX "" )
endif()

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

target_link_libraries( xgameruntime PUBLIC
        xgameruntime-impl )

set_target_properties(xgameruntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LINKER_LANGUAGE CXX )

target_link_options(xgameruntime PRIVATE
        -static
        -static-libgcc
        -static-libstdc++ )
