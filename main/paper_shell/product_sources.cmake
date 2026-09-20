# Included immediately before main's idf_component_register().
if(CONFIG_PAPER_CORE_APP)
    list(FILTER SOURCES EXCLUDE REGEX "(^|/)display/screen/standby_screen/standby_screen\\.cc$")
    list(FILTER SOURCES EXCLUDE REGEX "(^|/)(inkdesk_app|ui_demo|ui_demo_fonts|inkdesk_reader|inkdesk_epub)\\.(c|cc)$")
    list(FILTER SOURCES EXCLUDE REGEX "(^|/)(font_lab_assets|ui/fontbench|ui/fontlab4)/")
    list(APPEND SOURCES
        "paper_shell/inkdesk_bridge.cc"
        "paper_shell/metalio_hardware.cc"
        "paper_shell/network_service.cc"
        "paper_shell/bluetooth_service.cc"
        "paper_shell/maintenance.cc"
        "paper_shell/standby.cc"
        "paper_shell/rescue_fonts.c"
        "paper_shell/gray/driver.c")
    list(APPEND INCLUDE_DIRS "paper_shell")
endif()
