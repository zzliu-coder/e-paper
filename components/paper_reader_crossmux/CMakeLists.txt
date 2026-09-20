include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")
idf_component_register(SRCS ${CROSSMUX_SOURCES} INCLUDE_DIRS ${CROSSMUX_INCLUDES} REQUIRES freertos)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${CROSSMUX_DEFINITIONS})
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
