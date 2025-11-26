#include "GameStreamClient.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include "ConfigManager.hpp"
#include "audio/MicrophoneManager.hpp"
#include "crypto/CryptoManager.hpp"
#include "model/HostStorage.hpp"

// ConnectionManager compatibility shim removed; callers should use
// GameStreamClient::getAppList() and GameStreamClient::connect() directly.

// Directory creation is centralized in ConfigManager (ensureDirExists / ensureKeyDirExists)

GameStreamClient& GameStreamClient::instance()
{
    static GameStreamClient instance;
    return instance;
}

GameStreamClient::GameStreamClient()
{
    // Constructor vacío
}

GameStreamClient::~GameStreamClient()
{
    // Limpiar datos de servidores
    m_server_data.clear();
    m_app_lists.clear();
}

bool GameStreamClient::connect(const std::string& address)
{
    HostInfo h;
    h.ip     = address;
    h.name   = address;
    h.safeId = makeSafeHostId(address);
    return connect(h);
}

bool GameStreamClient::connect(const HostInfo& host)
{
    if (host.ip.empty())
    {
        brls::Logger::error("[GameStreamClient] Dirección vacía en connect(host)");
        return false;
    }
    std::string addr = host.ip;
    if (m_server_data.count(addr) > 0)
    {
        brls::Logger::info("[GameStreamClient] Ya conectado a {}", addr);
        return true;
    }
    SERVER_DATA serverData {};
    ConfigManager cfg;
    cfg.load();
    std::string baseDevices = cfg.getKeysDir();
    std::string safe        = host.safeId.empty() ? makeSafeHostId(host.name.empty() ? host.ip : host.name) : host.safeId;
    // Resolver posible keyDir existente (migración de versiones anteriores)
    auto resolveExistingKeyDir = [&](const std::string& base, const HostInfo& h) -> std::string
    {
        std::vector<std::string> candidates;
        // 1. Nuevo esquema (safeId derivado del nombre)
        candidates.push_back(base + "/" + safe);
        // 2. Nombre sin dominio (antes quizá se guardaba sin .local)
        if (!h.name.empty())
        {
            auto pos = h.name.find('.');
            if (pos != std::string::npos)
            {
                std::string shortName = h.name.substr(0, pos);
                candidates.push_back(base + "/" + makeSafeHostId(shortName));
            }
        }
        // 3. IP cruda
        if (!h.ip.empty())
            candidates.push_back(base + "/" + h.ip);
        // 4. safeId generado de la IP
        if (!h.ip.empty())
            candidates.push_back(base + "/" + makeSafeHostId(h.ip));
        // El primero que contenga device.ini lo usamos
        for (auto& c : candidates)
        {
            struct stat st {};
            if (stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                std::string devIni = c + "/device.ini";
                if (stat(devIni.c_str(), &st) == 0)
                {
                    brls::Logger::info("[GameStreamClient][keyDir-resolver] Reutilizando keyDir existente '{}' (device.ini encontrado)", c);
                    return c;
                }
            }
        }
        // Si ninguno tiene device.ini, devolvemos el nuevo esquema
        return candidates.front();
    };
    std::string keyDir = resolveExistingKeyDir(baseDevices, host);
    brls::Logger::info("[GameStreamClient] keyDir elegido para '{}' => '{}' (safeId base='{}')", addr, keyDir, safe);
    // Diagnóstico keyDir
    {
        struct stat st {};
        if (stat(keyDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        {
            brls::Logger::info("[GameStreamClient][keyDir] EXISTE '{}'", keyDir);
            DIR* d = opendir(keyDir.c_str());
            if (d)
            {
                struct dirent* ent;
                std::string files;
                while ((ent = readdir(d)) != nullptr)
                {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                        continue;
                    files += ent->d_name;
                    files += ",";
                }
                closedir(d);
                brls::Logger::info("[GameStreamClient][keyDir] Archivos: {}", files.empty() ? "(vacío)" : files);
            }
            else
            {
                brls::Logger::error("[GameStreamClient][keyDir] No se pudo abrir directorio '{}'", keyDir);
            }
        }
        else
        {
            brls::Logger::warning("[GameStreamClient][keyDir] NO EXISTE '{}' antes de gs_init", keyDir);
        }
    }
    int status = gs_init(&serverData, addr, keyDir);
    if (status != GS_OK)
    {
        brls::Logger::error("[GameStreamClient] Error al inicializar servidor: {}", status);
        return false;
    }
    // Guardar keyDir usado para este server para diagnósticos y comprobaciones
    m_key_dirs[addr]    = keyDir;
    m_server_data[addr] = serverData;
    brls::Logger::info("[GameStreamClient] Conexión exitosa a {}", addr);
    const char* srv_addr = serverData.serverInfo.address ? serverData.serverInfo.address : "NULL";
    brls::Logger::info("[GameStreamClient] ServerInfo address: {}", srv_addr);

    // Diagnóstico: comparar device.ini.paired (si existe) con lo que reporta el
    // servidor (serverData.paired). Esto ayuda a detectar desincronizaciones.
    {
        std::string deviceIni = keyDir + "/device.ini";
        struct stat st {};
        if (stat(deviceIni.c_str(), &st) == 0)
        {
            // intentamos leer el campo paired informativo ya cargado por HostStorage
            brls::Logger::info("[GameStreamClient] Diagnóstico: device.ini presente en '{}' y server.paired={}", deviceIni, serverData.paired);
        }
        else
        {
            brls::Logger::info("[GameStreamClient] Diagnóstico: device.ini NO presente en '{}' y server.paired={}", keyDir, serverData.paired);
        }
    }

    // Si existe uniqueid.dat en keyDir, reescribimos device.ini para asegurar que
    // el archivo exista y refleje valores actuales (paired/ip/port). NOTE: no
    // escribimos el campo `uuid` en device.ini — eso es intencional y mantiene
    // compatibilidad con Moonlight-Switch, que no persiste `uuid` en device.ini.
    // Solo crear/actualizar device.ini si NO existe. Si ya existe (por ejemplo
    // tras un emparejamiento previo), no debemos sobrescribirla con el valor
    // `serverData.paired` que puede reportar el servidor en estados temporales
    // (esto provocaba que device.ini pasara de paired=true a paired=false).
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
            brls::Logger::warning("[GameStreamClient] No se pudo crear device.ini para {}", addr);
        }
    }
    else
    {
        brls::Logger::info("[GameStreamClient] device.ini ya existe en '{}' -> no se sobreescribe paired", deviceIniPath);
        // Si device.ini ya existe pero el servidor nos reporta un MAC válido,
        // intentamos actualizar solo la línea mac= sin sobrescribir el resto.
        if (!serverData.mac.empty() && serverData.mac != "00:00:00:00:00:00")
        {
            brls::Logger::info("[GameStreamClient] Observado MAC '{}' para {} -> (sin acción: persistencia gestionada por flujo de pairing)", serverData.mac, addr);
        }
    }
    return true;
}

bool GameStreamClient::isConnected(const std::string& address)
{
    return m_server_data.count(address) > 0;
}

SERVER_DATA& GameStreamClient::serverData(const std::string& address)
{
    if (m_server_data.count(address) == 0)
    {
        brls::Logger::error("[GameStreamClient] No hay datos para servidor {}", address);
        // Esto debería manejarse mejor, pero por ahora devolveremos una referencia inválida
        static SERVER_DATA empty;
        return empty;
    }
    return m_server_data[address];
}

std::string GameStreamClient::getKeyDirFor(const std::string& address) const
{
    auto it = m_key_dirs.find(address);
    if (it == m_key_dirs.end())
        return std::string();
    return it->second;
}

bool GameStreamClient::startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId)
{
    return startApp(address, config, appId, StartMode::AUTO, 0, 0);
}

bool GameStreamClient::startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, int displayWidth, int displayHeight)
{
    return startApp(address, config, appId, StartMode::AUTO, displayWidth, displayHeight);
}

bool GameStreamClient::startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, StartMode mode, int displayWidth, int displayHeight)
{
    if (m_server_data.count(address) == 0)
    {
        brls::Logger::error("[GameStreamClient] No conectado a {}", address);
        return false;
    }
    brls::Logger::info("[GameStreamClient] Iniciando aplicación {} en {} (mode={}) display={}x{}", appId, address, (int)mode, displayWidth, displayHeight);

    // Si pedimos RESUME_ONLY, comprobar que hay currentGame
    if (mode == StartMode::RESUME_ONLY)
    {
        SERVER_DATA& sd = m_server_data[address];
        if (sd.currentGame == 0)
        {
            brls::Logger::warning("[GameStreamClient] Resume solicitado pero server.currentGame==0 para {}", address);
            return false;
        }
    }

    // Si estamos intentando un resume pedido por el usuario, marcarlo para que
    // probeActiveSession no vuelva a mostrar el diálogo de "Active Session"
    // mientras el intento de reanudar esté en progreso.
    bool markedResume = false;
    if (mode == StartMode::RESUME_ONLY)
    {
        brls::Logger::info("[GameStreamClient] Marcando resume en progreso para {}", address);
        m_resume_in_progress.insert(address);
        markedResume = true;
        // Registrar intento de resume con expiración para cubrir casos donde
        // la UI se recrea antes de que setActiveStream sea llamado.
        m_resume_attempts[address] = std::chrono::steady_clock::now();
    }

    // Si pedimos NEW_ONLY, forzamos fresh launch a través del flag global usado por libgamestream
    extern bool g_force_fresh_launch_h264; // definido en vita_session_globals.cpp
    bool prevForce = g_force_fresh_launch_h264;
    if (mode == StartMode::NEW_ONLY)
        g_force_fresh_launch_h264 = true;

    int status = gs_start_app(&m_server_data[address], &config, appId, true, true, 0x1, displayWidth, displayHeight);

    // Restaurar flag global
    if (mode == StartMode::NEW_ONLY)
        g_force_fresh_launch_h264 = prevForce;

    if (status != GS_OK)
    {
        // En caso de fallo, limpiar la marca de resume para permitir reintentos posteriores
        if (markedResume)
        {
            brls::Logger::info("[GameStreamClient] Resume falló para {} -> limpiando resume en progreso (status={})", address, status);
            m_resume_in_progress.erase(address);
            m_resume_attempts.erase(address);
        }
        brls::Logger::error("[GameStreamClient] Error al iniciar aplicación: {}", status);
        return false;
    }

    // NOTA: en caso de éxito dejamos la marca `m_resume_in_progress` activa hasta que
    // la propia vista confirme que la sesión ha arrancado. Esto evita condiciones de
    // carrera donde probeActiveSession vuelva a anunciar la sesión activa entre el
    // retorno de gs_start_app y la creación efectiva de VitaSession.

    // Guardar copia de la configuración (incluye remoteInputAesKey rellenada por gs_start_app)
    m_last_stream_cfg[address] = config;
    brls::Logger::info("[GameStreamClient] Aplicación iniciada correctamente (remoteInputKey set)");

    ConfigManager micConfig;
    micConfig.load();
    VideoSettings micSettings = micConfig.getVideoSettings();

    if (micSettings.enable_microphone)
    {
        brls::Logger::info("[GameStreamClient] Microphone enabled in settings - starting transmission");

        std::string micHost = micSettings.microphone_host_ip.empty()
            ? address
            : micSettings.microphone_host_ip;

        bool micStarted = MicrophoneManager::getInstance().start(
            micHost,
            micSettings.microphone_port,
            micSettings.microphone_sample_rate,
            micSettings.microphone_channels,
            micSettings.microphone_bitrate);

        if (micStarted)
        {
            brls::Logger::info("[GameStreamClient] Microphone started successfully to {}:{}",
                micHost, micSettings.microphone_port);
        }
        else
        {
            brls::Logger::warning("[GameStreamClient] Microphone failed to start - will retry every 10s");
        }
    }
    else
    {
        brls::Logger::debug("[GameStreamClient] Microphone disabled in settings");
    }

    return true;
}

bool GameStreamClient::lastStreamConfig(const std::string& address, STREAM_CONFIGURATION& out) const
{
    auto it = m_last_stream_cfg.find(address);
    if (it == m_last_stream_cfg.end())
        return false;
    out = it->second;
    return true;
}

bool GameStreamClient::pair(const std::string& address, const std::string& pin)
{
    if (m_server_data.count(address) == 0)
    {
        brls::Logger::error("[GameStreamClient] pair: no conectado a {}", address);
        return false;
    }
    SERVER_DATA& s = m_server_data[address];
    if (s.paired)
    {
        brls::Logger::info("[GameStreamClient] pair: ya emparejado");
        return true;
    }
    char pinBuf[16];
    memset(pinBuf, 0, sizeof(pinBuf));
    strncpy(pinBuf, pin.c_str(), sizeof(pinBuf) - 1);
    int status = gs_pair(&s, pinBuf);
    if (status != GS_OK)
    {
        brls::Logger::error("[GameStreamClient] pair fallo status={}", status);
        return false;
    }
    brls::Logger::info("[GameStreamClient] pairing OK");
    // NOTA: no limpiamos `currentGame` aquí para evitar discrepancias con el
    // estado real reportado por el servidor. Antes se forzaba a 0 para evitar
    // que la UI mostrara una sesión activa inmediatamente tras el pairing,
    // pero eso puede causar que `gs_start_app` falle con "An app is already
    // running" si el servidor realmente tiene una sesión activa. Ahora
    // conservamos el valor proporcionado por el servidor.
    // Invalidar caché de applist para este host: tras emparejar queremos forzar
    // que la próxima obtención de apps solicite al servidor la lista actual.
    if (m_app_lists.count(address) > 0)
        m_app_lists.erase(address);
    // Si después del pair el servidor nos proporcionó un MAC válido, actualizar device.ini
    std::string kd = getKeyDirFor(address);
    if (!kd.empty() && !s.mac.empty() && s.mac != "00:00:00:00:00:00")
    {
        brls::Logger::info("[GameStreamClient] pair(): MAC '{}' detectado -> (sin acción: persistencia gestionada por flujo de pairing)", s.mac, kd);
    }
    return true;
}

bool GameStreamClient::unpair(const std::string& address)
{
    if (m_server_data.count(address) == 0)
        return false;
    SERVER_DATA& s = m_server_data[address];
    int status     = gs_unpair(&s);
    if (status != GS_OK)
    {
        brls::Logger::error("[GameStreamClient] unpair fallo status={}", status);
        return false;
    }
    s.paired = false;
    return true;
}

bool GameStreamClient::isPaired(const std::string& address)
{
    if (m_server_data.count(address) == 0)
        return false;
    return m_server_data[address].paired;
}

bool GameStreamClient::beginPairing(const HostInfo& host, std::function<void(bool)> onFinished, std::function<void(const std::string&)> onPinReady)
{
    HostInfo localHost = host;
    if (localHost.safeId.empty())
        localHost.safeId = makeSafeHostId(localHost.name.empty() ? localHost.ip : localHost.name);
    std::string addr = localHost.ip;
    if (addr.empty())
    {
        brls::Logger::error("[GameStreamClient] beginPairing: host ip vacío");
        if (onFinished)
            onFinished(false);
        return false;
    }

    // Fast-path: si existe device.ini intentamos conectar y verificar si el
    // servidor considera el cliente emparejado. Si el servidor ya lo reconoce,
    // podemos devolver success inmediatamente; si no, continuamos con el flujo
    // normal de pairing (gs_init + PIN + gs_pair) para actualizar el estado.
    {
        ConfigManager cfg;
        cfg.load();
        std::string base = cfg.getKeysDir();
        std::vector<std::string> candidates;
        candidates.push_back(base + "/" + localHost.safeId);
        if (!localHost.name.empty())
        {
            auto pos = localHost.name.find('.');
            if (pos != std::string::npos)
                candidates.push_back(base + "/" + makeSafeHostId(localHost.name.substr(0, pos)));
        }
        if (!localHost.ip.empty())
        {
            candidates.push_back(base + "/" + localHost.ip);
            candidates.push_back(base + "/" + makeSafeHostId(localHost.ip));
        }
        struct stat st {};
        bool foundDeviceIni = false;
        for (auto& c : candidates)
        {
            std::string di = c + "/device.ini";
            if (stat(di.c_str(), &st) == 0)
            {
                brls::Logger::info("[Pairing][fast-path] device.ini existente en '{}' -> verificar estado en servidor", c);
                foundDeviceIni = true;
                break;
            }
        }
        if (foundDeviceIni)
        {
            // Intentar usar el keyDir exacto donde existe device.ini para comprobar
            // si el servidor reconoce el emparejamiento. Esto reproduce el
            // comportamiento de Moonlight-Switch: usar el mismo keyDir que
            // contiene device.ini en lugar de dejar que connect() resuelva otro
            // candidato (evita desajustes entre device.ini y los certs/uniqueid).
            std::string foundKeyDir;
            for (auto& c : candidates)
            {
                std::string di = c + "/device.ini";
                struct stat st {};
                if (stat(di.c_str(), &st) == 0)
                {
                    foundKeyDir = c;
                    break;
                }
            }
            if (!foundKeyDir.empty())
            {
                try
                {
                    SERVER_DATA tmp;
                    brls::Logger::info("[Pairing][fast-path] intentando gs_init con keyDir='{}' para comprobar paired en servidor", foundKeyDir);
                    int initRes = gs_init(&tmp, localHost.ip, foundKeyDir);
                    if (initRes == GS_OK)
                    {
                        if (tmp.paired)
                        {
                            brls::Logger::info("[Pairing][fast-path] servidor reconoce emparejamiento usando keyDir='{}' -> omitir pairing", foundKeyDir);
                            // Guardar serverData mínimo en el mapa local para uso futuro
                            m_server_data[localHost.ip] = tmp;
                            if (onFinished)
                                onFinished(true);
                            return true;
                        }
                        else
                        {
                            brls::Logger::info("[Pairing][fast-path] server PairStatus=0 usando keyDir='{}' -> continuar pairing", foundKeyDir);
                        }
                    }
                    else
                    {
                        brls::Logger::warning("[Pairing][fast-path] gs_init falló ({} ) con keyDir='{}' -> continuar pairing", initRes, foundKeyDir);
                    }
                }
                catch (...)
                {
                    // En caso de error no bloquear el flujo de pairing
                    brls::Logger::error("[Pairing][fast-path] excepción al intentar gs_init con keyDir='{}'", foundKeyDir);
                }
            }
        }
    }

    // Crear diálogo (spinner + label) no cancelable como antes
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

    // Flags
    auto finished  = std::make_shared<std::atomic<bool>>(false);
    auto cancelled = std::make_shared<std::atomic<bool>>(false); // por compatibilidad si en futuro se agrega cancelar

    // Hilo principal pairing
    std::thread([=]() mutable
        {
        auto t_start = std::chrono::high_resolution_clock::now();
        ConfigManager cfg; cfg.load();
        std::string base = cfg.getKeysDir();
        std::string keyDir = base + "/" + localHost.safeId;
        // Asegurar directorio (simplificado, sin borrar recursivamente)
        struct stat st{};
        // Resolución de keyDir reutilizando lógica de connect para evitar duplicado
        ConfigManager tmpCfg; tmpCfg.load();
        std::string baseDir = tmpCfg.getKeysDir();
        // Reusar mini-resolver simple: probar safeId, nombre sin dominio, ip
        std::vector<std::string> candidates;
        candidates.push_back(baseDir + "/" + localHost.safeId);
        if (!localHost.name.empty()) {
            auto pos = localHost.name.find('.');
            if (pos != std::string::npos) {
                candidates.push_back(baseDir + "/" + makeSafeHostId(localHost.name.substr(0,pos)));
            }
        }
        if (!localHost.ip.empty()) {
            candidates.push_back(baseDir + "/" + localHost.ip);
            candidates.push_back(baseDir + "/" + makeSafeHostId(localHost.ip));
        }
        // (fast-path ya evaluado antes de crear el diálogo)
        // Si no existe, procedemos flujo normal
        // Asegurar existencia del directorio usando ConfigManager centralizado
        if (!ConfigManager().ensureKeyDirExists(localHost.safeId)) {
            brls::Logger::error("[Pairing] No se pudo crear/asegurar keyDir '{}'", keyDir);
        } else {
            brls::Logger::info("[Pairing] keyDir asegurado {}", keyDir);
        }
        // Listado inicial
        {
            DIR* d = opendir(keyDir.c_str());
            if (d) {
                struct dirent* ent; std::string files;
                while ((ent = readdir(d)) != nullptr) {
                    if (strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
                    files += ent->d_name; files += ",";
                }
                closedir(d);
                brls::Logger::info("[Pairing][keyDir] Inicial archivos: {}", files.empty()? "(vacío)" : files);
            }
        }

        SERVER_DATA serverData{};
        // Antes de inicializar, limpiar cualquier certificado en caché para
        // forzar la regeneración desde cero. Esto evita que m_cert/m_key en
        // OpenSSLCryptoManager permanezcan en memoria y eviten la generación
        // de nuevos certificados al emparejar de nuevo.
        try {
            // Borrar client.pem, key.pem y uniqueid.dat para forzar regeneración completa
            namespace fs = std::filesystem;
            std::string clientPem = keyDir + "/client.pem";
            std::string keyPem = keyDir + "/key.pem";
            std::string uid = keyDir + "/uniqueid.dat";
            std::error_code ec;
            if (fs::exists(clientPem)) fs::remove(clientPem, ec);
            if (fs::exists(keyPem)) fs::remove(keyPem, ec);
            if (fs::exists(uid)) fs::remove(uid, ec);
            // Limpiar caché en memoria y cualquier resto en disco
            CryptoManager::remove_cert_key_pair(keyDir);
        } catch (...) {
            brls::Logger::warning("[Pairing] No se pudo limpiar cache de certificados para keyDir='{}'", keyDir);
        }
        // gs_init (bloqueante aquí, podemos optimizar con hilo nativo Vita más adelante)
    int initRes = gs_init(&serverData, addr, keyDir);
        if (initRes != GS_OK) {
            brls::Logger::error("[Pairing] gs_init fallo {}", initRes);
            brls::sync([dialog, label]() { label->setText(brls::getStr("host_dialog/pairing_error_init")); });
            std::this_thread::sleep_for(std::chrono::milliseconds(900));
            *finished = true;
            if (onFinished) onFinished(false);
            return;
        }
        // Listado tras gs_init (deberían existir certificados)
        {
            DIR* d = opendir(keyDir.c_str());
            if (d) {
                struct dirent* ent; std::string files;
                while ((ent = readdir(d)) != nullptr) {
                    if (strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
                    files += ent->d_name; files += ",";
                }
                closedir(d);
                brls::Logger::info("[Pairing][keyDir] Post-gs_init archivos: {}", files.empty()? "(vacío)" : files);
            }
        }
        // Generar PIN
        // Si el host está ocupado (currentGame != 0) vamos a forzar el emparejado.
        // Legacy clients a veces permitían forzar pairing aún con sesión activa.
        bool forced = false;
        int oldCurrent = serverData.currentGame;
        if (serverData.currentGame != 0) {
            forced = true;
            brls::Logger::warning("[Pairing] host ocupado (currentGame={}) -> forzando emparejado (puede fallar)", serverData.currentGame);
            brls::sync([label]() {
                label->setText(brls::getStr("host_dialog/pairing_force_warning"));
            });
            // Forzar que gs_pair no rechace por estado local
            serverData.currentGame = 0;
            // Dar un pequeño retardo para que el usuario vea el mensaje
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        }

        char pin[5];
        srand((unsigned int)time(nullptr));
        snprintf(pin, sizeof(pin), "%d%d%d%d", rand()%10, rand()%10, rand()%10, rand()%10);
        std::string pinStr(pin);
        brls::Logger::info("[Pairing] PIN generado {}", pinStr);
        brls::sync([label, pinStr, spinner]() {
            std::string msg = brls::getStr("host_dialog/pairing_enter_pin");
            size_t pos = msg.find("$(pin)"); if (pos != std::string::npos) msg.replace(pos, 6, pinStr);
            label->setText(msg);
            spinner->setVisibility(brls::Visibility::GONE);
        });
        // Notificar al llamador que el PIN está listo (por ejemplo AddHostTab) para
        // que pueda desbloquear inputs o mostrar el PIN en UI externa.
        if (onPinReady) onPinReady(pinStr);
        // Si hay un callback onPinReady (pasado por el llamador), invocarlo con el PIN
        // fuera del sync para no tocar UI desde hilos no-UI.
        if (onFinished) {
            // No hay parámetro onPinReady aquí; dejar al llamador usar onFinished o
            // la sobrecarga añadida en la cabecera (el llamador puede pasar onPinReady)
        }
        // Llamar gs_pair
            int pairRes = gs_pair(&serverData, pin);
        // Restaurar currentGame si lo modificamos
        if (forced) {
            serverData.currentGame = oldCurrent;
        }
        if (pairRes == GS_OK) {
            brls::Logger::info("[Pairing] Emparejamiento OK");
            // Forzar paired=true en serverData independientemente de la reconsulta HTTPS,
            // ya que el handshake de gs_pair fue exitoso. Esto evita que la UI muestre
            // "no emparejado" después de un pairing exitoso si /serverinfo HTTPS timeout.
            serverData.paired = true;
            // Tras un emparejamiento exitoso conservamos el valor de
            // `serverData.currentGame` que el servidor haya reportado (si
            // libgamestream hizo una re-consulta HTTPS probablemente ya
            // contiene el valor correcto). No forzamos a 0 para evitar
            // inconsistencias al solicitar el arranque de una app inmediatamente
            // después del emparejamiento.
            HostStorage::savePairedHost(localHost.safeId, addr, serverData.httpPort, serverData.paired, serverData.mac);
            // Actualizar device.ini (sin incluir uuid) para que la UI/almacenamiento
            // disponga de la información de paired/ip/port. Esto reproduce el
            // comportamiento de Moonlight-Switch donde device.ini no contiene el
            // uniqueid usado en las peticiones.
            HostStorage::writeDeviceIni(base + "/" + localHost.safeId, localHost.safeId, addr.c_str(), serverData.httpPort, serverData.paired, serverData.mac.empty() ? nullptr : serverData.mac.c_str());
            // Guardar serverData en mapa para futuro connect() reutilizable
            m_server_data[addr] = serverData;
            brls::sync([label]() { label->setText(brls::getStr("host_dialog/pairing_success")); });
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            *finished = true; if (onFinished) onFinished(true);
        } else {
            // Registrar gs_error procedente de libgamestream para diagnóstico
            std::string gse_str = gs_error();
            const char* gse = gse_str.empty() ? "(empty)" : gse_str.c_str();
            brls::Logger::error("[Pairing] gs_pair fallo {} -> gs_error='{}'", pairRes, gse);
            // También registrar contenido del keyDir para ayudar a reproducir
            std::string files;
            DIR* dchk = opendir(keyDir.c_str());
            if (dchk) {
                struct dirent* ent;
                while ((ent = readdir(dchk)) != nullptr) {
                    if (strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
                    files += ent->d_name; files += ",";
                }
                closedir(dchk);
            } else {
                files = "(no-open)";
            }
            brls::Logger::info("[Pairing][keyDir] Estado actual archivos: {}", files.empty()? "(vacío)" : files);
            std::string err = brls::getStr("host_dialog/pairing_error");
            brls::sync([label, err]() { label->setText(err); });
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            *finished = true; if (onFinished) onFinished(false);
        } })
        .detach();

    // Observador cierre
    std::thread([dialog, finished]()
        {
        while (!finished->load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        brls::sync([dialog]() {
            dialog->dismiss();
            brls::Application::unblockInputs();
        }); })
        .detach();
    return true;
}

void GameStreamClient::getAppList(const std::string& address, AppListCallback callback)
{
    if (m_server_data.count(address) == 0)
    {
        brls::Logger::error("[GameStreamClient] No conectado a {}", address);
        callback(std::vector<RemoteAppInfo>());
        return;
    }

    // Verificar si ya tenemos la lista en caché
    if (m_app_lists.count(address) > 0)
    {
        callback(m_app_lists[address]);
        return;
    }

    // Obtener lista de aplicaciones del servidor
    PAPP_LIST appList;
    int status = gs_applist(&m_server_data[address], &appList);
    if (status != GS_OK)
    {
        brls::Logger::error("[GameStreamClient] Error al obtener lista de aplicaciones: {}", status);
        callback(std::vector<RemoteAppInfo>());
        return;
    }

    // Guardar referencia a la cabeza de la lista para liberación posterior
    PAPP_LIST appListHead = appList;

    // Convertir a vector
    std::vector<RemoteAppInfo> apps;
    PAPP_LIST current = appList;
    while (current)
    {
        RemoteAppInfo app;
        app.id      = std::to_string(current->id);
        app.name    = current->name ? std::string(current->name) : "Unknown App";
        app.iconUrl = ""; // No tenemos iconUrl en la estructura PAPP_LIST
        apps.push_back(app);
        current = current->next;
    }

    // Liberar la memoria de la lista de aplicaciones
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

    // Ordenar por nombre
    std::sort(apps.begin(), apps.end(),
        [](const RemoteAppInfo& a, const RemoteAppInfo& b)
        {
            return a.name < b.name;
        });

    // Guardar en caché
    m_app_lists[address] = apps;

    callback(apps);
}

bool GameStreamClient::quitApp(const std::string& address)
{
    if (m_server_data.count(address) == 0)
    {
        brls::Logger::error("[GameStreamClient] No conectado a {}", address);
        return false;
    }

    brls::Logger::info("[GameStreamClient] Terminando aplicación en {}", address);

    int status = gs_quit_app(&m_server_data[address]);
    if (status != GS_OK)
    {
        brls::Logger::error("[GameStreamClient] Error al terminar aplicación: {}", status);
        return false;
    }

    brls::Logger::info("[GameStreamClient] Aplicación terminada correctamente");

    MicrophoneManager::getInstance().stop();
    brls::Logger::info("[GameStreamClient] Microphone stopped");

    // Limpiar estado de stream activo si coincide
    auto it = m_active_streams.find(address);
    if (it != m_active_streams.end())
    {
        m_active_streams.erase(it);
    }
    // Actualizar currentGame a 0 después de terminar la app
    m_server_data[address].currentGame = 0;
    return true;
}

void GameStreamClient::setActiveStream(const std::string& address, int appId, const std::string& appName)
{
    m_active_streams[address] = { appId, appName };
    // Si había un resume en progreso para este host, consideramos que la
    // sesión ya arrancó correctamente y limpiamos la marca para permitir
    // futuras detecciones/diálogos cuando corresponda.
    if (m_resume_in_progress.count(address) > 0)
    {
        brls::Logger::info("[GameStreamClient] setActiveStream: limpiando resume en progreso para {}", address);
        m_resume_in_progress.erase(address);
    }
}

void GameStreamClient::clearActiveStream(const std::string& address)
{
    m_active_streams.erase(address);

    MicrophoneManager::getInstance().stop();
    brls::Logger::debug("[GameStreamClient] clearActiveStream: microphone stopped");
}

bool GameStreamClient::hasActiveStream(const std::string& address) const
{
    return m_active_streams.count(address) > 0;
}

RemoteAppInfo GameStreamClient::activeAppInfo(const std::string& address) const
{
    RemoteAppInfo info;
    auto it = m_active_streams.find(address);
    if (it != m_active_streams.end())
    {
        info.id      = std::to_string(it->second.appId);
        info.name    = it->second.appName;
        info.iconUrl = ""; // placeholder
    }
    return info;
}

bool GameStreamClient::probeActiveSession(const HostInfo& host, RemoteAppInfo& outRunning)
{
    outRunning = RemoteAppInfo();
    if (host.ip.empty())
        return false;
    const std::string& address = host.ip;

    brls::Logger::info("[GameStreamClient] probeActiveSession ENTRY for {} (thread={})", address, std::to_string((long long)std::hash<std::thread::id>()(std::this_thread::get_id())));

    // Fast-path: si lo tenemos en memoria
    auto it = m_active_streams.find(address);
    if (it != m_active_streams.end())
    {
        outRunning.id      = std::to_string(it->second.appId);
        outRunning.name    = it->second.appName;
        outRunning.iconUrl = "";
        return true;
    }

    // Intentar conectar al host (gs_init) si no estamos conectados
    bool connected = isConnected(address);
    if (!connected)
    {
        connected = connect(host);
    }
    if (!connected)
        return false;

    // Inspeccionar serverData.currentGame
    SERVER_DATA& sd = serverData(address);
    // Diagnóstico: comprobar que el keyDir usado contiene certificados/uniqueid
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
                brls::Logger::info("[GameStreamClient] probeActiveSession: keyDir='{}' falta client/key/uniqueid -> no reanudar", kd);
                return false;
            }
        }
    }
    // Si el host no está emparejado, no podemos reanudar desde él
    // Comentado para permitir reanudar sesiones activas incluso si PairStatus=0
    //     brls::Logger::info("[GameStreamClient] probeActiveSession: host %s no emparejado según serverData -> no reanudar", address.c_str());
    // Si hay un resume en progreso iniciado por la UI para este host, evitar
    // que probeActiveSession indique que hay una sesión activa — así no se
    // reabrirá el diálogo de "Active Session" mientras el intento de resume
    // está en curso.
    if (m_resume_in_progress.count(address) > 0)
    {
        brls::Logger::info("[GameStreamClient] probeActiveSession: omitiendo notificación de sesión activa para {} porque resume está en progreso (resume_in_progress_count={})",
            address, (int)m_resume_in_progress.count(address));
        return false;
    }

    // Comprobar si hubo un intento reciente de resume (guard con expiración)
    auto itAttempt = m_resume_attempts.find(address);
    if (itAttempt != m_resume_attempts.end())
    {
        auto now                  = std::chrono::steady_clock::now();
        auto ageMs                = std::chrono::duration_cast<std::chrono::milliseconds>(now - itAttempt->second).count();
        const long long kExpireMs = 10000; // 10s
        if (ageMs >= 0 && ageMs < kExpireMs)
        {
            brls::Logger::info("[GameStreamClient] probeActiveSession: omitiendo notificación para {} porque resumeAttempt reciente (ageMs={})", address, ageMs);
            return false;
        }
        else
        {
            // Expiró el guard, borrar
            brls::Logger::info("[GameStreamClient] probeActiveSession: resumeAttempt para {} expiró (ageMs={}) -> continuar comprobación", address, ageMs);
            m_resume_attempts.erase(itAttempt);
        }
    }
    if (sd.currentGame == 0)
        return false;

    // Rellena ID
    outRunning.id   = std::to_string(sd.currentGame);
    outRunning.name = "";

    // Intentar resolver nombre por medio de la lista de apps
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
        // Nombre por defecto si no pudimos resolverlo
        outRunning.name = "Running session"; // texto por defecto; la UI puede sustituir por i18n
    }

    // Registrar en memoria para futuras consultas locales
    ActiveStream s;
    s.appId                   = sd.currentGame;
    s.appName                 = outRunning.name;
    m_active_streams[address] = s;
    brls::Logger::info("[GameStreamClient] probeActiveSession: registro m_active_streams[{}] = {{ appId={}, appName='{}' }}", address, sd.currentGame, outRunning.name);
    return true;
}
