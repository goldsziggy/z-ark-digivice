cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(digivice)

# Apply to the SDK parser itself: callback limits alone do not bound allocations
# while fragmented headers are being accumulated. Verified in IDF 5.3.x source; first compile candidate is 5.3.6.
idf_component_get_property(digivice_http_parser_lib http_parser COMPONENT_LIB)
target_compile_definitions(${digivice_http_parser_lib} PRIVATE HTTP_MAX_HEADER_SIZE=4096)
