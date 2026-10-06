cmake_minimum_required( VERSION 3.20 )

set( GDKC_VERSION 10002 )
set( GAMING_SERVICES_VERSION 7874 )

project(xgameruntime
        LANGUAGES C CXX
        VERSION ${GDKC_VERSION}.${GAMING_SERVICES_VERSION} )

include( ExternalProject )

find_program(WIDL
        NAMES
        widl
        x86_64-w64-mingw32-widl
        REQUIRED )

message( STATUS "Using widl: ${WIDL}" )

set(IDL_SOURCE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/include )

set(IDL_BINARY_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/include )

include_directories( ${PROJECT_SOURCE_DIR}/include )

if(ENABLE_TESTS)
    enable_testing()
    message( STATUS "Tests are enabled." )
    add_subdirectory(
            ${CMAKE_SOURCE_DIR}/external/googletest
            ${CMAKE_BINARY_DIR}/external/googletest
            EXCLUDE_FROM_ALL )
    add_subdirectory( tests )
endif()

add_subdirectory( libs )
add_subdirectory( src )