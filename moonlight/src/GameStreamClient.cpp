#include "GameStreamClient.hpp"

#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include "ConfigManager.hpp"
#include "audio/MicrophoneManager.hpp"
#include "crypto/CryptoManager.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "moonmic/MoonmicBridge.hpp"

namespace
{

std::vector<std::string> keyDirectoryCandidates(const std::string& base, const HostInfo& host)
{
    std::vector<std::string> candidates;
    auto add = [&candidates](std::string candidate)
    {
        if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
            candidates.push_back(std::move(candidate));
    };

    const std::string primary = makeSafeHostId(host.safeId.empty()
            ? (host.name.empty() ? host.ip : host.name)
            : host.safeId);
    add(base + "/" + primary);

    if (!host.name.empty())
    {
        const auto separator = host.name.find('.');
        if (separator != std::string::npos)
            add(base + "/" + makeSafeHostId(host.name.substr(0, separator)));
    }

    if (!host.ip.empty())
    {
        const std::string safeAddress = makeSafeHostId(host.ip);
        if (safeAddress == host.ip)
            add(base + "/" + host.ip);
        add(base + "/" + safeAddress);
    }
    return candidates;
}

std::string findDeviceKeyDirectory(const std::vector<std::string>& candidates)
{
    struct stat state {};
    for (const auto& candidate : candidates)
    {
        const std::string deviceIni = candidate + "/device.ini";
        if (stat(candidate.c_str(), &state) == 0 && S_ISDIR(state.st_mode)
            && stat(deviceIni.c_str(), &state) == 0)
            return candidate;
    }
    return {};
}

}

GameStreamClient& GameStreamClient::instance()
{
    static GameStreamClient instance;
    return instance;
}

bool GameStreamClient::connect(const HostInfo& host)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (host.ip.empty())
    {
        vita_log::error("[GameStreamClient] Dirección vacía en connect(host)");
        return false;
    }
    std::string addr = host.ip;
    if (m_server_data.count(addr) > 0)
    {
        vita_log::info("[GameStreamClient] Ya conectado a %s", addr.c_str());
        return true;
    }
    SERVER_DATA serverData {};
    ConfigManager cfg;
    cfg.load();
    std::string baseDevices = cfg.getKeysDir();
    std::string safe        = makeSafeHostId(host.safeId.empty() ? (host.name.empty() ? host.ip : host.name) : host.safeId);
    const auto candidates   = keyDirectoryCandidates(baseDevices, host);
    std::string keyDir      = findDeviceKeyDirectory(candidates);
    if (keyDir.empty())
        keyDir = candidates.front();
    vita_log::info("[GameStreamClient] keyDir elegido para '%s' => '%s' (safeId base='%s')", addr.c_str(), keyDir.c_str(), safe.c_str());
    int status = gs_init(&serverData, addr, keyDir);
    if (status != GS_OK)
    {
        vita_log::error("[GameStreamClient] Error al inicializar servidor: %d", status);
        return false;
    }
    m_key_dirs[addr]    = keyDir;
    m_server_data[addr] = serverData;
    vita_log::info("[GameStreamClient] Conexión exitosa a %s", addr.c_str());
    const char* srv_addr = serverData.serverInfo.address ? serverData.serverInfo.address : "NULL";
    vita_log::info("[GameStreamClient] ServerInfo address: %s", srv_addr);

    // A transient PairStatus must not overwrite an existing paired host.
    std::string deviceIniPath = keyDir + "/device.ini";
    struct stat st {};
    if (::stat(deviceIniPath.c_str(), &st) != 0)
    {
        try
        {
            HostStorage::writeDeviceIni(keyDir, safe, addr.c_str(), serverData.httpPort, serverData.paired, serverData.mac.empty() ? nullptr : serverData.mac.c_str());
        }
        catch (...)
        {
            vita_log::warning("[GameStreamClient] No se pudo crear device.ini para %s", addr.c_str());
        }
    }
    return true;
}

bool GameStreamClient::isConnected(const std::string& address)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_server_data.count(address) > 0;
}

bool GameStreamClient::serverInfo(const std::string& address, SERVER_INFORMATION& out) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto server = m_server_data.find(address);
    if (server == m_server_data.end())
        return false;
    out = server->second.serverInfo;
    return true;
}

int GameStreamClient::currentGame(const std::string& address) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto server = m_server_data.find(address);
    return server == m_server_data.end() ? 0 : server->second.currentGame;
}

std::string GameStreamClient::getKeyDirFor(const std::string& address) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_key_dirs.find(address);
    if (it == m_key_dirs.end())
        return std::string();
    return it->second;
}

bool GameStreamClient::startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, StartMode mode, int displayWidth, int displayHeight)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_server_data.count(address) == 0)
    {
        vita_log::error("[GameStreamClient] No conectado a %s", address.c_str());
        return false;
    }
    vita_log::info("[GameStreamClient] Iniciando aplicación %d en %s (mode=%d) display=%dx%d", appId, address.c_str(), (int)mode, displayWidth, displayHeight);

    if (mode == StartMode::RESUME_ONLY)
    {
        SERVER_DATA& sd = m_server_data[address];
        if (sd.currentGame == 0)
        {
            vita_log::warning("[GameStreamClient] Resume solicitado pero server.currentGame==0 para %s", address.c_str());
            return false;
        }
    }

    // Suppress the active-session prompt while a resume request is in flight.
    bool markedResume = false;
    if (mode == StartMode::RESUME_ONLY)
    {
        vita_log::info("[GameStreamClient] Marcando resume en progreso para %s", address.c_str());
        m_resume_in_progress.insert(address);
        markedResume               = true;
        m_resume_attempts[address] = std::chrono::steady_clock::now();
    }

    extern bool g_force_fresh_launch_h264;
    bool prevForce = g_force_fresh_launch_h264;
    if (mode == StartMode::NEW_ONLY)
        g_force_fresh_launch_h264 = true;

    ConfigManager configManager;
    configManager.load();
    VideoSettings videoSettings = configManager.getVideoSettings();
    bool sops                   = videoSettings.sops;
    vita_log::info("[GameStreamClient] SOPS (Optimize game settings) = %d", sops);
    vita_log::info("[GameStreamClient] Local audio playback on host = %d", videoSettings.localaudio);

    uint16_t targetWidth  = 0;
    uint16_t targetHeight = 0;
    {
        std::string micHost = videoSettings.microphone_host_ip.empty() ? address : videoSettings.microphone_host_ip;
        int micPort         = videoSettings.microphone_port;

        auto& bridge = moonmic::MoonmicBridge::getInstance();
        bridge.loadConfig();
        if (bridge.isAvailable(micHost, micPort))
        {
            auto [width, height] = bridge.getTargetResolution();
            targetWidth          = width;
            targetHeight         = height;
        }
    }

    if (displayWidth == 0 && displayHeight == 0 && targetWidth > 0 && targetHeight > 0)
    {
        displayWidth  = targetWidth;
        displayHeight = targetHeight;
        vita_log::info("[GameStreamClient] Applying Moonmic target resolution to gs_start_app: %dx%d", displayWidth, displayHeight);
    }

    int status = gs_start_app(&m_server_data[address], &config, appId, sops,
        videoSettings.localaudio, 0x1, displayWidth, displayHeight);

    if (mode == StartMode::NEW_ONLY)
        g_force_fresh_launch_h264 = prevForce;

    if (status != GS_OK)
    {
        if (markedResume)
        {
            vita_log::info("[GameStreamClient] Resume falló para %s -> limpiando resume en progreso (status=%d)", address.c_str(), status);
            m_resume_in_progress.erase(address);
            m_resume_attempts.erase(address);
        }
        vita_log::error("[GameStreamClient] Error al iniciar aplicación: %d", status);
        return false;
    }

    // Keep the resume guard until VitaSession confirms startup.

    m_last_stream_cfg[address] = config;
    vita_log::info("[GameStreamClient] Aplicación iniciada correctamente (remoteInputKey set)");

    if (videoSettings.enable_microphone)
    {
        vita_log::info("[GameStreamClient] Microphone enabled in settings - starting transmission");

        std::string micHost = videoSettings.microphone_host_ip.empty()
            ? address
            : videoSettings.microphone_host_ip;

        bool micStarted = MicrophoneManager::getInstance().start(
            micHost,
            videoSettings.microphone_port,
            videoSettings.microphone_sample_rate,
            videoSettings.microphone_channels,
            videoSettings.microphone_bitrate);

        if (micStarted)
        {
            vita_log::info("[GameStreamClient] Microphone started successfully to %s:%d",
                micHost.c_str(), videoSettings.microphone_port);
        }
        else
        {
            vita_log::warning("[GameStreamClient] Microphone failed to start - will retry every 10s");
        }
    }
    else
    {
        vita_log::debug("[GameStreamClient] Microphone disabled in settings");
    }

    return true;
}

bool GameStreamClient::resumeApp(const std::string& address, STREAM_CONFIGURATION& config, int appId)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto server = m_server_data.find(address);
    if (server == m_server_data.end() || server->second.currentGame == 0)
    {
        vita_log::warning("[GameStreamClient] No hay sesión activa para reanudar en %s", address.c_str());
        return false;
    }

    ConfigManager configManager;
    configManager.load();
    VideoSettings settings = configManager.getVideoSettings();
    int status             = gs_start_app(&server->second, &config, appId, settings.sops,
                    settings.localaudio, 0x1, 0, 0);
    if (status != GS_OK)
    {
        vita_log::error("[GameStreamClient] Reanudación de sesión falló: %d", status);
        return false;
    }

    m_last_stream_cfg[address] = config;
    vita_log::info("[GameStreamClient] Sesión reanudada y parámetros RTSP renovados");
    return true;
}

bool GameStreamClient::lastStreamConfig(const std::string& address, STREAM_CONFIGURATION& out) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_last_stream_cfg.find(address);
    if (it == m_last_stream_cfg.end())
        return false;
    out = it->second;
    return true;
}

bool GameStreamClient::unpair(const std::string& address)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_server_data.count(address) == 0)
        return false;
    SERVER_DATA& s = m_server_data[address];
    int status     = gs_unpair(&s);
    if (status != GS_OK)
    {
        vita_log::error("[GameStreamClient] unpair fallo status=%d", status);
        return false;
    }
    s.paired = false;
    return true;
}

bool GameStreamClient::beginPairing(const HostInfo& host, std::function<void(bool)> onFinished, std::function<void(const std::string&)> onPinReady)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    HostInfo localHost = host;
    localHost.safeId   = makeSafeHostId(localHost.safeId.empty() ? (localHost.name.empty() ? localHost.ip : localHost.name) : localHost.safeId);
    std::string addr   = localHost.ip;
    if (addr.empty())
    {
        vita_log::error("[GameStreamClient] beginPairing: host ip vacío");
        if (onFinished)
            onFinished(false);
        return false;
    }

    // Reuse a valid pairing before replacing its certificates.
    {
        ConfigManager cfg;
        cfg.load();
        std::string base         = cfg.getKeysDir();
        const auto candidates    = keyDirectoryCandidates(base, localHost);
        const std::string keyDir = findDeviceKeyDirectory(candidates);
        if (!keyDir.empty())
        {
            try
            {
                SERVER_DATA serverData {};
                vita_log::info("[Pairing][fast-path] Comprobando emparejamiento con keyDir='%s'", keyDir.c_str());
                int initResult = gs_init(&serverData, localHost.ip, keyDir);
                if (initResult == GS_OK && serverData.paired)
                {
                    m_server_data[localHost.ip] = serverData;
                    m_key_dirs[localHost.ip]    = keyDir;
                    if (onFinished)
                        onFinished(true);
                    return true;
                }
            }
            catch (...)
            {
                vita_log::error("[Pairing][fast-path] Excepción al usar keyDir='%s'", keyDir.c_str());
            }
        }
    }

    auto* holder = new brls::Box(brls::Axis::COLUMN);
    auto* label  = new brls::Label();
    label->setText(brls::getStr("host_dialog/connecting"));
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMarginBottom(18);
    auto* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    spinner->View::setSize(brls::Size(92, 92));
    holder->addView(label);
    holder->addView(spinner);
    holder->setAlignItems(brls::AlignItems::CENTER);
    holder->setJustifyContent(brls::JustifyContent::CENTER);
    holder->setPadding(28, 28, 28, 28);
    auto* dialog = new brls::Dialog(holder);
    dialog->setCancelable(false);
    dialog->open();
    brls::Application::blockInputs();

    auto completePairing = [dialog, onFinished](bool success)
    {
        brls::sync([dialog]()
            {
            dialog->dismiss();
            brls::Application::unblockInputs(); });
        if (onFinished)
            onFinished(success);
    };

    std::thread([=]() mutable
        {
            ConfigManager cfg;
            cfg.load();
            std::string base   = cfg.getKeysDir();
            std::string keyDir = base + "/" + localHost.safeId;
            if (!cfg.ensureKeyDirExists(localHost.safeId))
            {
                vita_log::error("[Pairing] No se pudo crear/asegurar keyDir '%s'", keyDir.c_str());
                brls::sync([label]()
                    { label->setText(brls::getStr("host_dialog/pairing_error_init")); });
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                completePairing(false);
                return;
            }

            SERVER_DATA serverData {};
            try
            {
                namespace fs       = std::filesystem;
                std::string client = keyDir + "/client.pem";
                std::string key    = keyDir + "/key.pem";
                std::string uid    = keyDir + "/uniqueid.dat";
                std::error_code error;
                fs::remove(client, error);
                fs::remove(key, error);
                fs::remove(uid, error);
                CryptoManager::remove_cert_key_pair(keyDir);
            }
            catch (...)
            {
                vita_log::warning("[Pairing] No se pudo limpiar cache de certificados para keyDir='%s'", keyDir.c_str());
            }

            int initResult = gs_init(&serverData, addr, keyDir);
            if (initResult != GS_OK)
            {
                vita_log::error("[Pairing] gs_init fallo %d", initResult);
                brls::sync([label]()
                    { label->setText(brls::getStr("host_dialog/pairing_error_init")); });
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
                completePairing(false);
                return;
            }

            bool forced   = false;
            int oldGameId = serverData.currentGame;
            if (serverData.currentGame != 0)
            {
                forced = true;
                vita_log::warning("[Pairing] host ocupado (currentGame=%d) -> forzando emparejado (puede fallar)", serverData.currentGame);
                brls::sync([label]()
                    { label->setText(brls::getStr("host_dialog/pairing_force_warning")); });
                serverData.currentGame = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
            }

            char pin[5];
            srand((unsigned int)time(nullptr));
            snprintf(pin, sizeof(pin), "%d%d%d%d", rand() % 10, rand() % 10, rand() % 10, rand() % 10);
            std::string pinString(pin);
            vita_log::info("[Pairing] PIN generado %s", pinString.c_str());
            brls::sync([label, pinString, spinner]()
                {
                std::string message = brls::getStr("host_dialog/pairing_enter_pin");
                size_t marker       = message.find("$(pin)");
                if (marker != std::string::npos)
                    message.replace(marker, 6, pinString);
                label->setText(message);
                spinner->setVisibility(brls::Visibility::GONE);
            });

            if (onPinReady)
                onPinReady(pinString);

            int pairResult = gs_pair(&serverData, pin);
            if (forced)
                serverData.currentGame = oldGameId;

            if (pairResult == GS_OK)
            {
                vita_log::info("[Pairing] Emparejamiento OK");
                serverData.paired = true;
                if (!HostStorage::savePairedHost(localHost.safeId, addr, serverData.httpPort,
                        serverData.paired, serverData.mac, localHost.microphone_port))
                    vita_log::warning("[Pairing] No se pudo guardar device.ini para %s", addr.c_str());

                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    m_server_data[addr] = serverData;
                    m_key_dirs[addr]    = keyDir;
                }
                brls::sync([label]()
                    { label->setText(brls::getStr("host_dialog/pairing_success")); });
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                completePairing(true);
                return;
            }

            std::string gse_str = gs_error();
            const char* gse      = gse_str.empty() ? "(empty)" : gse_str.c_str();
            vita_log::error("[Pairing] gs_pair fallo %d -> gs_error='%s'", pairResult, gse);
            brls::sync([label]()
                { label->setText(brls::getStr("host_dialog/pairing_error")); });
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            completePairing(false); })
        .detach();
    return true;
}

void GameStreamClient::getAppList(const std::string& address, AppListCallback callback)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_server_data.count(address) == 0)
    {
        vita_log::error("[GameStreamClient] No conectado a %s", address.c_str());
        callback(std::vector<RemoteAppInfo>());
        return;
    }

    if (m_app_lists.count(address) > 0)
    {
        callback(m_app_lists[address]);
        return;
    }

    PAPP_LIST appList;
    int status = gs_applist(&m_server_data[address], &appList);
    if (status != GS_OK)
    {
        vita_log::error("[GameStreamClient] Error al obtener lista de aplicaciones: %d", status);
        callback(std::vector<RemoteAppInfo>());
        return;
    }

    PAPP_LIST appListHead = appList;

    std::vector<RemoteAppInfo> apps;
    PAPP_LIST current = appList;
    while (current)
    {
        RemoteAppInfo app;
        app.id      = std::to_string(current->id);
        app.name    = current->name ? std::string(current->name) : "Unknown App";
        app.iconUrl = "";
        apps.push_back(app);
        current = current->next;
    }

    current = appListHead;
    while (current)
    {
        PAPP_LIST next = current->next;
        if (current->name)
        {
            free(current->name);
        }
        free(current);
        current = next;
    }

    std::sort(apps.begin(), apps.end(),
        [](const RemoteAppInfo& a, const RemoteAppInfo& b)
        {
            return a.name < b.name;
        });

    m_app_lists[address] = apps;

    callback(apps);
}

bool GameStreamClient::getAppBoxart(const std::string& address, int appId, Data& outData)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_server_data.count(address) == 0)
    {
        vita_log::error("[GameStreamClient] getAppBoxart: no conectado a %s", address.c_str());
        return false;
    }
    int status = gs_app_boxart(&m_server_data[address], appId, &outData);
    if (status != GS_OK)
    {
        vita_log::warning("[GameStreamClient] gs_app_boxart falló para appId=%d con status=%d", appId, status);
        return false;
    }
    return true;
}

bool GameStreamClient::quitApp(const std::string& address)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_server_data.count(address) == 0)
    {
        vita_log::error("[GameStreamClient] No conectado a %s", address.c_str());
        return false;
    }

    vita_log::info("[GameStreamClient] Terminando aplicación en %s", address.c_str());

    int status = gs_quit_app(&m_server_data[address]);
    if (status != GS_OK)
    {
        vita_log::error("[GameStreamClient] Error al terminar aplicación: %d", status);
        return false;
    }

    vita_log::info("[GameStreamClient] Aplicación terminada correctamente");

    MicrophoneManager::getInstance().stop();
    vita_log::info("[GameStreamClient] Microphone stopped");

    auto it = m_active_streams.find(address);
    if (it != m_active_streams.end())
    {
        m_active_streams.erase(it);
    }
    m_server_data[address].currentGame = 0;
    return true;
}

void GameStreamClient::setActiveStream(const std::string& address, int appId, const std::string& appName)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_active_streams[address] = { appId, appName };
    if (m_resume_in_progress.erase(address) > 0)
    {
        vita_log::info("[GameStreamClient] setActiveStream: limpiando resume en progreso para %s", address.c_str());
    }
    m_resume_attempts.erase(address);
}

void GameStreamClient::clearActiveStream(const std::string& address)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_active_streams.erase(address);

    MicrophoneManager::getInstance().stop();
    vita_log::debug("[GameStreamClient] clearActiveStream: microphone stopped");
}

bool GameStreamClient::probeActiveSession(const HostInfo& host, RemoteAppInfo& outRunning)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    outRunning = RemoteAppInfo();
    if (host.ip.empty())
        return false;
    const std::string& address = host.ip;

    vita_log::info("[GameStreamClient] probeActiveSession ENTRY for %s (thread=%lld)", address.c_str(), (long long)std::hash<std::thread::id>()(std::this_thread::get_id()));

    bool connected = isConnected(address);
    if (!connected)
    {
        connected = connect(host);
    }
    if (!connected)
        return false;

    SERVER_DATA& sd        = m_server_data.at(address);
    bool sunshineRefreshed = false;
    {
        std::string serverinfo;
        if (fetchSunshineServerinfo(address, serverinfo))
        {
            int liveCurrentGame = 0;
            if (parseSunshineCurrentGame(serverinfo, liveCurrentGame))
            {
                sunshineRefreshed = true;
                sd.currentGame    = liveCurrentGame;
                vita_log::info("[GameStreamClient] probeActiveSession: Sunshine reports currentGame=%d for %s", liveCurrentGame, address.c_str());
                if (liveCurrentGame == 0)
                {
                    m_active_streams.erase(address);
                }
            }
            else
            {
                vita_log::warning("[GameStreamClient] probeActiveSession: Sunshine serverinfo missing/invalid currentGame for %s", address.c_str());
            }
        }
        else
        {
            vita_log::warning("[GameStreamClient] probeActiveSession: Sunshine serverinfo fetch failed for %s", address.c_str());
        }
    }

    if (!sunshineRefreshed)
    {
        auto it = m_active_streams.find(address);
        if (it != m_active_streams.end())
        {
            outRunning.id      = std::to_string(it->second.appId);
            outRunning.name    = it->second.appName;
            outRunning.iconUrl = "";
            return true;
        }
    }
    {
        std::string kd = getKeyDirFor(address);
        if (!kd.empty())
        {
            struct stat st;
            std::string clientPem = kd + "/client.pem";
            std::string keyPem    = kd + "/key.pem";
            std::string uniqueid  = kd + "/uniqueid.dat";
            bool hasClient        = (stat(clientPem.c_str(), &st) == 0);
            bool hasKey           = (stat(keyPem.c_str(), &st) == 0);
            bool hasUid           = false;
            if (stat(uniqueid.c_str(), &st) == 0 && st.st_size > 0)
                hasUid = true;
            if (!hasClient || !hasKey || !hasUid)
            {
                vita_log::info("[GameStreamClient] probeActiveSession: keyDir='%s' falta client/key/uniqueid -> no reanudar", kd.c_str());
                return false;
            }
        }
    }
    // Do not reopen the active-session dialog while the UI is resuming it.
    if (m_resume_in_progress.count(address) > 0)
    {
        vita_log::info("[GameStreamClient] probeActiveSession: omitiendo notificación de sesión activa para %s porque resume está en progreso (resume_in_progress_count=%d)",
            address.c_str(), (int)m_resume_in_progress.count(address));
        return false;
    }

    auto itAttempt = m_resume_attempts.find(address);
    if (itAttempt != m_resume_attempts.end())
    {
        auto now                  = std::chrono::steady_clock::now();
        auto ageMs                = std::chrono::duration_cast<std::chrono::milliseconds>(now - itAttempt->second).count();
        const long long kExpireMs = 10000;
        if (ageMs >= 0 && ageMs < kExpireMs)
        {
            vita_log::info("[GameStreamClient] probeActiveSession: omitiendo notificación para %s porque resumeAttempt reciente (ageMs=%lld)", address.c_str(), ageMs);
            return false;
        }
        else
        {
            vita_log::info("[GameStreamClient] probeActiveSession: resumeAttempt para %s expiró (ageMs=%lld) -> continuar comprobación", address.c_str(), ageMs);
            m_resume_attempts.erase(itAttempt);
        }
    }
    if (sd.currentGame == 0)
        return false;

    outRunning.id   = std::to_string(sd.currentGame);
    outRunning.name = "";

    std::vector<RemoteAppInfo> apps;
    getAppList(address, [&apps](const std::vector<RemoteAppInfo>& a)
        { apps = a; });
    for (const auto& a : apps)
    {
        if (a.id == outRunning.id)
        {
            outRunning.name = a.name;
            break;
        }
    }

    if (outRunning.name.empty())
    {
        outRunning.name = "Running session";
    }

    ActiveStream s;
    s.appId                   = sd.currentGame;
    s.appName                 = outRunning.name;
    m_active_streams[address] = s;
    vita_log::info("[GameStreamClient] probeActiveSession: registro m_active_streams[%s] = {{ appId=%d, appName='%s' }}", address.c_str(), sd.currentGame, outRunning.name.c_str());
    return true;
}

// libcurl requires a C-compatible callback on Vita.
static size_t sunshine_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total_size     = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

bool GameStreamClient::fetchSunshineServerinfo(const std::string& address, std::string& response)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    response.clear();

    std::string keyDir = getKeyDirFor(address);
    if (keyDir.empty())
    {
        vita_log::error("[GameStreamClient] fetchSunshineServerinfo: No keyDir for %s", address.c_str());
        return false;
    }

    std::string certPath     = keyDir + "/client.pem";
    std::string keyPath      = keyDir + "/key.pem";
    std::string uniqueidPath = keyDir + "/uniqueid.dat";

    std::string uniqueid;
    FILE* f = fopen(uniqueidPath.c_str(), "r");
    if (f)
    {
        char buf[32] = { 0 };
        if (fgets(buf, sizeof(buf), f))
        {
            uniqueid = buf;
            while (!uniqueid.empty() && (uniqueid.back() == '\n' || uniqueid.back() == '\r'))
            {
                uniqueid.pop_back();
            }
        }
        fclose(f);
    }

    if (uniqueid.empty())
    {
        vita_log::error("[GameStreamClient] fetchSunshineServerinfo: Failed to read uniqueid for %s", address.c_str());
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        vita_log::error("[GameStreamClient] fetchSunshineServerinfo: Failed to init curl");
        return false;
    }

    int port = 47984;
    if (m_server_data.count(address) > 0)
    {
        port = m_server_data[address].httpsPort;
        if (port == 0)
            port = 47984;
    }

    char urlBuf[512];
    snprintf(urlBuf, sizeof(urlBuf), "https://%s:%d/serverinfo?uniqueid=%s",
        address.c_str(), port, uniqueid.c_str());

    std::string response_string;

    curl_easy_setopt(curl, CURLOPT_URL, urlBuf);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certPath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLKEY, keyPath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sunshine_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

    CURLcode res   = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        vita_log::warning("[GameStreamClient] Sunshine serverinfo failed (curl error): %s", curl_easy_strerror(res));
        return false;
    }

    if (http_code != 200)
    {
        vita_log::warning("[GameStreamClient] Sunshine serverinfo HTTP error: %ld", http_code);
        return false;
    }

    response = std::move(response_string);
    return true;
}

bool GameStreamClient::parseSunshineCurrentGame(const std::string& response, int& outCurrentGame)
{
    outCurrentGame = 0;

    const std::string tagOpen  = "<currentgame>";
    const std::string tagClose = "</currentgame>";

    auto start = response.find(tagOpen);
    auto end   = response.find(tagClose);
    if (start == std::string::npos || end == std::string::npos || end <= start)
    {
        return false;
    }

    start += tagOpen.size();
    std::string value = response.substr(start, end - start);
    try
    {
        outCurrentGame = std::stoi(value);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

int GameStreamClient::getSunshinePairStatus(const std::string& address)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string response;
    if (!fetchSunshineServerinfo(address, response))
    {
        return 0;
    }

    if (response.find("<PairStatus>1</PairStatus>") != std::string::npos)
    {
        vita_log::info("[GameStreamClient] Sunshine validation success (PairStatus=1)");
        return 1;
    }

    vita_log::warning("[GameStreamClient] Sunshine validation returned unpaired/unknown");
    return 0;
}
