# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.0.0-paper-perf11f")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

# Managed dependencies may be recreated on a clean build. Never silently revert
# the MSC error propagation on which our SD ownership check depends.
if(CONFIG_TINYUSB_MSC_ENABLED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_tinyusb_handoff.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/managed_components/espressif__esp_tinyusb/tinyusb_msc.c"
        RESULT_VARIABLE PAPER_MSC_PATCH_RESULT)
    if(NOT PAPER_MSC_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "TinyUSB handoff overlay needs review")
    endif()
endif()

# Override esp-sr packed model with custom wakeword when present
if(EXISTS "${CMAKE_SOURCE_DIR}/wakeword/srmodels.bin")
    if(TARGET srmodels_bin)
        add_custom_target(custom_wakeword_srmodels ALL
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/wakeword/srmodels.bin"
                "${CMAKE_BINARY_DIR}/srmodels/srmodels.bin"
            COMMENT "Override srmodels.bin with custom wakeword"
        )
        add_dependencies(custom_wakeword_srmodels srmodels_bin)
        if(TARGET flash)
            add_dependencies(flash custom_wakeword_srmodels)
        endif()
        message(STATUS "Custom wakeword enabled: wakeword/srmodels.bin -> build/srmodels/srmodels.bin")
    else()
        message(WARNING "wakeword/srmodels.bin exists but srmodels_bin target not found; custom wakeword override skipped")
    endif()
endif()
