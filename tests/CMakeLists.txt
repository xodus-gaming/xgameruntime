add_executable(test_xgameruntime
        System/test_xthreading.cpp
        test_fixtures.cpp)

target_link_libraries(test_xgameruntime
        PRIVATE
        libxgameruntime
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
        ${CMAKE_SOURCE_DIR}/external/googletest/googletest/include )

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
