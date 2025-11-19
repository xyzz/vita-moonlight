add_library(enet STATIC
    enet/callbacks.c
    enet/compress.c
    enet/host.c
    enet/list.c
    enet/packet.c
    enet/peer.c
    enet/protocol.c
    enet/unix.c
)

target_include_directories(enet PUBLIC enet/include)

add_library(h264bitstream STATIC
    h264bitstream/h264_nal.c
    h264bitstream/h264_sei.c
    h264bitstream/h264_stream.c
)
target_include_directories(h264bitstream PUBLIC h264bitstream)

add_library(inih STATIC
    inih/ini.c
)
target_include_directories(inih PUBLIC inih)

add_library(libuuid STATIC
    libuuid/src/clear.c
    libuuid/src/compare.c
    libuuid/src/copy.c
    libuuid/src/gen_uuid.c
    libuuid/src/isnull.c
    libuuid/src/pack.c
    libuuid/src/parse.c
    libuuid/src/unpack.c
    libuuid/src/unparse.c
    libuuid/src/uuid_time.c
)
target_include_directories(libuuid PUBLIC libuuid/src)

add_library(moonlight-common-c STATIC
    moonlight-common-c/src/AudioStream.c
    moonlight-common-c/src/ByteBuffer.c
    moonlight-common-c/src/Connection.c
    moonlight-common-c/src/ConnectionTester.c
    moonlight-common-c/src/ControlStream.c
    moonlight-common-c/src/FakeCallbacks.c
    moonlight-common-c/src/InputStream.c
    moonlight-common-c/src/LinkedBlockingQueue.c
    moonlight-common-c/src/Misc.c
    moonlight-common-c/src/Platform.c
    moonlight-common-c/src/PlatformCrypto.c
    moonlight-common-c/src/PlatformSockets.c
    moonlight-common-c/src/RecorderCallbacks.c
    moonlight-common-c/src/RtpAudioQueue.c
    moonlight-common-c/src/RtpVideoQueue.c
    moonlight-common-c/src/RtspConnection.c
    moonlight-common-c/src/RtspParser.c
    moonlight-common-c/src/SdpGenerator.c
    moonlight-common-c/src/SimpleStun.c
    moonlight-common-c/src/VideoDepacketizer.c
    moonlight-common-c/src/VideoStream.c
    moonlight-common-c/reedsolomon/rs.c
)
target_include_directories(moonlight-common-c PUBLIC moonlight-common-c/src moonlight-common-c/reedsolomon)

add_library(mdnsniff STATIC
    mdnsniff/udp_sniffer_vita.c
)
target_include_directories(mdnsniff PUBLIC mdnsniff)
