add_executable(mcd_tests tst_reliability.cpp)
target_link_libraries(mcd_tests PRIVATE mcd_core Qt6::Test Qt6::Widgets)
target_compile_definitions(mcd_tests PRIVATE MCD_PLUGIN_ROOT="$<TARGET_FILE_DIR:mcd_app>/plugins")
add_dependencies(mcd_tests mcd_app serial_generic usb_raw_linux usb_raw_win
    raw_passthrough can_frame raw_viewer time_chart gauge raw_control slider_widget)
add_test(NAME reliability COMMAND mcd_tests)
add_test(NAME plugins_smoke COMMAND mcd_app --smoke-test)
set_tests_properties(reliability plugins_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
