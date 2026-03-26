

# ENET ahora es gestionado por moonlight-common-c como submódulo y no debe ser compilado aquí




# moonlight-common-c static library (con opción de OpenSSL/MbedTLS)
# La opción USE_MBEDTLS debe ser definida en el CMakeLists.txt principal si se desea usar MbedTLS
add_subdirectory(moonlight-common-c)


# h264bitstream static library
add_library(h264bitstream STATIC
    h264bitstream/h264_nal.c
    h264bitstream/h264_sei.c
    h264bitstream/h264_stream.c
)
target_include_directories(h264bitstream PUBLIC h264bitstream)

# inih static library
add_library(inih STATIC
    inih/ini.c
)
target_include_directories(inih PUBLIC inih)

add_subdirectory(borealis/library)

# gamestream static library
add_subdirectory(gamestream)

# libmoonmic - Microphone capture and transmission library
add_subdirectory(libmoonmic)

# mdnsniff portable library (Vita y Windows)
add_library(mdnsniff STATIC
    mdnsniff/udp_sniffer_vita.c
    mdnsniff/udp_sniffer_win.c
    mdnsniff/common/debugScreen.c
)
target_include_directories(mdnsniff PUBLIC mdnsniff mdnsniff/common)

# Enlazar stubs de Vita solo si se compila para PSVita
if(CMAKE_SYSTEM_NAME STREQUAL "Vita" OR PLATFORM_PSV)
    target_link_libraries(mdnsniff PUBLIC SceSysmodule_stub SceNet_stub SceNetCtl_stub SceCtrl_stub SceDisplay_stub)
endif()
