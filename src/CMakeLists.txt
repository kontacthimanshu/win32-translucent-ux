find_package(wil CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# te_core: everything except the process entry point, so unit and integration
# tests can link the application code without creating the main window.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE TE_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM TE_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app/wWinMain.cpp")

add_library(te_core STATIC ${TE_CORE_SOURCES})
target_include_directories(te_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(te_core
    PUBLIC
        te_defs
        WIL::WIL
        nlohmann_json::nlohmann_json
        dwmapi
        d2d1
        dwrite
        d3d11
        dxgi
        dcomp
        windowscodecs
        uiautomationcore
        shlwapi
        pathcch
        comctl32
        propsys
        runtimeobject
    PRIVATE
        te_warnings
)

# ---------------------------------------------------------------------------
# TranslucentExplorer.exe: the WIN32-subsystem application.
# The manifest (PerMonitorV2, longPathAware, Common Controls v6, supportedOS)
# is listed as a source, so the linker embeds it (/MANIFEST:EMBED with
# /MANIFESTINPUT) together with the default UAC entry.
# ---------------------------------------------------------------------------
add_executable(TranslucentExplorer WIN32
    app/wWinMain.cpp
    "${PROJECT_SOURCE_DIR}/resources/TranslucentExplorer.rc"
    "${PROJECT_SOURCE_DIR}/resources/TranslucentExplorer.manifest"
)
target_include_directories(TranslucentExplorer PRIVATE "${PROJECT_SOURCE_DIR}/resources")
target_link_libraries(TranslucentExplorer PRIVATE te_core te_warnings)
