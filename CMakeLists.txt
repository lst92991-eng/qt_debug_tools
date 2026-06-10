cmake_minimum_required(VERSION 3.20)

project(MCUDebugTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)

set(MCD_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${MCD_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${MCD_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(MCD_SDK_HEADERS
    src/sdk/DataFrame.h
    src/sdk/IControlPlugin.h
    src/sdk/IPhysicalPlugin.h
    src/sdk/IProtocolPlugin.h
    src/sdk/IVisualPlugin.h
)

add_library(mcd_sdk STATIC
    src/sdk/DataFrame.cpp
    ${MCD_SDK_HEADERS}
)
target_include_directories(mcd_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mcd_sdk PUBLIC Qt6::Core Qt6::Widgets)

add_library(mcd_core STATIC
    src/core/ChannelHub.cpp
    src/core/ChannelHub.h
    src/core/DebugCore.cpp
    src/core/DebugCore.h
    src/core/PluginManager.cpp
    src/core/PluginManager.h
    src/core/RingBufferPool.cpp
    src/core/RingBufferPool.h
)
target_include_directories(mcd_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mcd_core PUBLIC mcd_sdk Qt6::Core Qt6::Widgets)

add_executable(mcd_app
    src/app/DeviceConfigDialog.cpp
    src/app/DeviceConfigDialog.h
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/main.cpp
)
target_link_libraries(mcd_app PRIVATE mcd_core Qt6::Core Qt6::Widgets Qt6::SerialPort)
add_custom_command(TARGET mcd_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::SerialPort>"
        "$<TARGET_FILE_DIR:mcd_app>"
)

function(mcd_add_plugin target category)
    add_library(${target} MODULE ${ARGN})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target} PRIVATE mcd_sdk Qt6::Core Qt6::Widgets)

    set(plugin_output_dir "${MCD_OUTPUT_DIR}/$<CONFIG>/plugins/${category}")
    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${plugin_output_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${plugin_output_dir}"
        PREFIX ""
    )

    foreach(plugin_file IN LISTS ARGN)
        if(plugin_file MATCHES "\\.json$")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/${plugin_file}"
                    "$<TARGET_FILE_DIR:${target}>"
            )
        endif()
    endforeach()
endfunction()

mcd_add_plugin(serial_generic physical
    src/plugins/physical/serial_generic/SerialGenericPlugin.cpp
    src/plugins/physical/serial_generic/SerialGenericPlugin.h
    src/plugins/physical/serial_generic/serial_generic.json
)
target_link_libraries(serial_generic PRIVATE Qt6::SerialPort)

mcd_add_plugin(usb_raw_linux physical
    src/plugins/physical/usb_raw_linux/UsbRawLinuxPlugin.cpp
    src/plugins/physical/usb_raw_linux/UsbRawLinuxPlugin.h
    src/plugins/physical/usb_raw_linux/usb_raw_linux.json
)
if(UNIX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB QUIET libusb-1.0)
        if(LIBUSB_FOUND)
            target_compile_definitions(usb_raw_linux PRIVATE MCD_HAVE_LIBUSB=1)
            target_include_directories(usb_raw_linux PRIVATE ${LIBUSB_INCLUDE_DIRS})
            target_link_libraries(usb_raw_linux PRIVATE ${LIBUSB_LIBRARIES})
            target_link_directories(usb_raw_linux PRIVATE ${LIBUSB_LIBRARY_DIRS})
        endif()
    endif()
endif()

mcd_add_plugin(usb_raw_win physical
    src/plugins/physical/usb_raw_win/UsbRawWinPlugin.cpp
    src/plugins/physical/usb_raw_win/UsbRawWinPlugin.h
    src/plugins/physical/usb_raw_win/usb_raw_win.json
)
if(WIN32)
    target_link_libraries(usb_raw_win PRIVATE setupapi winusb)
endif()

mcd_add_plugin(raw_passthrough protocol
    src/plugins/protocol/raw_passthrough/RawPassthroughPlugin.cpp
    src/plugins/protocol/raw_passthrough/RawPassthroughPlugin.h
    src/plugins/protocol/raw_passthrough/raw_passthrough.json
)

mcd_add_plugin(can_frame protocol
    src/plugins/protocol/can_frame/CanFramePlugin.cpp
    src/plugins/protocol/can_frame/CanFramePlugin.h
    src/plugins/protocol/can_frame/can_frame.json
)

mcd_add_plugin(raw_viewer visual
    src/plugins/visual/raw_viewer/RawViewerPlugin.cpp
    src/plugins/visual/raw_viewer/RawViewerPlugin.h
    src/plugins/visual/raw_viewer/raw_viewer.json
)

mcd_add_plugin(time_chart visual
    src/plugins/visual/time_chart/TimeChartPlugin.cpp
    src/plugins/visual/time_chart/TimeChartPlugin.h
    src/plugins/visual/time_chart/time_chart.json
)

mcd_add_plugin(gauge visual
    src/plugins/visual/gauge/GaugePlugin.cpp
    src/plugins/visual/gauge/GaugePlugin.h
    src/plugins/visual/gauge/gauge.json
)

mcd_add_plugin(raw_control control
    src/plugins/control/raw_control/RawControlPlugin.cpp
    src/plugins/control/raw_control/RawControlPlugin.h
    src/plugins/control/raw_control/raw_control.json
)

mcd_add_plugin(slider_widget control
    src/plugins/control/slider_widget/SliderWidgetPlugin.cpp
    src/plugins/control/slider_widget/SliderWidgetPlugin.h
    src/plugins/control/slider_widget/slider_widget.json
)
