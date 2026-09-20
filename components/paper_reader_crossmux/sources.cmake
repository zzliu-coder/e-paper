# Shared source list for ESP-IDF and native acceptance.
set(CROSSMUX_ROOT "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB CM_HYPH "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/hyphenation/*.cpp")
set(CROSSMUX_SOURCES
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/css/CssParser.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/Section.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/Page.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/ParsedText.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/htmlEntities.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/blocks/TextBlock.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/converters/ImageDimsProbe.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/MiniBidi/BidiUtils.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/MiniBidi/minibidi.c"
 "${CROSSMUX_ROOT}/vendor/lib/Utf8/Utf8.cpp"
 "${CROSSMUX_ROOT}/vendor/lib/expat/xmlparse.c"
 "${CROSSMUX_ROOT}/vendor/lib/expat/xmltok.c"
 "${CROSSMUX_ROOT}/vendor/lib/expat/xmlrole.c"
 "${CROSSMUX_ROOT}/port/ImageBlock.cpp" ${CM_HYPH})
set(CROSSMUX_INCLUDES "${CROSSMUX_ROOT}/port" "${CROSSMUX_ROOT}/port/Epub/converters"
 "${CROSSMUX_ROOT}/vendor/lib/Epub" "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub"
 "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/css" "${CROSSMUX_ROOT}/vendor/lib/Epub/Epub/converters"
 "${CROSSMUX_ROOT}/vendor/lib/MiniBidi" "${CROSSMUX_ROOT}/vendor/lib/Memory"
 "${CROSSMUX_ROOT}/vendor/lib/Utf8" "${CROSSMUX_ROOT}/vendor/lib/Serialization"
 "${CROSSMUX_ROOT}/vendor/lib/XmlParserUtils" "${CROSSMUX_ROOT}/vendor/lib/expat")
set(CROSSMUX_DEFINITIONS PAPER_CSS_MEMORY_ONLY=1 BOARD_HAS_PSRAM=1 ENABLE_CHINESE_VERSION=1
 XML_GE=1 XML_CONTEXT_BYTES=1024 XML_POOR_ENTROPY=1 BYTEORDER=1234)
