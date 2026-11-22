add_executable(test_xgameruntime
        System/test_gameconfig.cpp
        System/test_xgame.cpp
        System/test_xgame_integration.cpp
        System/test_xthreading.cpp
        ${CMAKE_SOURCE_DIR}/src/GameConfig.cpp
        ${CMAKE_SOURCE_DIR}/src/System/XGame.cpp
        test_fixtures.cpp)

target_link_libraries(test_xgameruntime
        PRIVATE
        libxgameruntime
        xmllite
        ole32
        GTest::gtest_main )

target_link_options(test_xgameruntime PRIVATE
        -static
        -static-libgcc
        -static-libstdc++ )

target_compile_features(test_xgameruntime
        PUBLIC
        cxx_std_23 )

include( GoogleTest )

target_include_directories(test_xgameruntime
        PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/googletest/googletest/include )

# The default MicrosoftGame.config lookup starts at the test executable.
add_custom_command( TARGET test_xgameruntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/System/fixtures/MicrosoftGame.config
                $<TARGET_FILE_DIR:test_xgameruntime> )

# For discovery purposes.
set_target_properties(test_xgameruntime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )

if( CMAKE_CROSSCOMPILING )
    message( STATUS "We are cross compiling tests" )
    find_program( WINE_EXECUTABLE wine )
    message( STATUS "Using wine: ${WINE_EXECUTABLE}" )
    if( NOT WINE_EXECUTABLE )
        message( FATAL_ERROR "Wine is required to run Windows tests on non-Windows hosts" )
    endif()
    set_property(
            TARGET test_xgameruntime
            PROPERTY CROSSCOMPILING_EMULATOR
            "${WINE_EXECUTABLE}" )
endif()

# We should not be pre-running tests during build.
gtest_discover_tests(
        test_xgameruntime
        DISCOVERY_MODE PRE_TEST )
