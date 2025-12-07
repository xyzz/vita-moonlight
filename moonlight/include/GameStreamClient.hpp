#ifndef GAMESTREAM_CLIENT_HPP
#define GAMESTREAM_CLIENT_HPP

// Incluir Borealis primero para evitar conflictos con constantes BUTTON_*
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "borealis.hpp"

// Headers de Limelight después de Borealis
#include "Limelight.h"
#include "client.h"
#include "errors.h"

// RemoteAppInfo: estructura ligera usada por la UI para listar apps remotas
struct RemoteAppInfo
{
    std::string id;
    std::string name;
    std::string iconUrl;
};

struct HostInfo; // forward

// NOTE: ConnectionManager compatibility shim removed. Use GameStreamClient::getAppList
// and GameStreamClient::connect directly.

typedef std::function<void(const std::vector<RemoteAppInfo>&)> AppListCallback;
typedef std::function<void(bool)> BoolCallback;

class GameStreamClient
{
  public:
    static GameStreamClient& instance();

    // Inicialización del servidor
    bool connect(const std::string& address);
    bool connect(const HostInfo& host); // usa safeId consistente
    bool isConnected(const std::string& address);

    // Obtener datos del servidor
    SERVER_DATA& serverData(const std::string& address);

    // Iniciar aplicación (devuelve true si se inició correctamente)
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId);
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, int displayWidth, int displayHeight);

    // Enum para controlar el comportamiento de startApp
    enum class StartMode
    {
        AUTO        = 0, // Permitir resume automático si hay sesión activa
        RESUME_ONLY = 1, // Solo resume, fallar si no hay sesión activa
        NEW_ONLY    = 2 // Siempre fresh launch, ignorar sesión activa
    };
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, StartMode mode, int displayWidth = 0, int displayHeight = 0);
    // Recuperar última configuración usada (incluye remoteInputAesKey/IV si ya se generó)
    bool lastStreamConfig(const std::string& address, STREAM_CONFIGURATION& out) const;

    // Pairing
    bool pair(const std::string& address, const std::string& pin);
    bool unpair(const std::string& address);
    bool isPaired(const std::string& address);
    // Flujo completo de pairing con popup (PIN auto-generado) replicando comportamiento original
    bool beginPairing(const HostInfo& host, std::function<void(bool)> onFinished, std::function<void(const std::string&)> onPinReady = nullptr);

    // Obtener lista de aplicaciones
    void getAppList(const std::string& address, AppListCallback callback);

    // Terminar aplicación
    bool quitApp(const std::string& address);

    // Gestión de sesión activa
    void setActiveStream(const std::string& address, int appId, const std::string& appName);
    void clearActiveStream(const std::string& address);
    bool hasActiveStream(const std::string& address) const;
    RemoteAppInfo activeAppInfo(const std::string& address) const; // devuelve appName/icon genérico si existe

    // Probar si existe una sesión activa para un host (local o remoto). Si hay una
    // sesión activa, rellena outRunning con id/nombre y retorna true.
    // Esta función encapsula la lógica de connect()+consulta a SERVER_DATA.currentGame
    // y la resolución del nombre de la app mediante getAppList.
    bool probeActiveSession(const HostInfo& host, RemoteAppInfo& outRunning);

    // Devuelve el keyDir calculado/guardado para una dirección si existe
    std::string getKeyDirFor(const std::string& address) const;

    // Obtiene el PairStatus de Sunshine mediante petición HTTPS a /serverinfo
    // Retorna 1 si está emparejado, 0 si no o error
    int getSunshinePairStatus(const std::string& address);

  private:
    GameStreamClient();
    ~GameStreamClient();

    std::map<std::string, SERVER_DATA> m_server_data;
    std::map<std::string, STREAM_CONFIGURATION> m_last_stream_cfg; // address -> última config lanzada
    std::map<std::string, std::vector<RemoteAppInfo>> m_app_lists;
    std::map<std::string, std::string> m_key_dirs; // address -> keyDir usado en gs_init
    struct ActiveStream
    {
        int appId;
        std::string appName;
    };
    std::map<std::string, ActiveStream> m_active_streams; // address -> active stream
    // Marcar reanudos en progreso para evitar que probeActiveSession reabriera
    // el diálogo de "Active Session" durante el intento de resume iniciado
    // desde la UI. La clave es la dirección (ip) del host.
    std::set<std::string> m_resume_in_progress;
    // Marcas de intento de resume con expiración para evitar reaparición del diálogo
    // cuando la UI se recrea. Mapea address -> time_point de inicio.
    std::map<std::string, std::chrono::steady_clock::time_point> m_resume_attempts;
};

#endif // GAMESTREAM_CLIENT_HPP
