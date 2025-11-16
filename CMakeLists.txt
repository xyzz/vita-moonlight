cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
	message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()
add_compile_options(-g3 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign -Wno-sign-compare -Wno-switch)


project(moonlight)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
# NOTE: To update the version shown in the UI and VPK, edit the following lines:
#   set(VERSION_MAJOR "0")
#   set(VERSION_MINOR "11")
#   set(VERSION_PATCH "2")
#
set(TITLE_ID "XYZZ00002")
set(TITLE "Moonlight")
set(VERSION_MAJOR "0")
set(VERSION_MINOR "13")
set(VERSION_PATCH "2")

execute_process(COMMAND
	python -c "print('%02d.%02d' % (${VERSION_MAJOR}, ${VERSION_MINOR}))"
	OUTPUT_VARIABLE APP_VER
)
string(REPLACE "\n" "" APP_VER "${APP_VER}")

add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

set(CMAKE_C_FLAGS "-Wl,-q -g -std=c99 -D__vita__")



include_directories(
	include/
	src/
	third_party/moonlight-common-c/src/
	third_party/moonlight-common-c/reedsolomon/
	libgamestream/
	third_party/libuuid/src/
	third_party/h264bitstream/
	third_party/enet/include/
	third_party/inih/
	third_party/mdnsniff/
)


add_executable(${PROJECT_NAME}.elf
	src/config.c
	src/input/mapping.c
	src/connection.c
	src/debug.c
	src/main.c
	src/platform.c
	src/util.c
	src/device.c
	src/check_dir.c
	src/wake_on_lan.c
	src/audio/vita.c
	src/video/vita.c
	src/input/vita.c
	src/input/shortcuts.c
	src/power/vita.c
	src/input/motion.c
	src/graphics.c
	src/font.c
	src/gui/guilib.c
	src/gui/ime.c
	src/gui/ui.c
	src/gui/ui_settings.c
	src/gui/ui_connect.c
	src/gui/ui_device.c
	src/gui/ui_hotkey.c
	src/gui/hotkey.c
	src/gui/ui_check.c

	libgamestream/client.c
	libgamestream/http.c
	libgamestream/mkcert.c
	libgamestream/sps.c
	libgamestream/xml.c

	third_party/libuuid/src/clear.c
	third_party/libuuid/src/compare.c
	third_party/libuuid/src/copy.c
	third_party/libuuid/src/gen_uuid.c
	third_party/libuuid/src/isnull.c
	third_party/libuuid/src/pack.c
	third_party/libuuid/src/parse.c
	third_party/libuuid/src/unpack.c
	third_party/libuuid/src/unparse.c

	third_party/h264bitstream/h264_nal.c
	third_party/h264bitstream/h264_sei.c
	third_party/h264bitstream/h264_stream.c

	third_party/moonlight-common-c/src/AudioStream.c
	third_party/moonlight-common-c/src/ByteBuffer.c
	third_party/moonlight-common-c/src/Connection.c
	third_party/moonlight-common-c/src/ConnectionTester.c
	third_party/moonlight-common-c/src/ControlStream.c
	third_party/moonlight-common-c/src/FakeCallbacks.c
	third_party/moonlight-common-c/src/InputStream.c
	third_party/moonlight-common-c/src/LinkedBlockingQueue.c
	third_party/moonlight-common-c/src/Misc.c
	third_party/moonlight-common-c/src/Platform.c
	third_party/moonlight-common-c/src/PlatformCrypto.c
	third_party/moonlight-common-c/src/PlatformSockets.c
	third_party/moonlight-common-c/src/RecorderCallbacks.c
	third_party/moonlight-common-c/src/RtpAudioQueue.c
	third_party/moonlight-common-c/src/RtpVideoQueue.c
	third_party/moonlight-common-c/src/RtspConnection.c
	third_party/moonlight-common-c/src/RtspParser.c
	third_party/moonlight-common-c/src/SdpGenerator.c
	third_party/moonlight-common-c/src/SimpleStun.c
	third_party/moonlight-common-c/src/VideoDepacketizer.c
	third_party/moonlight-common-c/src/VideoStream.c
	third_party/moonlight-common-c/reedsolomon/rs.c

	third_party/enet/callbacks.c
	third_party/enet/compress.c
	third_party/enet/host.c
	third_party/enet/list.c
	third_party/enet/packet.c
	third_party/enet/peer.c
	third_party/enet/protocol.c
	third_party/enet/unix.c

	third_party/inih/ini.c

	src/keyboardsystem.c
	src/input/keyboardkeys.c
	src/gui/ui_keyboard.c
	src/input/touchabsolute.c
	src/check_host.c
)


# Por defecto, solo enlazamos curl y el resto de libs estándar:
target_link_libraries(${PROJECT_NAME}.elf
	-lexpat
	-lcurl
	moonlight-common-c
	enet
	h264bitstream
	inih
	libuuid
	mdnsniff
	-lz
	-lssl
	-lcrypto
	-lopus
	-lbz2
	-lpthread
	-lSceDisplay_stub
	-lSceCtrl_stub
	-lSceTouch_stub
	-lSceNet_stub
	-lSceNetCtl_stub
	-lScePower_stub
	-lSceVideodec_stub
	-lSceCommonDialog_stub
	-lSceShellSvc_stub
	-lSceAppMgr_stub
	-lSceAudio_stub
	-lSceIme_stub
	-lSceMotion_stub
	-lvita2d
	-lSceGxm_stub
	-lScePgf_stub
	-lSceCommonDialog_stub
	-lfreetype
	-lpng
	-ljpeg
	-lz
	-lm
	-lc
)

add_custom_target(${PROJECT_NAME}.velf ALL
	COMMAND vita-elf-create ${PROJECT_NAME}.elf ${PROJECT_NAME}.velf
)

add_custom_target(param.sfo ALL   
	COMMAND vita-mksfoex -s TITLE_ID=${TITLE_ID} -s APP_VER=${APP_VER} "${TITLE}" param.sfo
)

# Actualizar versión en template.xml automáticamente antes de empaquetar el VPK (solo la línea visual)
add_custom_target(update_template_version ALL
	COMMAND sed -i '/<str size=\"18.*>v[0-9]*\.[0-9]*\.[0-9]*<\\/str>/s/v[0-9]*\.[0-9]*\.[0-9]*/v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}/' sce_sys/livearea/contents/template.xml
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(${PROJECT_NAME}.vpk ALL
	COMMAND vita-make-fself -a 0x2800000000000001 -at 0x0E -c ${PROJECT_NAME}.velf eboot.bin
	COMMAND vita-pack-vpk -s param.sfo -b eboot.bin
			--add ../sce_sys/icon0.png=sce_sys/icon0.png
			--add ../sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
			--add ../sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
			--add ../sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml
			--add ../assets/nerdfont.ttf=assets/nerdfont.ttf
			${PROJECT_NAME}.vpk
)

add_dependencies(${PROJECT_NAME}.velf ${PROJECT_NAME}.elf)
add_dependencies(${PROJECT_NAME}.vpk ${PROJECT_NAME}.velf param.sfo update_template_version)

add_subdirectory(third_party)

option(USE_DIR_UMA0 "Forzar uso de uma0:data/moonlight como directorio de claves/config" OFF)
if(USE_DIR_UMA0)
  add_definitions(-DUSE_DIR_UMA0)
endif()


