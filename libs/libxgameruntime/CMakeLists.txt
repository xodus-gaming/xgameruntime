add_library(libxgameruntime STATIC
        Init.cpp
        Error.cpp
        Impl.cpp)

target_link_libraries(libxgameruntime PRIVATE
        xgameruntime-common )

add_dependencies(libxgameruntime
        xgameruntime-idl )

target_include_directories(libxgameruntime
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${IDL_BINARY_DIR} )

if(WIN32)
    set_target_properties(libxgameruntime PROPERTIES
            PREFIX "" )
endif()