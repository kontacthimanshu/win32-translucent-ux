find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Builds one GoogleTest executable from a folder of test sources and registers
# each test with CTest under the given label. Nothing is created while the
# folder has no sources yet, so the project still configures before the
# first test is written.
function(te_add_test_suite target folder label)
    file(GLOB suite_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${folder}/*.cpp")
    if(NOT suite_sources)
        message(STATUS "No ${label} tests yet in tests/${folder}; skipping ${target}.")
        return()
    endif()

    # The app manifest gives the tests the same process environment as the app:
    # PerMonitorV2 DPI awareness (without it Windows virtualizes coordinates and,
    # for example, DWM caption-button bounds no longer match hit testing),
    # Common Controls v6 and the UTF-8 code page.
    # The resource script too, so components that load dialogs and strings (the
    # appearance popup) can be tested with the real templates.
    add_executable(${target} ${suite_sources} "${PROJECT_SOURCE_DIR}/resources/TranslucentExplorer.manifest"
                   "${PROJECT_SOURCE_DIR}/resources/TranslucentExplorer.rc")
    target_link_libraries(${target} PRIVATE te_core GTest::gtest_main te_warnings te_defs)

    # PRE_TEST discovery lists the tests when CTest runs rather than after every
    # build, which suits the multi-config Visual Studio generator.
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS ${label}
    )
endfunction()

te_add_test_suite(te_unit_tests unit unit)
te_add_test_suite(te_integration_tests integration integration)
