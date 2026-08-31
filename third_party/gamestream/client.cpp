/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "client.h"

#include <Limelight.h>
#include <errno.h>
#include <fmt/format.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <vector>

#include "ConfigManager.hpp"
#include "CryptoManager.hpp"
#include "debug.hpp"
#include "errors.h"
#include "http.h"

#define CHANNEL_COUNT_STEREO 2
#define CHANNEL_COUNT_51_SURROUND 6

#define CHANNEL_MASK_STEREO 0x3
#define CHANNEL_MASK_51_SURROUND 0xFC

#define UNIQUE_FILE_NAME "uniqueid.dat"
#define UNIQUEID_CHARS 16
static std::string unique_id;

static int load_unique_id(const std::string& keyDirectory)
{
    std::string uniqueFilePath = keyDirectory + "/" + UNIQUE_FILE_NAME;
    FILE* fd                   = fopen(uniqueFilePath.c_str(), "r");
    unique_id.resize(UNIQUEID_CHARS);
    bool fromFile = false;
    if (fd != NULL)
    {
        if (fread(&unique_id[0], UNIQUEID_CHARS, 1, fd) == 1)
        {
            fromFile = true;
        }
        fclose(fd);
    }
    if (!fromFile)
    {
        unique_id = "0123456789ABCDEF";
        FILE* wfd = fopen(uniqueFilePath.c_str(), "w");
        if (wfd == NULL)
        {
            vita_log::error("[load_unique_id] no se pudo crear/abrir '{}' para escribir uniqueid", uniqueFilePath);
            return GS_FAILED;
        }
        fwrite(unique_id.c_str(), UNIQUEID_CHARS, 1, wfd);
        fclose(wfd);
    }
    return GS_OK;
}

int extractVersionQuadFromString(const char* string, int* quad)
{
    if (!string)
    {
        // If string is NULL, set all quads to 0
        for (int i = 0; i < 4; i++)
        {
            quad[i] = 0;
        }
        return -1;
    }
    const char* nextNumber = string;
    for (int i = 0; i < 4; i++)
    {
        // Parse the next component
        quad[i] = (int)strtol(nextNumber, (char**)&nextNumber, 10);

        // Skip the dot if we still have version components left.
        //
        // We continue looping even when we're at the end of the
        // input string to ensure all subsequent version components
        // are zeroed.
        if (*nextNumber != 0)
        {
            nextNumber++;
        }
    }

    return 0;
}

bool _SERVER_DATA::isSunshine()
{
    if (serverInfoAppVersion.empty())
    {
        return false;
    }
    int AppVersionQuad[4];
    extractVersionQuadFromString(serverInfoAppVersion.c_str(), AppVersionQuad);
    return AppVersionQuad[3] < 0;
}

static int load_serverinfo(PSERVER_DATA server, bool https)
{
    int ret = GS_INVALID;
    char url[4096];
    snprintf(url, sizeof(url), "%s://%s:%d/serverinfo?uniqueid=%s",
        https ? "https" : "http", server->serverInfo.address,
        https ? server->httpsPort : server->httpPort, unique_id.c_str());

    Data data;
    // TLS setup is noticeably slower on the Vita network stack.
    HTTPRequestTimeout timeout = https ? HTTPRequestTimeoutLong : HTTPRequestTimeoutLow;
    if (http_request(url, &data, timeout) != GS_OK)
    {
        vita_log::error("[load_serverinfo] http_request fallo");
        return GS_IO_ERROR;
    }
    if (xml_status(data) == GS_ERROR)
    {
        vita_log::error("[load_serverinfo] xml_status error");
        return GS_ERROR;
    }

    // Avoid xml_search here; malformed serverinfo responses previously crashed it.
    auto extractTag = [](const std::string& xml, const char* tag, std::string& out) -> bool
    {
        out.clear();
        const std::string open  = std::string("<") + tag + ">";
        const std::string close = std::string("</") + tag + ">";
        size_t p1               = xml.find(open);
        if (p1 == std::string::npos)
            return false;
        p1 += open.size();
        size_t p2 = xml.find(close, p1);
        if (p2 == std::string::npos || p2 < p1)
            return false;
        out.assign(xml.data() + p1, p2 - p1);
        return true;
    };

    const std::string xmlPayload((const char*)data.bytes(), data.size());
    if (xmlPayload.size() < 20)
    {
        vita_log::error("[load_serverinfo] XML demasiado corto");
    }
    std::string pairedText;
    std::string currentGameText;
    std::string stateText;
    std::string httpsPortText;

    if (!extractTag(xmlPayload, "currentgame", currentGameText))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo currentgame");
        return ret;
    }
    if (!extractTag(xmlPayload, "PairStatus", pairedText))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo PairStatus");
        return ret;
    }
    std::string serverUniqueId;
    extractTag(xmlPayload, "uniqueid", serverUniqueId);
    if (!extractTag(xmlPayload, "appversion", server->serverInfoAppVersion))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo appversion");
        return ret;
    }
    if (!extractTag(xmlPayload, "state", stateText))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo state");
        return ret;
    }

    std::string scms;
    if (!extractTag(xmlPayload, "ServerCodecModeSupport", scms))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo ServerCodecModeSupport");
        return ret;
    }
    server->serverInfo.serverCodecModeSupport = scms.empty() ? 0 : atoi(scms.c_str());

    extractTag(xmlPayload, "gputype", server->gpuType);
    extractTag(xmlPayload, "GsVersion", server->gsVersion);
    if (!extractTag(xmlPayload, "hostname", server->hostname))
    {
        vita_log::error("[load_serverinfo] simpleXML fallo hostname");
        return ret;
    }
    extractTag(xmlPayload, "GfeVersion", server->serverInfoGfeVersion);
    extractTag(xmlPayload, "HttpsPort", httpsPortText);
    extractTag(xmlPayload, "mac", server->mac);

    if (currentGameText.empty() || pairedText.empty() || server->serverInfoAppVersion.empty() || stateText.empty())
    {
        vita_log::error("[load_serverinfo] campos requeridos vacios");
        return ret;
    }

    server->paired = pairedText == "1";
    if (!server->paired)
    {
        vita_log::info("[load_serverinfo] Host no emparejado (PairStatus=0). Se requerirá pairing antes de lanzar.");
        if (!serverUniqueId.empty() && serverUniqueId != unique_id)
        {
            vita_log::debug("[load_serverinfo] Server and client unique IDs differ");
        }
    }
    if (currentGameText.size() > 32)
    {
        vita_log::error("[load_serverinfo] currentGameText demasiado largo ({} bytes)", currentGameText.size());
        return ret;
    }
    server->currentGame = currentGameText.empty() ? 0 : atoi(currentGameText.c_str());
    if (server->currentGame != 0)
    {
        vita_log::info("[load_serverinfo] currentGame reportado por host: {} (posible sesión previa)", server->currentGame);
    }
    server->supports4K = server->serverInfo.serverCodecModeSupport != 0;
    if (server->serverInfoAppVersion.size() > 64)
    {
        vita_log::error("[load_serverinfo] appversion demasiado largo ({} bytes)", server->serverInfoAppVersion.size());
        return ret;
    }
    server->serverMajorVersion = server->serverInfoAppVersion.empty() ? 0 : atoi(server->serverInfoAppVersion.c_str());
    server->httpsPort          = httpsPortText.empty() ? 47984 : atoi(httpsPortText.c_str());
    if (!server->httpsPort)
        server->httpsPort = 47984;

    if (stateText == "_SERVER_BUSY")
    {
        server->currentGame = 0;
    }
    vita_log::info("[load_serverinfo] OK paired={}, currentGame={}, appVersion='{}', httpsPort={}, state='{}'", server->paired, server->currentGame, server->serverInfoAppVersion, server->httpsPort, stateText);
    return GS_OK;
}

static int load_server_status(PSERVER_DATA server)
{
    int ret = GS_INVALID;
    int i;

    /* Fetch the HTTPS port if we don't have one yet */
    if (!server->httpsPort)
    {
        ret = load_serverinfo(server, false);
        if (ret != GS_OK)
            return ret;
    }

    // Modern GFE versions don't allow serverinfo to be fetched over HTTPS if the client
    // is not already paired. Since we can't pair without knowing the server version, we
    // make another request over HTTP if the HTTPS request fails. We can't just use HTTP
    // for everything because it doesn't accurately tell us if we're paired.
    ret = GS_INVALID;
    for (i = 0; i < 2 && ret != GS_OK; i++)
    {
        ret = load_serverinfo(server, i == 0);
    }

    if (ret == GS_OK)
    {
        if (server->serverMajorVersion > MAX_SUPPORTED_GFE_VERSION)
        {
            gs_set_error(
                "Ensure you're running the latest version of "
                "Moonlight-Switch or downgrade GeForce Experience and try again");
            ret = GS_UNSUPPORTED_VERSION;
        }
        else if (server->serverMajorVersion < MIN_SUPPORTED_GFE_VERSION)
        {
            gs_set_error(
                "Moonlight-Switch requires a newer version of GeForce "
                "Experience. Please upgrade GFE on your PC and try again.");
            ret = GS_UNSUPPORTED_VERSION;
        }
    }

    return ret;
}

static std::string _gs_error = "";

void gs_set_error(std::string error) { _gs_error = error; }

std::string gs_error()
{
    if (_gs_error.empty())
    {
        return "Unknown error...";
    }
    return _gs_error;
}

int gs_unpair(PSERVER_DATA server)
{
    int ret = GS_OK;
    char url[4096];

    Data data;

    snprintf(url, sizeof(url), "http://%s:%u/unpair?uniqueid=%s",
        server->serverInfo.address,
        server->httpPort,
        unique_id.c_str());
    ret = http_request(url, &data, HTTPRequestTimeoutLow);
    return ret;
}

static int gs_pair_validate(Data& data, std::string* result)
{
    *result = "";

    int ret = GS_OK;
    if ((ret = xml_status(data) != GS_OK))
    {
        return ret;
    }
    else if ((ret = xml_search(data, "paired", result)) != GS_OK)
    {
        return ret;
    }

    return ret;
}

static int gs_pair_cleanup(int ret, PSERVER_DATA server, std::string* result)
{
    if (ret != GS_OK)
    {
        gs_unpair(server);
    }
    return ret;
}

int gs_pair(PSERVER_DATA server, char* pin)
{
    int ret = GS_OK;
    Data data;
    std::string result;
    char url[4096];

    if (server->paired)
    {
        gs_set_error("Already paired");
        return GS_WRONG_STATE;
    }

    if (server->currentGame != 0)
    {
        gs_set_error(
            "The computer is currently in a game. You must close the game "
            "before pairing");
        return GS_WRONG_STATE;
    }

    vita_log::info("Client: Pairing with generation {} server",
        server->serverMajorVersion);
    vita_log::info("Client: Start pairing stage #1");

    Data salt       = Data::random_bytes(16);
    Data salted_pin = salt.append(pin ? Data(pin, strlen(pin)) : Data());
    snprintf(url, sizeof(url),
        "http://%s:%u/"
        "pair?uniqueid=%s&devicename=roth&updateState=1&phrase="
        "getservercert&salt=%s&clientcert=%s",
        server->serverInfo.address,
        server->httpPort,
        unique_id.c_str(), salt.hex().bytes(),
        CryptoManager::cert_data().hex().bytes());

    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = gs_pair_validate(data, &result) != GS_OK))
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = xml_search(data, "plaincert", &result)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    vita_log::info("Client: Start pairing stage #2");

    Data plainCert = result.empty() ? Data() : Data((char*)result.c_str(), result.size());
    Data aesKey;

    // Gen 7 servers use SHA256 to get the key
    int hashLength;
    if (server->serverMajorVersion >= 7)
    {
        aesKey     = CryptoManager::create_AES_key_from_salt_SHA256(salted_pin);
        hashLength = 32;
    }
    else
    {
        aesKey     = CryptoManager::create_AES_key_from_salt_SHA1(salted_pin);
        hashLength = 20;
    }

    Data randomChallenge    = Data::random_bytes(16);
    Data encryptedChallenge = CryptoManager::aes_encrypt(randomChallenge, aesKey);

    snprintf(
        url, sizeof(url),
        "http://%s:%u/"
        "pair?uniqueid=%s&devicename=roth&updateState=1&clientchallenge=%s",
        server->serverInfo.address,
        server->httpPort,
        unique_id.c_str(),
        encryptedChallenge.hex().bytes());

    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = gs_pair_validate(data, &result) != GS_OK))
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if (xml_search(data, "challengeresponse", &result) != GS_OK)
    {
        ret = GS_INVALID;
        return gs_pair_cleanup(ret, server, &result);
    }

    vita_log::info("Client: Start pairing stage #3");

    Data encServerChallengeResp = result.empty() ? Data() : Data((char*)result.c_str(), result.size()).hex_to_bytes();
    Data decServerChallengeResp = CryptoManager::aes_decrypt(encServerChallengeResp, aesKey);
    Data serverResponse         = decServerChallengeResp.subdata(0, hashLength);
    Data serverChallenge        = decServerChallengeResp.subdata(hashLength, 16);

    Data clientSecret           = Data::random_bytes(16);
    Data challengeRespHashInput = serverChallenge
                                      .append(CryptoManager::signature(CryptoManager::cert_data()))
                                      .append(clientSecret);

    Data challengeRespHash;

    if (server->serverMajorVersion >= 7)
    {
        challengeRespHash = CryptoManager::SHA256_hash_data(challengeRespHashInput);
    }
    else
    {
        challengeRespHash = CryptoManager::SHA1_hash_data(challengeRespHashInput);
    }
    Data challengeRespEncrypted = CryptoManager::aes_encrypt(challengeRespHash, aesKey);

    snprintf(
        url, sizeof(url),
        "http://%s:%u/"
        "pair?uniqueid=%s&devicename=roth&updateState=1&serverchallengeresp=%s",
        server->serverInfo.address,
        server->httpPort,
        unique_id.c_str(),
        challengeRespEncrypted.hex().bytes());

    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = gs_pair_validate(data, &result) != GS_OK))
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if (xml_search(data, "pairingsecret", &result) != GS_OK)
    {
        ret = GS_INVALID;
        return gs_pair_cleanup(ret, server, &result);
    }

    vita_log::info("Client: Start pairing stage #4");

    Data serverSecretResp = result.empty() ? Data() : Data((char*)result.c_str(), result.size()).hex_to_bytes();
    Data serverSecret     = serverSecretResp.subdata(0, 16);
    Data serverSignature  = serverSecretResp.subdata(16, 256);

    if (!CryptoManager::verify_signature(serverSecret, serverSignature,
            plainCert.hex_to_bytes()))
    {
        gs_set_error("MITM attack detected");
        ret = GS_FAILED;
        return gs_pair_cleanup(ret, server, &result);
    }

    Data serverChallengeRespHashInput = randomChallenge
                                            .append(CryptoManager::signature(plainCert.hex_to_bytes()))
                                            .append(serverSecret);
    Data serverChallengeRespHash;

    if (server->serverMajorVersion >= 7)
    {
        serverChallengeRespHash = CryptoManager::SHA256_hash_data(serverChallengeRespHashInput);
    }
    else
    {
        serverChallengeRespHash = CryptoManager::SHA1_hash_data(serverChallengeRespHashInput);
    }

    Data clientPairingSecret = clientSecret.append(
        CryptoManager::sign_data(clientSecret, CryptoManager::key_data()));

    snprintf(
        url, sizeof(url),
        "http://%s:%u/"
        "pair?uniqueid=%s&devicename=roth&updateState=1&clientpairingsecret=%s",
        server->serverInfo.address,
        server->httpPort,
        unique_id.c_str(),
        clientPairingSecret.hex().bytes());
    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = gs_pair_validate(data, &result) != GS_OK))
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    vita_log::info("Client: Start pairing stage #5");

    snprintf(
        url, sizeof(url),
        "https://%s:%u/"
        "pair?uniqueid=%s&devicename=roth&updateState=1&phrase=pairchallenge",
        server->serverInfo.address, server->httpsPort, unique_id.c_str());
    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) != GS_OK)
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    if ((ret = gs_pair_validate(data, &result) != GS_OK))
    {
        return gs_pair_cleanup(ret, server, &result);
    }

    server->paired = true;

    // Sunshine may take a moment to expose PairStatus over HTTPS.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        int r = load_serverinfo(server, true);
        if (r == GS_OK && server->paired)
        {
            vita_log::info("[gs_pair] post-pair server reports paired via HTTPS (attempt {})", attempt + 1);
            break;
        }
        vita_log::warning("[gs_pair] post-pair /serverinfo HTTPS did not report PairStatus=1 (attempt {})", attempt + 1);
        usleep(200 * 1000); // 200ms
    }

    return gs_pair_cleanup(ret, server, &result);
}

int gs_applist(PSERVER_DATA server, PAPP_LIST* list)
{
    int ret = GS_OK;
    char url[4096];
    Data data;
    *list = NULL;

    snprintf(url, sizeof(url), "https://%s:%u/applist?uniqueid=%s",
        server->serverInfo.address, server->httpsPort, unique_id.c_str());

    if (http_request(url, &data, HTTPRequestTimeoutMedium) != GS_OK)
    {
        return GS_IO_ERROR;
    }
    if (xml_status(data) == GS_ERROR)
    {
        return GS_ERROR;
    }

    std::string xml((const char*)data.bytes(), data.size());
    size_t pos     = 0;
    PAPP_LIST head = NULL;
    int appCount   = 0;
    while (true)
    {
        size_t start = xml.find("<App>", pos);
        if (start == std::string::npos)
            break;
        size_t end = xml.find("</App>", start);
        if (end == std::string::npos)
            break;
        size_t contentStart  = start + 5; // len("<App>")
        std::string appBlock = xml.substr(contentStart, end - contentStart);

        auto extract = [](const std::string& block, const char* tag, std::string& out) -> bool
        {
            out.clear();
            std::string o = std::string("<") + tag + ">";
            std::string c = std::string("</") + tag + ">";
            size_t p1     = block.find(o);
            if (p1 == std::string::npos)
                return false;
            p1 += o.size();
            size_t p2 = block.find(c, p1);
            if (p2 == std::string::npos)
                return false;
            out.assign(block.data() + p1, p2 - p1);
            return true;
        };
        std::string idStr, titleStr;
        if (!extract(appBlock, "ID", idStr) || !extract(appBlock, "AppTitle", titleStr))
        {
            vita_log::error("[gs_applist] fallo parse parcial appBlock");
            pos = end + 6;
            continue;
        }
        if (titleStr.size() > 256)
            titleStr.resize(256);
        PAPP_LIST app = (PAPP_LIST)malloc(sizeof(APP_LIST));
        if (!app)
        {
            vita_log::error("[gs_applist] malloc APP_LIST fallo");
            break;
        }
        memset(app, 0, sizeof(APP_LIST));
        app->id = idStr.empty() ? 0 : atoi(idStr.c_str());
        if (!titleStr.empty())
        {
            app->name = (char*)malloc(titleStr.size() + 1);
            if (app->name)
            {
                memcpy(app->name, titleStr.c_str(), titleStr.size() + 1);
            }
        }
        app->next = head;
        head      = app;
        appCount++;
        pos = end + 6; // avanzar tras </App>
    }
    *list = head;
    vita_log::info("[gs_applist] parse simple completo apps={} bytesXML={}", appCount, (int)xml.size());
    return ret;
}

int gs_app_boxart(PSERVER_DATA server, int app_id, Data* out)
{
    int ret = GS_OK;
    char url[4096];
    Data data;

    snprintf(
        url, sizeof(url),
        "https://%s:%u/appasset?uniqueid=%s&appid=%d&AssetType=2&AssetIdx=0",
        server->serverInfo.address, server->httpsPort, unique_id.c_str(), app_id);

    if (http_request(url, &data, HTTPRequestTimeoutMedium) != GS_OK)
    {
        ret = GS_IO_ERROR;
    }
    else
    {
        *out = data;
    }
    return ret;
}

int gs_start_app(PSERVER_DATA server, STREAM_CONFIGURATION* config, int appId,
    bool sops, bool localaudio, int gamepad_mask, int displayWidth, int displayHeight)
{
    int ret = GS_OK;
    std::string result;

    if (config->height >= 2160 && !server->supports4K)
    {
        gs_set_error("4K not supported");
        return GS_NOT_SUPPORTED_4K;
    }

    Data rand = Data::random_bytes(16);
    memcpy(config->remoteInputAesKey, rand.bytes(), 16);
    char url[4096];
    int rikeyid = 0;

    Data data;

    extern bool g_force_fresh_launch_h264; // declarado en vita_session.cpp
    bool forceFresh = g_force_fresh_launch_h264;
    if (server->currentGame == 0 || forceFresh) {
        int channelCounnt =
            config->audioConfiguration == AUDIO_CONFIGURATION_STEREO
                ? CHANNEL_COUNT_STEREO
                : CHANNEL_COUNT_51_SURROUND;
        int mask = config->audioConfiguration == AUDIO_CONFIGURATION_STEREO
                       ? CHANNEL_MASK_STEREO
                       : CHANNEL_MASK_51_SURROUND;
        int fps = sops && config->fps > 60 ? 60 : config->fps;

        // Build base /launch URL with standard parameters
        snprintf(url, sizeof(url),
                 "https://%s:%u/"
                 "launch?uniqueid=%s&appid=%d&mode=%dx%dx%d&additionalStates=1&"
                 "sops=%d&rikey=%s&rikeyid=%d&localAudioPlayMode=%d&"
                 "surroundAudioInfo=%d&remoteControllersBitmap=%d&gcmap=%d",
                 server->serverInfo.address, server->httpsPort, unique_id.c_str(), appId,
                 config->width, config->height, fps, sops, rand.hex().bytes(),
                 rikeyid, localaudio, (mask << 16) + channelCounnt,
                 gamepad_mask, gamepad_mask);

        // Append display resolution hint if specified (Sunshine protocol extension)
        // Allows host to configure display at different resolution than stream
        // Use case: Stream 960x544 to client while host display runs at 1920x1080
        if (displayWidth > 0 && displayHeight > 0) {
            size_t currentLen = strlen(url);
            snprintf(url + currentLen, sizeof(url) - currentLen,
                     "&displayWidth=%d&displayHeight=%d",
                     displayWidth, displayHeight);
            vita_log::info("[gs_start_app] Display resolution hint: {}x{} (stream: {}x{})",
                             displayWidth, displayHeight,
                             config->width, config->height);
        }

        // Append Limelight query parameters
        size_t currentLen = strlen(url);
        snprintf(url + currentLen, sizeof(url) - currentLen, "%s",
                 LiGetLaunchUrlQueryParameters());
        if (forceFresh && server->currentGame != 0) {
            vita_log::warning("[gs_start_app] Ignorando resume para forzar renegociación H.264 (fresh launch)");
        }
    } else {
        snprintf(url, sizeof(url),
                 "https://%s:%u/resume?uniqueid=%s&rikey=%s&rikeyid=%d&localAudioPlayMode=%d%s",
                 server->serverInfo.address, server->httpsPort, unique_id.c_str(),
                 rand.hex().bytes(), rikeyid, localaudio, LiGetLaunchUrlQueryParameters());
    }

    if ((ret = http_request(url, &data, HTTPRequestTimeoutLong)) == GS_OK) {
        server->currentGame = appId;
    } else {
        goto exit;
    }

    if ((ret = xml_status(data) != GS_OK)) {
        goto exit;
    } else if ((ret = xml_search(data, "gamesession", &result)) != GS_OK) {
        goto exit;
    }

    if (result == "0") {
        ret = GS_FAILED;
        goto exit;
    }

    if (xml_search(data, "sessionUrl0", &result) == GS_OK && !result.empty()) {
        const std::string::size_type size = result.size();
        server->serverInfo.rtspSessionUrl = new char[size + 1];
        memcpy((void *) server->serverInfo.rtspSessionUrl, result.c_str(), size + 1);
    } else {
        vita_log::error("sessionUrl0 not found or empty");
    }

exit:
    return ret;
}

int gs_quit_app(PSERVER_DATA server)
{
    int ret = GS_OK;
    char url[4096];
    std::string result;
    Data data;

    snprintf(url, sizeof(url), "https://%s:%u/cancel?uniqueid=%s",
        server->serverInfo.address, server->httpsPort, unique_id.c_str());
    if ((ret = http_request(url, &data, HTTPRequestTimeoutMedium)) != GS_OK)
        goto exit;

    if ((ret = xml_status(data) != GS_OK))
    {
        goto exit;
    }
    else if ((ret = xml_search(data, "cancel", &result)) != GS_OK)
    {
        goto exit;
    }

    if (result == "0")
    {
        ret = GS_FAILED;
        goto exit;
    }

exit:
    return ret;
}

int gs_init(PSERVER_DATA server, const std::string address, const std::string& keyDir)
{
    std::stringstream addressStream(address);
    std::string segment;
    std::vector<std::string> seglist;
    unsigned short httpPort = 47989; // Default HTTP port

    while (std::getline(addressStream, segment, ':'))
    {
        seglist.push_back(segment);
    }

    // Override port if it presented
    if (seglist.size() > 1 && !seglist[1].empty())
    {
        const char* portStr = seglist[1].c_str();
        if (portStr && strlen(portStr) > 0)
        {
            httpPort = atoi(portStr);
        }
    }

    if (!CryptoManager::load_cert_key_pair(keyDir))
    {
        vita_log::info("Client: No certs, generate new...");

        if (!CryptoManager::generate_new_cert_key_pair(keyDir))
        {
            vita_log::info("Client: Failed to generate certs...");
            return GS_FAILED;
        }
    }

    http_init(keyDir);

    if (load_unique_id(keyDir) != GS_OK)
        return GS_FAILED;

    LiInitializeServerInformation(&server->serverInfo);
    server->address = seglist[0];
    if (server->address.empty())
    {
        return GS_INVALID;
    }
    // Limelight retains this pointer for the process lifetime.
    static std::vector<char*> _allocated;
    auto dup_str = [&](const std::string& s) -> const char*
    {
        if (s.empty())
            return nullptr;
        char* cpy = (char*)malloc(s.size() + 1);
        if (!cpy)
            return nullptr;
        memcpy(cpy, s.c_str(), s.size() + 1);
        _allocated.push_back(cpy);
        return cpy;
    };
    server->serverInfo.address = dup_str(server->address);
    server->httpPort           = httpPort;
    server->httpsPort          = 0; /* Populated by load_server_status() */

    int result = load_server_status(server);
    if (!server->serverInfoAppVersion.empty())
    {
        server->serverInfo.serverInfoAppVersion = dup_str(server->serverInfoAppVersion);
    }
    if (!server->serverInfoGfeVersion.empty())
    {
        server->serverInfo.serverInfoGfeVersion = dup_str(server->serverInfoGfeVersion);
    }
    return result;
}
