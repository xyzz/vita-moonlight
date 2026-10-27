#include "session/session_app_select.hpp"

#include <sys/stat.h>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "audio/MicrophoneManager.hpp"
#include "client.h"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "moonmic/MoonmicBridge.hpp"
#include "moonmic/MoonmicPrep.hpp"
#include "session/session_main.hpp"
#include "session/stream_config.hpp"
#include "session/vita_session.hpp"
#include "utils/dialog_utils.h"
#include "view/pccard.hpp"

brls::Dialog* SessionAppSelect::showConnectingDialog(const std::string& msg, brls::Visibility& outPrevGridVis)
{
    outPrevGridVis = brls::Visibility::GONE;
    brls::Application::blockInputs();

    if (this->gridView)
    {
        outPrevGridVis = this->gridView->getVisibility();
        this->gridView->setVisibility(brls::Visibility::GONE);
        this->gridView->setHideHighlight(true);
        this->gridView->setHideHighlightBackground(true);
        this->gridView->setHideHighlightBorder(true);
        this->gridView->setFocusable(false);
        brls::Application::giveFocus(nullptr);
    }

    return createLoadingDialog(msg);
}

void SessionAppSelect::restoreGridViewAndInputs(brls::Visibility prevGridVis)
{
    brls::Application::unblockInputs();
    if (this->gridView)
    {
        this->gridView->setHideHighlight(false);
        this->gridView->setHideHighlightBackground(false);
        this->gridView->setHideHighlightBorder(false);
        this->gridView->setFocusable(true);
        if (prevGridVis == brls::Visibility::VISIBLE)
            brls::Application::giveFocus(this->gridView);
        this->gridView->setVisibility(prevGridVis);
    }
}

using namespace brls::literals;

SessionAppSelect::SessionAppSelect(const std::string& hostName)
    : brls::Box(brls::Axis::COLUMN)
    , host()
{
    this->isAlive = std::make_shared<std::atomic<bool>>(true);
    vita_log::info("View: SessionAppSelect for host: %s", hostName.c_str());

    // Find the real host by name
    auto found = HostStorage::findHost(hostName);
    if (found)
    {
        this->host = *found;
    }
    else
    {
        vita_log::error("[SessionAppSelect] Host not found: '%s' in HostStorage", hostName.c_str());
    }

    this->inflateFromXMLRes("xml/views/session_app_select.xml");

    app_select_title->setText(hostName);
    app_select_subtitle->setText(brls::getStr("moonlight/session/app_select/subtitle"));

    gridView = new GridView();
    gridView->setColumns(3);
    gridView->setWidth(brls::Box::AUTO);
    gridView->setGrow(1.0f);
    grid_placeholder->addView(gridView);

    spinner = loading_spinner;

    HostInfo hostCopy = this->host;
    auto isAliveCopy  = this->isAlive;
    brls::async([this, hostCopy, isAliveCopy]() mutable
        {
        RemoteAppInfo running;
        bool active = GameStreamClient::instance().probeActiveSession(hostCopy, running);
        if (active) {
            brls::sync([this, running, hostCopy, isAliveCopy]() {
                if (!isAliveCopy->load(std::memory_order_acquire)) return;
                if (this->gridView && this->gridView->getVisibility() != brls::Visibility::VISIBLE) {
                    return;
                }
                vita_log::info("[SessionAppSelect] Active session detected for %s", hostCopy.ip.c_str());
                auto* holder = new brls::Box(brls::Axis::COLUMN);
                auto* label = new brls::Label();
                std::string msg = brls::getStr("moonlight/session/app_select/active_session_msg");
                size_t pos = msg.find("$(app)");
                if (pos != std::string::npos) msg.replace(pos, 6, running.name);
                label->setText(msg);
                label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                label->setMarginBottom(18);

                auto* btnBox = new brls::Box(brls::Axis::ROW);
                btnBox->setJustifyContent(brls::JustifyContent::CENTER);
                btnBox->setAlignItems(brls::AlignItems::CENTER);

                auto* resumeBtn = new brls::Button();
                resumeBtn->setText(brls::getStr("moonlight/session/app_select/button_resume"));
                resumeBtn->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
                resumeBtn->setMargins(0, 8, 0, 0);

                auto* newBtn = new brls::Button();
                newBtn->setText(brls::getStr("moonlight/session/app_select/button_start_new"));
                newBtn->setStyle(&brls::BUTTONSTYLE_PRIMARY);

                btnBox->addView(resumeBtn);
                btnBox->addView(newBtn);
                holder->addView(label);
                holder->addView(btnBox);
                holder->setPadding(18,18,18,18);

                auto* dialog = new brls::Dialog(holder);
                dialog->setCancelable(true);
                dialog->open();

                resumeBtn->registerClickAction([this, running, dialog](brls::View*) -> bool {
                    dialog->dismiss();
                    this->AppSelected(running, true);
                    return true;
                });

                newBtn->registerClickAction([this, running, dialog, hostCopy, isAliveCopy](brls::View*) -> bool {
                    dialog->dismiss();
                    auto* waitDlg = createLoadingDialog(brls::getStr("moonlight/session/app_select/ending_session"));
                    std::thread([hostCopy, this, waitDlg, isAliveCopy]() mutable {
                        vita_log::info("[SessionAppSelect] Sending quitApp for %s before displaying apps", hostCopy.ip.c_str());
                        if (!GameStreamClient::instance().isConnected(hostCopy.ip)) {
                            GameStreamClient::instance().connect(hostCopy);
                        }
                        bool ok = GameStreamClient::instance().quitApp(hostCopy.ip);
                        brls::sync([this, ok, waitDlg, isAliveCopy]() {
                            if (waitDlg)
                                waitDlg->close();
                            if (!isAliveCopy->load(std::memory_order_acquire))
                                return;
                            if (!ok) {
                                brls::Application::notify(brls::getStr("moonlight/session/app_select/error_end_session"));
                            }
                            this->populateAppList();
                        });
                    }).detach();
                    return true;
                });
            });
        } else {
            brls::sync([this, isAliveCopy]() {
                if (!isAliveCopy->load(std::memory_order_acquire)) return;
                this->populateAppList();
            });
        } });
}

SessionAppSelect::~SessionAppSelect()
{
    this->isAlive->store(false, std::memory_order_release);
}

void SessionAppSelect::populateAppList()
{
    vita_log::info("[SessionAppSelect] populateAppList called for host: %s (ip: %s)", host.name.c_str(), host.ip.c_str());

    // Wait for Moonmic to confirm/prepare Sunshine before requesting the app list
    if (!sunshineReady)
    {
        if (sunshineCheckInFlight)
        {
            vita_log::info("[SessionAppSelect] Waiting for Moonmic/Sunshine signal (spinner active)");
            return;
        }

        HostInfo hostCopy = this->host;

        ConfigManager cfg;
        cfg.load();
        VideoSettings vs       = cfg.getVideoSettings();
        StreamConfiguration sc = cfg.getStreamConfig();

        auto isAliveCopy = this->isAlive;
        moonmic::PrepCallbacks callbacks;
        callbacks.onStart = [this, isAliveCopy]()
        {
            if (!isAliveCopy->load(std::memory_order_acquire))
                return;
            sunshineCheckInFlight = true;
            if (spinner)
                spinner->setVisibility(brls::Visibility::VISIBLE);
            if (gridView)
                gridView->setVisibility(brls::Visibility::INVISIBLE);
            if (app_select_empty)
                app_select_empty->setVisibility(brls::Visibility::GONE);
        };

        callbacks.onDone = [this, isAliveCopy](bool ok)
        {
            if (!isAliveCopy->load(std::memory_order_acquire))
                return;
            sunshineCheckInFlight = false;
            if (!ok)
            {
                sunshineReady = true;
                if (spinner)
                    spinner->setVisibility(brls::Visibility::GONE);

                vita_log::info("[SessionAppSelect] Moonmic unavailable; continuing without integration");
                this->populateAppList();
                return;
            }

            sunshineReady = true;
            brls::Application::notify(brls::getStr("moonlight/session/app_select/moonmic_host_active"));

            auto [currentTargetW, currentTargetH] = moonmic::MoonmicBridge::getInstance().getTargetResolution();
            vita_log::info("[SessionAppSelect] Moonmic handshake OK (target %dx%d)", currentTargetW, currentTargetH);

            this->populateAppList();
        };

        moonmic::prepareSunshine(hostCopy, sc, vs, callbacks);
        return;
    }

    // Show spinner and hide content
    if (spinner)
        spinner->setVisibility(brls::Visibility::VISIBLE);
    if (gridView)
        gridView->setVisibility(brls::Visibility::INVISIBLE);
    if (app_select_empty)
        app_select_empty->setVisibility(brls::Visibility::GONE);

    if (host.name.empty() || host.ip.empty())
    {
        if (spinner)
            spinner->setVisibility(brls::Visibility::GONE);
        if (app_select_empty)
        {
            app_select_empty->setText(brls::getStr("moonlight/session/app_select/error_no_host"));
            app_select_empty->setVisibility(brls::Visibility::VISIBLE);
        }
        return;
    }

    auto isAliveCopy  = this->isAlive;
    HostInfo hostCopy = this->host;
    brls::async([this, hostCopy, isAliveCopy]()
        {
    vita_log::info("[SessionAppSelect] Calling GameStreamClient::getAppList for host: %s (ip: %s)", hostCopy.name.c_str(), hostCopy.ip.c_str());
    std::vector<RemoteAppInfo> apps;
    GameStreamClient::instance().getAppList(hostCopy.ip, [&apps](const std::vector<RemoteAppInfo>& a){ apps = a; });
    vita_log::info("[SessionAppSelect] getAppList returned %u apps", (unsigned)apps.size());
        for (const auto& app : apps) {
            vita_log::info("[SessionAppSelect] App recibida: id='%s', name='%s', iconUrl='%s'", app.id.c_str(), app.name.c_str(), app.iconUrl.c_str());
        }
        brls::sync([this, apps, isAliveCopy]() {
            if (!isAliveCopy->load(std::memory_order_acquire)) return;
            if (spinner) spinner->setVisibility(brls::Visibility::GONE);
            if (apps.empty()) {
                vita_log::info("[SessionAppSelect] No applications found on this host.");
                if (app_select_empty) {
                    app_select_empty->setText(brls::getStr("moonlight/session/app_select/no_apps"));
                    app_select_empty->setVisibility(brls::Visibility::VISIBLE);
                }
                return;
            }

            std::vector<std::string> appNames;
            std::vector<std::string> appIcons;
            std::vector<int> appsToDownload;

            std::string cacheDir = "ux0:data/moonlight/cache";
#ifndef __PSV__
            cacheDir = "cache";
#endif
            // Ensure the cache directory exists
            mkdir(cacheDir.c_str(), 0777);

            for (size_t i = 0; i < apps.size(); ++i) {
                const auto& app = apps[i];
                appNames.push_back(app.name);

                std::string cachePath = cacheDir + "/boxart_" + app.id + ".png";
                struct stat st;
                if (stat(cachePath.c_str(), &st) == 0 && st.st_size > 0) {
                    appIcons.push_back(cachePath);
                } else {
                    appIcons.push_back("img/moonlight/pc.png");
                    appsToDownload.push_back(i);
                }
            }

            if (gridView) {
                gridView->setItems(appNames, appIcons);
                gridView->setOnItemSelect([this, apps](int index) {
                    if (index >= 0 && index < (int)apps.size()) {
                        this->AppSelected(apps[index]);
                    }
                });
                gridView->setVisibility(brls::Visibility::VISIBLE);

                // Start downloading missing icons asynchronously in a single background thread
                if (!appsToDownload.empty()) {
                    std::string hostIp = this->host.ip;
                    auto isAliveCopy = this->isAlive;
                    auto gridViewCopy = this->gridView;

                    std::thread([isAliveCopy, gridViewCopy, apps, appsToDownload, hostIp, cacheDir]() {
                        for (int idx : appsToDownload) {
                            if (!isAliveCopy->load(std::memory_order_acquire)) break;

                            const auto& app = apps[idx];
                            int appId = 0;
                            try {
                                appId = std::stoi(app.id);
                            } catch (...) {
                                continue;
                            }

                            vita_log::info("[SessionAppSelect] Downloading box art for app: %s (ID: %d)", app.name.c_str(), appId);
                            Data boxartData;
                            bool ok = GameStreamClient::instance().getAppBoxart(hostIp, appId, boxartData);
                            if (ok && boxartData.size() > 0) {
                                std::string cachePath = cacheDir + "/boxart_" + app.id + ".png";
                                boxartData.write_to_file(cachePath);
                                vita_log::info("[SessionAppSelect] Box art saved to: %s", cachePath.c_str());

                                brls::sync([isAliveCopy, gridViewCopy, idx, cachePath]() {
                                    if (!isAliveCopy->load(std::memory_order_acquire)) return;
                                    if (gridViewCopy) {
                                        gridViewCopy->setItemIcon(idx, cachePath);
                                    }
                                });
                            } else {
                                vita_log::warning("[SessionAppSelect] Failed to download box art for app: %s (ID: %d)", app.name.c_str(), appId);
                            }

                            // Prevent flooding the network/server
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }).detach();
                }

                // Give focus to the first element after a short delay
                brls::async([this, isAliveCopy]() {
                    brls::sync([this, isAliveCopy]() {
                        if (!isAliveCopy->load(std::memory_order_acquire)) return;
                        // Try to give focus to the GridView first
                        if (gridView) {
                            brls::Application::giveFocus(gridView);
                            vita_log::info("[SessionAppSelect] Foco dado al GridView");
                        }
                    });
                });
            }
        }); });
}

void SessionAppSelect::AppSelected(const RemoteAppInfo& app, bool forceStart)
{
    vita_log::info("App seleccionada: %s (ID: %s)", app.name.c_str(), app.id.c_str());

    ConfigManager configManager;
    if (!configManager.load())
    {
        vita_log::warning("[SessionAppSelect] Could not load settings; using defaults");
    }

    StreamConfiguration streamSettings = configManager.getStreamConfig();
    VideoSettings videoSettings        = configManager.getVideoSettings();

    vita_log::info("[SessionAppSelect] Stream %dx%d @ %dfps, bitrate=%d, render_mode=%d",
        streamSettings.streamWidth, streamSettings.streamHeight,
        streamSettings.fps, streamSettings.bitrate, videoSettings.render_mode);

    const bool automaticBitrate       = streamSettings.bitrate == -1;
    STREAM_CONFIGURATION streamConfig = stream::buildConfiguration(streamSettings);

    vita_log::info("[SessionAppSelect] Stream configuration:");
    vita_log::info("[SessionAppSelect] - Resolution: %dx%d", streamConfig.width, streamConfig.height);
    vita_log::info("[SessionAppSelect] - FPS: %d", streamConfig.fps);
    vita_log::info("[SessionAppSelect] - Bitrate: %d Kbps%s", streamConfig.bitrate,
        automaticBitrate ? " (automatic based on resolution/FPS)" : "");

    vita_log::info("[SessionAppSelect] - packetSize: %d streamingRemotely=%d formats=0x%X",
        streamConfig.packetSize, streamConfig.streamingRemotely, streamConfig.supportedVideoFormats);

    brls::Visibility prevGridVis = brls::Visibility::GONE;
    std::string connectingMsg    = brls::getStr("moonlight/session/app_select/connecting");
    auto* loadingDialog          = this->showConnectingDialog(connectingMsg, prevGridVis);

    HostInfo hostCopy            = this->host;
    RemoteAppInfo appCopy        = app;
    STREAM_CONFIGURATION cfgCopy = streamConfig;
    auto isAliveCopy             = this->isAlive;
    std::thread([hostCopy, appCopy, cfgCopy, loadingDialog, this, prevGridVis, forceStart, isAliveCopy]() mutable
        {
        auto& client = GameStreamClient::instance();
        int result   = 0;
        int appId    = 0;
        std::shared_ptr<VitaSession> vitaSession;

        try
        {
            appId = std::stoi(appCopy.id);
            if (!client.connect(hostCopy))
            {
                result = 1;
            }
            else
            {
                GameStreamClient::StartMode mode = forceStart
                    ? GameStreamClient::StartMode::RESUME_ONLY
                    : GameStreamClient::StartMode::AUTO;
                if (!client.startApp(hostCopy.ip, cfgCopy, appId, mode))
                {
                    result = 2;
                }
                else
                {
                    vitaSession = std::make_shared<VitaSession>(hostCopy.ip, appId);
                    if (!vitaSession->start())
                        result = 3;
                }
            }
        }
        catch (...)
        {
            result = 2;
        }

        brls::sync([result, appId, hostCopy, appCopy, loadingDialog, this, prevGridVis, isAliveCopy, vitaSession = std::move(vitaSession)]() mutable {
            if (loadingDialog)
                loadingDialog->close();
            if (!isAliveCopy->load(std::memory_order_acquire))
            {
                brls::Application::unblockInputs();
                if (vitaSession)
                    vitaSession->stop(false);
                return;
            }

            this->restoreGridViewAndInputs(prevGridVis);
            if (result != 0)
            {
                const char* errorKey = result == 1
                    ? "moonlight/session/app_select/error_connect"
                    : (result == 3 ? "moonlight/session/app_select/error_start_stream"
                                   : "moonlight/session/app_select/error_start_app");
                brls::Application::notify(brls::getStr(errorKey));
                return;
            }

            GameStreamClient::instance().setActiveStream(hostCopy.ip, appId, appCopy.name);
            auto* sessionView = new SessionMainView(hostCopy, appCopy, std::move(vitaSession));
            brls::Application::pushActivity(new brls::Activity(sessionView), brls::TransitionAnimation::NONE);
        }); })
        .detach();
}
