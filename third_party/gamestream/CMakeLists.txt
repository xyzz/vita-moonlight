cmake_minimum_required(VERSION 3.10)

add_library(gamestream STATIC
    client.cpp
    http.cpp
    xml.cpp
    sps.cpp
)

set_target_properties(gamestream PROPERTIES CXX_STANDARD 17)

target_include_directories(gamestream PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src
    ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon
    ${BOREALIS_LIBRARY}/include
    ${CMAKE_SOURCE_DIR}/moonlight/include
    ${CMAKE_SOURCE_DIR}/moonlight/src/crypto
    ${CMAKE_SOURCE_DIR}/third_party/h264bitstream
)

target_link_libraries(gamestream PUBLIC
    borealis
    moonlight-common-c
    ${CURL_LIBRARIES}
    ${EXPAT_LIBRARIES}
)

target_compile_definitions(gamestream PRIVATE USE_MBEDTLS_CRYPTO)
