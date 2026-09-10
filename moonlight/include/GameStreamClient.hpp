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
#include <mutex>
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

struct HostInfo;

typedef std::function<void(const std::vector<RemoteAppInfo>&)> AppListCallback;

class GameStreamClient
{
  public:
    static GameStreamClient& instance();

    bool connect(const HostInfo& host);
    bool isConnected(const std::string& address);

    bool serverInfo(const std::string& address, SERVER_INFORMATION& out) const;
    int currentGame(const std::string& address) const;
    enum class StartMode
    {
        AUTO        = 0,
        RESUME_ONLY = 1,
        NEW_ONLY    = 2
    };
    bool startApp(const std::string& address, STREAM_CONFIGURATION& config, int appId, StartMode mode, int displayWidth = 0, int displayHeight = 0);
    bool resumeApp(const std::string& address, STREAM_CONFIGURATION& config, int appId);
    bool lastStreamConfig(const std::string& address, STREAM_CONFIGURATION& out) const;

    bool unpair(const std::string& address);
    bool beginPairing(const HostInfo& host, std::function<void(bool)> onFinished, std::function<void(const std::string&)> onPinReady = nullptr);

    void getAppList(const std::string& address, AppListCallback callback);
    bool getAppBoxart(const std::string& address, int appId, Data& outData);
    bool quitApp(const std::string& address);

    void setActiveStream(const std::string& address, int appId, const std::string& appName);
    void clearActiveStream(const std::string& address);

    bool probeActiveSession(const HostInfo& host, RemoteAppInfo& outRunning);

    std::string getKeyDirFor(const std::string& address) const;

    int getSunshinePairStatus(const std::string& address);

  private:
    GameStreamClient()  = default;
    ~GameStreamClient() = default;

    bool fetchSunshineServerinfo(const std::string& address, std::string& response);
    bool parseSunshineCurrentGame(const std::string& response, int& outCurrentGame);

    mutable std::recursive_mutex m_mutex;
    std::map<std::string, SERVER_DATA> m_server_data;
    std::map<std::string, STREAM_CONFIGURATION> m_last_stream_cfg;
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
