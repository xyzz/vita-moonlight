cmake_minimum_required(VERSION 3.10)

# Definir la biblioteca estática libgamestream
add_library(libgamestream STATIC
    client.cpp
    http.cpp
    xml.cpp
    sps.cpp
)

# Establecer estándar C++17
set_target_properties(libgamestream PROPERTIES CXX_STANDARD 17)

# Incluir directorios necesarios
target_include_directories(libgamestream PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src
    ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon
    ${CMAKE_SOURCE_DIR}/third_party/borealis/library/include
    ${CMAKE_SOURCE_DIR}/moonlight/include
    ${CMAKE_SOURCE_DIR}/moonlight/src/crypto
    ${CMAKE_SOURCE_DIR}/third_party/h264bitstream
)

# Enlazar dependencias
target_link_libraries(libgamestream PUBLIC
    borealis
    moonlight-common-c
    ${CURL_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# Definiciones de compilación
if(BUILD_FFMPEG)
    target_compile_definitions(libgamestream PRIVATE USE_MBEDTLS_CRYPTO)
else()
    target_compile_definitions(libgamestream PRIVATE USE_OPENSSL_CRYPTO)
endif()
