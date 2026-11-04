cmake_minimum_required(VERSION 3.19)
project(openrgb_ambient VERSION 4.0.3)
set(OPENRGB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/OpenRGB" CACHE PATH "Matching OpenRGB source headers")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (NOT QT_VERSION)
    set(QT_VERSION 5)
endif()

set(REQUIRED_LIBS Core Gui Widgets Network)
set(REQUIRED_LIBS_QUALIFIED Qt${QT_VERSION}::Core Qt${QT_VERSION}::Gui Qt${QT_VERSION}::Widgets Qt${QT_VERSION}::Network)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

# Plugin metadata, shared between the Qt plugin metadata JSON - which OpenRGB reads before loading the
# library - and GetPluginInfo().
set(PLUGIN_ID "com.github.krojew.OpenRGBAmbient")
set(PLUGIN_NAME "OpenRGBAmbientPlugin")
set(PLUGIN_DESCRIPTION "Desktop ambient light support")
set(PLUGIN_URL "https://github.com/krojew/OpenRGB-Ambient")
set(PLUGIN_VERSION ${PROJECT_VERSION})

# OpenRGB skips any plugin whose JSON API version differs from the headers it was built against, so take
# the value straight from those headers instead of duplicating it.
file(STRINGS "${OPENRGB_SOURCE_DIR}/OpenRGBPluginInterface.h" PLUGIN_API_VERSION_DEFINE
        REGEX "^#define[ \t]+OPENRGB_PLUGIN_API_VERSION[ \t]+[0-9]+")
if (NOT PLUGIN_API_VERSION_DEFINE)
    message(FATAL_ERROR "Could not read OPENRGB_PLUGIN_API_VERSION from OpenRGB/OpenRGBPluginInterface.h")
endif ()
string(REGEX REPLACE "^#define[ \t]+OPENRGB_PLUGIN_API_VERSION[ \t]+([0-9]+).*$" "\\1"
        PLUGIN_API_VERSION "${PLUGIN_API_VERSION_DEFINE}")

configure_file(OpenRGBAmbientPlugin.json.in OpenRGBAmbientPlugin.json @ONLY)
configure_file(PluginMetadata.h.in PluginMetadata.h @ONLY)
file(READ "${CMAKE_SOURCE_DIR}/OfficeSampling.hlsl" OFFICE_SAMPLING_SHADER)
configure_file(OfficeSamplingShader.h.in OfficeSamplingShader.h @ONLY)

add_library(${PROJECT_NAME} SHARED OpenRGBAmbientPlugin.h OpenRGBAmbientPlugin.cpp DeviceList.cpp DeviceList.h Settings.cpp Settings.h ScreenCapture.cpp ScreenCapture.h Limiter.cpp Limiter.h ReleaseWrapper.h SettingsTab.cpp SettingsTab.h RegionsWidget.cpp RegionsWidget.h RegionWidget.cpp RegionWidget.h LedUpdateEvent.h LedUpdateEvent.cpp ImageProcessor.h LedRange.h ZoneMapping.h SdrHorizontalRegionProcessor.h SdrVerticalRegionProcessor.h ColorConversion.h HdrVerticalRegionProcessor.h HdrHorizontalRegionProcessor.h ColorPostProcessor.h LedPreviewWidget.h LedPreviewWidget.cpp)
target_sources(${PROJECT_NAME} PRIVATE OfficeAmbience.cpp OfficeAmbience.h)

if (WIN32)
    add_definitions(-D_MBCS -DWIN32 -D_CRT_SECURE_NO_WARNINGS -D_WINSOCKAPI_ -D_WINSOCK_DEPRECATED_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    target_include_directories(${PROJECT_NAME} PUBLIC "${OPENRGB_SOURCE_DIR}/dependencies/hidapi-win/include")
endif()

if (NOT CMAKE_PREFIX_PATH)
    message(WARNING "CMAKE_PREFIX_PATH is not defined, you may need to set it "
            "(-DCMAKE_PREFIX_PATH=\"path/to/Qt/lib/cmake\" or -DCMAKE_PREFIX_PATH=/usr/include/{host}/qt{version}/ on Ubuntu)")
endif ()

if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    include("${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

find_package(Qt${QT_VERSION} COMPONENTS ${REQUIRED_LIBS} REQUIRED)
find_package(DirectX11)
target_link_libraries(${PROJECT_NAME} ${REQUIRED_LIBS_QUALIFIED})
foreach(directory "" i2c_smbus net_port RGBController dependencies/json qt hidapi_wrapper SPDAccessor serial_port)
    target_include_directories(${PROJECT_NAME} PRIVATE "${OPENRGB_SOURCE_DIR}/${directory}")
endforeach()

add_executable(OfficeControl WIN32 OfficeControl.cpp OfficeLayout.h)
target_include_directories(OfficeControl PRIVATE "${OPENRGB_SOURCE_DIR}/dependencies/json")
target_link_libraries(OfficeControl PRIVATE ${REQUIRED_LIBS_QUALIFIED})

include(CTest)
if(BUILD_TESTING AND WIN32)
    add_executable(office_capture_tests tests/office_capture_tests.cpp)
    target_include_directories(office_capture_tests PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
    target_compile_definitions(office_capture_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCKAPI_)
    target_link_libraries(office_capture_tests PRIVATE ${REQUIRED_LIBS_QUALIFIED} d3d11 dxgi windowsapp d3dcompiler dwmapi wtsapi32)
    add_test(NAME office_capture COMMAND office_capture_tests)
    add_executable(office_lifecycle_tests tests/office_lifecycle_tests.cpp)
    target_include_directories(office_lifecycle_tests PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},INCLUDE_DIRECTORIES>)
    target_compile_definitions(office_lifecycle_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCKAPI_)
    target_link_libraries(office_lifecycle_tests PRIVATE ${REQUIRED_LIBS_QUALIFIED} d3d11 dxgi windowsapp d3dcompiler dwmapi wtsapi32)
    add_test(NAME office_lifecycle COMMAND office_lifecycle_tests)
    add_executable(office_layout_tests tests/office_layout_tests.cpp)
    target_include_directories(office_layout_tests PRIVATE "${OPENRGB_SOURCE_DIR}/dependencies/json")
    add_test(NAME office_layout COMMAND office_layout_tests)
    add_executable(office_widget_tests tests/office_widget_tests.cpp)
    target_include_directories(office_widget_tests PRIVATE "${OPENRGB_SOURCE_DIR}/dependencies/json")
    target_link_libraries(office_widget_tests PRIVATE ${REQUIRED_LIBS_QUALIFIED})
    add_test(NAME office_widgets COMMAND office_widget_tests -platform offscreen)
    add_executable(office_control_tests tests/office_control_tests.cpp)
    target_include_directories(office_control_tests PRIVATE "${OPENRGB_SOURCE_DIR}/dependencies/json")
    target_link_libraries(office_control_tests PRIVATE Qt${QT_VERSION}::Core Qt${QT_VERSION}::Network)
    add_test(NAME office_control COMMAND office_control_tests)
endif()

if (WIN32)
    target_link_libraries(${PROJECT_NAME} ${DirectX11_LIBRARY} windowsapp d3dcompiler dwmapi wtsapi32)

    set(DEBUG_SUFFIX)
    if (CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    # App-owned rendered validation without taking control of the host desktop.
    if(EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qoffscreen${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${QT_INSTALL_PATH}/plugins/platforms/qoffscreen${DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif()
    foreach (QT_LIB ${REQUIRED_LIBS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt${QT_VERSION}${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()
