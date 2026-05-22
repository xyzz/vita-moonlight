/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include "http.h"
#include "CryptoManager.hpp"
#include "client.h"
#include "errors.h"
#include "debug.hpp"

#include <fmt/format.h>
#include <curl/curl.h>
#include <cstring>
#include <sys/stat.h>

static bool curlGlobalInit = false;
static std::string certificateFilePath;
static std::string keyFilePath;

extern "C" void vita_debug_log(const char* fmt, ...);

static void _gs_log_info(const std::string& msg) {
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    vita_debug_log("%s", msg.c_str());
#else
    vita_log::info("{}", msg);
#endif
}

static void _gs_log_error(const std::string& msg) {
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    vita_debug_log("%s", msg.c_str());
#else
    vita_log::error("{}", msg);
#endif
}

static int _curl_debug_log(CURL* /*handle*/, curl_infotype type, char* data, size_t size, void* /*userptr*/) {
    if (!data || size == 0) {
        return 0;
    }

    // Keep only human-readable verbose lines (skip raw payload dumps)
    if (type != CURLINFO_TEXT && type != CURLINFO_HEADER_IN && type != CURLINFO_HEADER_OUT) {
        return 0;
    }

    std::string msg(data, size);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }
    if (!msg.empty()) {
        _gs_log_info(msg);
    }
    return 0;
}

static bool _file_exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

CURL* makeCurl();
void freeCurl(CURL* curl);

struct HTTP_DATA {
    char* memory;
    size_t size;
};

static size_t _write_curl(void* contents, size_t size, size_t nmemb,
                          void* userp) {
    size_t realsize = size * nmemb;
    auto* mem = (HTTP_DATA*)userp;

    mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
        return 0;

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

int http_init(const std::string& key_directory) {
    std::string actual_key_directory = key_directory.empty() ? "." : key_directory;

    // Always refresh file paths because keyDir can change between hosts/retries.
    certificateFilePath = actual_key_directory + "/" + CERTIFICATE_FILE_NAME;
    keyFilePath = actual_key_directory + "/" + KEY_FILE_NAME;
    _gs_log_info(fmt::format("Curl: TLS files cert='{}' ({}) key='{}' ({})",
                       certificateFilePath,
                       _file_exists(certificateFilePath) ? "ok" : "missing",
                       keyFilePath,
                       _file_exists(keyFilePath) ? "ok" : "missing"));

    if (!curlGlobalInit) {
#if LIBCURL_VERSION_NUM >= 0x075600
#ifdef USE_OPENSSL_CRYPTO
        curl_global_sslset(CURLSSLBACKEND_OPENSSL, NULL, NULL);
#elif USE_MBEDTLS_CRYPTO
        curl_global_sslset(CURLSSLBACKEND_MBEDTLS, NULL, NULL);
#endif
#endif
        curl_global_init(CURL_GLOBAL_ALL);
        _gs_log_info(fmt::format("Curl: {}", curl_version()));
        curlGlobalInit = true;
    }
    return GS_OK;
}

CURL* makeCurl() {
    auto curl = curl_easy_init();

    if (!curl)
        return nullptr;

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, _curl_debug_log);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLENGINE_DEFAULT, 1L);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certificateFilePath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, keyFilePath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_curl);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    return curl;
}

void freeCurl(CURL* curl) {
    curl_easy_cleanup(curl);
}

int http_request(const std::string& url, Data* data,
                 HTTPRequestTimeout timeout) {
    _gs_log_info(fmt::format("Curl: Request:\n{}", url.c_str()));

    auto* http_data = (HTTP_DATA*)malloc(sizeof(HTTP_DATA));
    if (!http_data) {
        gs_set_error("Out of memory");
        return GS_OUT_OF_MEMORY;
    }
    http_data->memory = (char*)malloc(1);
    if (!http_data->memory) {
        gs_set_error("Out of memory");
        free(http_data);
        return GS_OUT_OF_MEMORY;
    }
    http_data->size = 0;

    auto curl = makeCurl();
    if (!curl) {
        free(http_data->memory);
        free(http_data);
        return GS_FAILED;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_data);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        gs_set_error(curl_easy_strerror(res));
        _gs_log_error(fmt::format("Curl: error: {}", gs_error().c_str()));
        freeCurl(curl);
        free(http_data->memory);
        free(http_data);
        return GS_FAILED;
    } else if (http_data->memory == nullptr) {
        _gs_log_error("Curl: memory = NULL");
        freeCurl(curl);
        free(http_data->memory);
        free(http_data);
        return GS_OUT_OF_MEMORY;
    }

    *data = Data(http_data->memory, http_data->size);

    if (http_data->size > 3000) {
        _gs_log_info("Curl: Response: Ok");
    } else {
        _gs_log_info(fmt::format("Curl: Response:\n{}", http_data->memory));
    }

    free(http_data->memory);
    free(http_data);
    freeCurl(curl);

    return GS_OK;
}

void http_cleanup() {
    curl_global_cleanup();
    curlGlobalInit = false;
    certificateFilePath.clear();
    keyFilePath.clear();
}
