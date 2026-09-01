#ifndef GAMESTREAM_CLIENT_HPP
#define GAMESTREAM_CLIENT_HPP

// Borealis must precede Limelight to avoid BUTTON_* macro conflicts.
// clang-format off
#include "borealis.hpp"
#include "Limelight.h"
// clang-format on

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "client.h"
#include "errors.h"

struct RemoteAppInfo
{
    std::string id;
    std::string name;
    std::string iconUrl;
};

struct HostInfo; // forward

typedef std::function<void(const std::vector<RemoteAppInfo>&)> AppListCallback;
typedef std::function<void(bool)> BoolCallback;

class GameStreamClient
{
  public:
    static GameStreamClient& instance();

    bool connect(const std::string& address);
    bool connect(const HostInfo& host); // use consistent safeId
    bool isConnected(const std::string& address);

    // Get data from server
    SERVER_DATA& serverData(const std::string& address);

    // Start application (returns true if started successfully)
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId);
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, int displayWidth, int displayHeight);

    enum class StartMode
    {
        AUTO        = 0, // Allow automatic resume if there is an active session
        RESUME_ONLY = 1, // Just resume, fail if no session active
        NEW_ONLY    = 2 // Always fresh launch, ignore active session
    };
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, StartMode mode, int displayWidth = 0, int displayHeight = 0);
    bool resumeApp(const std::string& address, STREAM_CONFIGURATION& config, int appId);
    bool lastStreamConfig(const std::string& address, STREAM_CONFIGURATION& out) const;

    // Pairing
    bool pair(const std::string& address, const std::string& pin);
    bool unpair(const std::string& address);
    bool isPaired(const std::string& address);
    bool beginPairing(const HostInfo& host, std::function<void(bool)> onFinished, std::function<void(const std::string&)> onPinReady = nullptr);

    void getAppList(const std::string& address, AppListCallback callback);

    bool getAppBoxart(const std::string& address, int appId, Data& outData);

    bool quitApp(const std::string& address);

    void setActiveStream(const std::string& address, int appId, const std::string& appName);
    void clearActiveStream(const std::string& address);
    bool hasActiveStream(const std::string& address) const;
    RemoteAppInfo activeAppInfo(const std::string& address) const; // returns generic appName/icon if it exists

    bool probeActiveSession(const HostInfo& host, RemoteAppInfo& outRunning);

    std::string getKeyDirFor(const std::string& address) const;

    int getSunshinePairStatus(const std::string& address);

  private:
    GameStreamClient();
    ~GameStreamClient();

    bool fetchSunshineServerinfo(const std::string& address, std::string& response);
    bool parseSunshineCurrentGame(const std::string& response, int& outCurrentGame);

    std::map<std::string, SERVER_DATA> m_server_data;
    std::map<std::string, STREAM_CONFIGURATION> m_last_stream_cfg; // address -> last released config
    std::map<std::string, std::vector<RemoteAppInfo>> m_app_lists;
    std::map<std::string, std::string> m_key_dirs;
    struct ActiveStream
    {
        int appId;
        std::string appName;
    };
    std::map<std::string, ActiveStream> m_active_streams;
    // Suppresses duplicate active-session prompts while a resume is pending.
    std::set<std::string> m_resume_in_progress;
    std::map<std::string, std::chrono::steady_clock::time_point> m_resume_attempts;
};

#endif // GAMESTREAM_CLIENT_HPP
