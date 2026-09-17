add_library(xgameruntime-common STATIC
        env.c
        logging.c
        os.c )

target_include_directories(xgameruntime-common
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDL_BINARY_DIR} )

if(WIN32)
    set_target_properties(xgameruntime-common PROPERTIES
            PREFIX "" )
endif()