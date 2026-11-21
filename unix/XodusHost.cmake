# Host-compiler targets for the xodus unix library, its test server and the
# host tests. The main build uses the mingw cross compiler, so these are
# plain custom commands running the host C compiler (no Wine headers).
#
#   xodus_add_host_target( <name> SHARED|EXECUTABLE OUTPUT <file> SOURCES <files...> )

find_program( XODUS_HOST_CC NAMES cc clang gcc NO_CMAKE_FIND_ROOT_PATH )

if( NOT XODUS_HOST_CC )
    message( FATAL_ERROR "A host C compiler (cc, clang or gcc) is required for xgameruntime-unix.so" )
endif()

message( STATUS "Using host C compiler: ${XODUS_HOST_CC}" )

function( xodus_add_host_target NAME KIND )
    cmake_parse_arguments( ARG "" "OUTPUT" "SOURCES" ${ARGN} )

    set( flags -Wall -Wextra -O2 -pthread -I ${PROJECT_SOURCE_DIR}/unix )
    if( CMAKE_HOST_APPLE )
        # Wine on Apple Silicon runs x86_64 (Rosetta).
        list( APPEND flags -arch x86_64 )
    endif()
    if( KIND STREQUAL "SHARED" )
        list( APPEND flags -shared -fPIC )
    endif()

    file( GLOB headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/unix/*.h ${PROJECT_SOURCE_DIR}/unix/*.c ${PROJECT_SOURCE_DIR}/tests/unix/*.h )

    get_filename_component( out_dir ${ARG_OUTPUT} DIRECTORY )

    add_custom_command(
            OUTPUT ${ARG_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
            COMMAND ${XODUS_HOST_CC} ${flags} -o ${ARG_OUTPUT} ${ARG_SOURCES}
            DEPENDS ${ARG_SOURCES} ${headers}
            VERBATIM )

    add_custom_target( ${NAME} ALL DEPENDS ${ARG_OUTPUT} )
endfunction()
