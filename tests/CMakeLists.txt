add_library(mcd_test_physical MODULE TestPhysicalPlugin.cpp test_physical.json)
target_link_libraries(mcd_test_physical PRIVATE mcd_sdk)
if(CMAKE_CONFIGURATION_TYPES)
    set(test_plugin_dir "${MCD_OUTPUT_DIR}/$<CONFIG>/test-plugins")
else()
    set(test_plugin_dir "${MCD_OUTPUT_DIR}/test-plugins")
endif()
set_target_properties(mcd_test_physical PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${test_plugin_dir}" RUNTIME_OUTPUT_DIRECTORY "${test_plugin_dir}")

function(mcd_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE mcd_core Qt6::Test Qt6::Widgets)
    target_compile_definitions(${target} PRIVATE
        MCD_PLUGIN_ROOT="$<TARGET_FILE_DIR:mcd_app>/plugins"
        MCD_TEST_PLUGIN_ROOT="$<TARGET_FILE_DIR:mcd_test_physical>")
    add_dependencies(${target} mcd_app mcd_test_physical serial_generic usb_raw_linux usb_raw_win
        raw_passthrough can_frame raw_viewer time_chart gauge raw_control slider_widget)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
endfunction()
mcd_test(mcd_tests tst_reliability.cpp)
mcd_test(mcd_sessions tst_sessions.cpp)
mcd_test(mcd_storage tst_storage.cpp)
mcd_test(mcd_usb_worker tst_usb_worker.cpp)
add_test(NAME plugins_smoke COMMAND mcd_app --smoke-test)
set_tests_properties(plugins_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
