cmake_minimum_required( VERSION 3.20 )

set( GDKC_VERSION 10002 )
set( GAMING_SERVICES_VERSION 7874 )

project(xgameruntime
        LANGUAGES C CXX
        VERSION ${GDKC_VERSION}.${GAMING_SERVICES_VERSION} )

include( ExternalProject )

find_program(WIDL
        NAMES
        widl )

if (NOT WIDL)
    find_program(MINGW_WIDL
            NAMES
            x86_64-w64-mingw32-widl )

    if (MINGW_WIDL)
        message(WARNING
                "x86_64-w64-mingw32-widl was found at ${MINGW_WIDL},"
                "but it is not supported. This project requires Wine's widl." )
    endif()

    message(FATAL_ERROR
            "Could not find a supported widl executable."
            "Please install Wine's widl." )
endif()

message( STATUS "Using widl: ${WIDL}" )

set(IDL_SOURCE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/include )

set(IDL_BINARY_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/include )

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

add_custom_target(xgameruntime-idl
        DEPENDS ${IDL_GENERATED_FILES} )

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# We only need libxml2's static library compiled for Windows.
set( LIBXML2_WITH_PYTHON OFF CACHE BOOL "" FORCE )
set( LIBXML2_WITH_TESTS OFF CACHE BOOL "" FORCE )
set( LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE )
set( LIBXML2_WITH_ICONV OFF CACHE BOOL "" FORCE )
set( LIBXML2_WITH_LZMA OFF CACHE BOOL "" FORCE )
set( LIBXML2_WITH_ZLIB OFF CACHE BOOL "" FORCE )
set( BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE )

add_subdirectory(
        ${CMAKE_SOURCE_DIR}/external/libxml2
        ${CMAKE_BINARY_DIR}/external/libxml2
        EXCLUDE_FROM_ALL )

if(ENABLE_TESTS)
    enable_testing()
    message( STATUS "Tests are enabled." )
    add_subdirectory(
            ${CMAKE_SOURCE_DIR}/external/googletest
            ${CMAKE_BINARY_DIR}/external/googletest
            EXCLUDE_FROM_ALL )
    add_subdirectory( tests )
endif()

# ---------------------------------------------------------------------------
# Versioning
# ---------------------------------------------------------------------------
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/include/version.h
        @ONLY )

include_directories( ${PROJECT_SOURCE_DIR}/include )

add_subdirectory( src )
add_subdirectory( libs )