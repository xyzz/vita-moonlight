/*
    session_app_select.cpp - Selección de aplicación para iniciar streaming en Moonlight PSVita/Windows
    Autor: aorsini + comunidad
*/
#include "session/session_app_select.hpp"

#include <sys/stat.h>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
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
#include "session/vita_session.hpp"
#include "utils/dialog_utils.h"
#include "video/legacy/modules/vita_globals.hpp" // For VITA_STREAM constants
#include "view/pccard.hpp"

namespace
{
// Hosts for which the active session dialog will be temporarily suppressed
static std::set<std::string> g_suppressedActiveHosts;
// Hosts for which we already show the 'resume' dialog during login
static std::set<std::string> g_resumeShownHosts;

// NOTE: Implementations moved to class methods so they can access
// private members (gridView) directly.
}

brls::Dialog* SessionAppSelect::showConnectingDialog(const std::string& msg, brls::Visibility& outPrevGridVis)
{
    outPrevGridVis = brls::Visibility::GONE;
    brls::Application::blockInputs();

    if (this->gridView)
    {
        outPrevGridVis = this->gridView->getVisibility();
        // Completely hide the GridView and remove focus/highlight
        this->gridView->setVisibility(brls::Visibility::GONE);
        this->gridView->setHideHighlight(true);
        this->gridView->setHideHighlightBackground(true);
        this->gridView->setHideHighlightBorder(true);
        this->gridView->setFocusable(false);
        brls::Application::giveFocus(nullptr);

        // Hide highlights from all descendants
        std::function<void(brls::View*)> walkHideLocal;
        walkHideLocal = [&walkHideLocal](brls::View* v)
        {
            if (!v)
                return;
            v->setHideHighlight(true);
            v->setHideHighlightBackground(true);
            v->setHideHighlightBorder(true);
            v->setFocusable(false);
            brls::Box* box = dynamic_cast<brls::Box*>(v);
            if (!box)
                return;
            auto ch = box->getChildren();
            for (auto* cc : ch)
                walkHideLocal(cc);
        };
        auto rows = this->gridView->getChildren();
        for (auto* row : rows)
            walkHideLocal(row);
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

        std::function<void(brls::View*)> walkRestoreLocal;
        walkRestoreLocal = [&walkRestoreLocal](brls::View* v)
        {
            if (!v)
                return;
            v->setHideHighlight(false);
            v->setHideHighlightBackground(false);
            v->setHideHighlightBorder(false);
            v->setFocusable(true);
            brls::Box* box = dynamic_cast<brls::Box*>(v);
            if (!box)
                return;
            auto ch = box->getChildren();
            for (auto* cc : ch)
                walkRestoreLocal(cc);
        };
        auto rows2 = this->gridView->getChildren();
        for (auto* row : rows2)
            walkRestoreLocal(row);
    }
}

using namespace brls::literals;

SessionAppSelect::SessionAppSelect(const std::string& hostName)
    : brls::Box(brls::Axis::COLUMN)
    , host()
{
    this->isAlive = std::make_shared<bool>(true);
    vita_log::info("View: SessionAppSelect para host: %s", hostName.c_str());

    // Find the real host by name
    auto found = HostStorage::findHost(hostName);
    if (found)
    {
        this->host = *found;
    }
    else
    {
        vita_log::error("[SessionAppSelect] No se encontró el host '%s' en HostStorage", hostName.c_str());
        // Leave host empty, will show error in populateAppList
    }

    // We no longer report Moonmic status in ctor to avoid false negatives.

    this->inflateFromXMLRes("xml/views/session_app_select.xml");

    // Set titles
    app_select_title->setText(hostName);
    app_select_subtitle->setText(brls::getStr("moonlight/session/app_select/subtitle"));

    // Create and configure the GridView dynamically
    gridView = new GridView();
    gridView->setColumns(3); // 3 columns for PS Vita (best fit)
    gridView->setWidth(brls::Box::AUTO);
    gridView->setGrow(1.0f);
    grid_placeholder->addView(gridView);

    // Set the spinner with appropriate size
    BRLS_BIND(brls::ProgressSpinner, loading_spinner, "loading_spinner");
    spinner = loading_spinner;
    if (!spinner)
    {
        spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::NORMAL);
    }

    // Check if an active session exists — delegating logic to GameStreamClient
    HostInfo hostCopy = this->host;
    auto isAliveCopy  = this->isAlive;
    brls::async([this, hostCopy, isAliveCopy]() mutable
        {
        RemoteAppInfo running;
        bool active = GameStreamClient::instance().probeActiveSession(hostCopy, running);
        if (active) {
            brls::sync([this, running, hostCopy, isAliveCopy]() {
                if (!*isAliveCopy) return;
                // If the view has already decided to suppress the dialog (for example the
                // user pressed Resume before this check finished),
                // we avoid showing it again.
                if (this->suppressActiveDialog) {
                    vita_log::info("[SessionAppSelect] suppressActiveDialog set -> omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // Check if globally the dialog was suppressed for this host
                if (g_suppressedActiveHosts.count(hostCopy.ip)) {
                    vita_log::info("[SessionAppSelect] g_suppressedActiveHosts contains host -> omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // If the GridView is not visible it is because we are already in a flow
                // connection/startup (AppSelected), so we should NOT show
                // the currently active session dialog.
                if (this->gridView && this->gridView->getVisibility() != brls::Visibility::VISIBLE) {
                    vita_log::info("[SessionAppSelect] gridView no visible -> omitiendo diálogo de sesión activa");
                    return;
                }

                // If we already showed the dialog in this instance, do not do it again
                if (this->activeDialogShown) {
                    vita_log::info("[SessionAppSelect] activeDialogShown set — omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // If we already show the dialog for this host during the (global) visit, skip
                if (g_resumeShownHosts.count(hostCopy.ip)) {
                    vita_log::info("[SessionAppSelect] g_resumeShownHosts contains host — omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                vita_log::info("[SessionAppSelect] Sesión activa detectada para %s -> mostrar diálogo Reanudar/Empezar nuevo (suppressActiveDialog=%d, g_suppressedActiveHosts=%d, activeDialogShown=%d, g_resumeShownHosts=%d)",
                                    hostCopy.ip.c_str(),
                                    this->suppressActiveDialog ? 1 : 0,
                                    g_suppressedActiveHosts.count(hostCopy.ip) ? 1 : 0,
                                    this->activeDialogShown ? "1" : "0",
                                    g_resumeShownHosts.count(hostCopy.ip) ? "1" : "0");
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

                // Do not show 'Re-pair' button in this dialog to reduce UX friction.
                btnBox->addView(resumeBtn);
                btnBox->addView(newBtn);
                holder->addView(label);
                holder->addView(btnBox);
                holder->setPadding(18,18,18,18);

                auto* dialog = new brls::Dialog(holder);
                dialog->setCancelable(true);
                dialog->open();

                // Mark that we already show the dialog in this instance and at the host level
                this->activeDialogShown = true;
                g_resumeShownHosts.insert(hostCopy.ip);

                resumeBtn->registerClickAction([this, running, dialog, hostCopy](brls::View*) -> bool {
                    dialog->dismiss();
                        // Check to not show the active dialog again in this view
                        this->suppressActiveDialog = true;
                        // Check at host level to avoid respawning if view is recreated
                        g_resumeShownHosts.insert(hostCopy.ip);
                        // Also globally suppress the dialog for this host while
                        // process the resume — this avoids race conditions where
                        // probeActiveSession re-fire the UI.
                        g_suppressedActiveHosts.insert(hostCopy.ip);
                        vita_log::info("[SessionAppSelect] resumeBtn: inserted host into g_suppressedActiveHosts for %s", hostCopy.ip.c_str());
                        // Log current suppression set size for diagnostics
                        vita_log::info("[SessionAppSelect] g_suppressedActiveHosts size=%d (contains host=%d)", (int)g_suppressedActiveHosts.size(), g_suppressedActiveHosts.count(hostCopy.ip)?1:0);
                    // Resume active session by forcing start even if PairStatus is 0
                    this->AppSelected(running, true);
                    return true;
                });

                // repairBtn removed: pairing via this dialog no longer exposed here.

                newBtn->registerClickAction([this, running, dialog, hostCopy, isAliveCopy](brls::View*) -> bool {
                    dialog->dismiss();
                    // Prevent the active session dialog from reappearing
                    this->suppressActiveDialog = true;
                    auto* waitDlg = createLoadingDialog(brls::getStr("moonlight/session/app_select/ending_session"));
                    std::thread([hostCopy, this, waitDlg, isAliveCopy]() mutable {
                        vita_log::info("[SessionAppSelect] Enviando quitApp para %s antes de mostrar apps", hostCopy.ip.c_str());
                        if (!GameStreamClient::instance().isConnected(hostCopy.ip)) {
                            GameStreamClient::instance().connect(hostCopy);
                        }
                        bool ok = GameStreamClient::instance().quitApp(hostCopy.ip);
                        brls::sync([this, ok, waitDlg, isAliveCopy]() {
                            if (!*isAliveCopy) return;
                            if (waitDlg) waitDlg->close();
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
            // If probeActiveSession returned false, we do not show mismatch dialog here.
            brls::sync([this, isAliveCopy]() {
                if (!*isAliveCopy) return;
                this->populateAppList();
            });
        } });
}

SessionAppSelect::~SessionAppSelect()
{
    *this->isAlive = false;
    // The view destructor is responsible for freeing the children (gridView, spinner, etc.)
    // Clear the global flag so that the next entry to the host can show the dialog again.
    try
    {
        if (!this->host.ip.empty() && g_resumeShownHosts.count(this->host.ip))
            g_resumeShownHosts.erase(this->host.ip);
        if (!this->host.ip.empty() && g_suppressedActiveHosts.count(this->host.ip))
            g_suppressedActiveHosts.erase(this->host.ip);
    }
    catch (...)
    {
        // Don't do anything if it were a spear (I shouldn't)
    }
}

void SessionAppSelect::onLayout()
{
    Box::onLayout();
}

void SessionAppSelect::populateAppList()
{
    vita_log::info("[SessionAppSelect] populateAppList llamado para host: %s (ip: %s)", host.name.c_str(), host.ip.c_str());

    // Wait for Moonmic to confirm/prepare Sunshine before requesting the app list
    if (!sunshineReady)
    {
        if (sunshineCheckInFlight)
        {
            vita_log::info("[SessionAppSelect] Esperando señal de Moonmic/Sunshine (spinner activo)");
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
            if (!*isAliveCopy)
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
            if (!*isAliveCopy)
                return;
            sunshineCheckInFlight = false;
            if (!ok)
            {
                sunshineReady = true; // Mark check as fully handled (don't retry endlessly)
                if (!moonmicNotified)
                {
                    brls::Application::notify(brls::getStr("moonlight/session/app_select/moonmic_not_connected"));
                    moonmicLastStatus = false;
                    moonmicNotified   = true;
                }
                if (spinner)
                    spinner->setVisibility(brls::Visibility::GONE);

                // Fallback: Proceed even if Moonmic is not active
                vita_log::warning("[SessionAppSelect] Moonmic handshake failed, proceeding with normal connection...");
                this->populateAppList();
                return;
            }

            sunshineReady = true;
            if (!moonmicNotified)
            {
                brls::Application::notify(brls::getStr("moonlight/session/app_select/moonmic_host_active"));
                moonmicLastStatus = true;
                moonmicNotified   = true;
            }

            auto [currentTargetW, currentTargetH] = moonmic::MoonmicBridge::getInstance().getTargetResolution();
            vita_log::info("[SessionAppSelect] Moonmic handshake OK (target %dx%d)", currentTargetW, currentTargetH);

            this->populateAppList();
        };

        callbacks.onCancel = [this, isAliveCopy]()
        {
            if (!*isAliveCopy)
                return;
            sunshineCheckInFlight = false;
            brls::Application::notify(brls::getStr("moonlight/session/app_select/error_start_app"));
            brls::Application::popActivity();
        };

        moonmic::ensureSunshineReadyWithPrompt(hostCopy, sc, vs, resolutionPromptShown, callbacks);
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

    auto isAliveCopy = this->isAlive;
    brls::async([this, isAliveCopy]()
        {
    vita_log::info("[SessionAppSelect] Llamando a GameStreamClient::getAppList para host: %s (ip: %s)", host.name.c_str(), host.ip.c_str());
    std::vector<RemoteAppInfo> apps;
    GameStreamClient::instance().getAppList(this->host.ip, [&apps](const std::vector<RemoteAppInfo>& a){ apps = a; });
    vita_log::info("[SessionAppSelect] getAppList devolvió %u apps", (unsigned)apps.size());
        for (const auto& app : apps) {
            vita_log::info("[SessionAppSelect] App recibida: id='%s', name='%s', iconUrl='%s'", app.id.c_str(), app.name.c_str(), app.iconUrl.c_str());
        }
        brls::sync([this, apps, isAliveCopy]() {
            if (!*isAliveCopy) return;
            if (spinner) spinner->setVisibility(brls::Visibility::GONE);
            if (apps.empty()) {
                vita_log::info("[SessionAppSelect] No se encontraron aplicaciones en este host.");
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
                            if (!*isAliveCopy) break;

                            const auto& app = apps[idx];
                            int appId = 0;
                            try {
                                appId = std::stoi(app.id);
                            } catch (...) {
                                continue;
                            }

                            vita_log::info("[SessionAppSelect] Descargando boxart para App: %s (ID: %d)", app.name.c_str(), appId);
                            Data boxartData;
                            bool ok = GameStreamClient::instance().getAppBoxart(hostIp, appId, boxartData);
                            if (ok && boxartData.size() > 0) {
                                std::string cachePath = cacheDir + "/boxart_" + app.id + ".png";
                                boxartData.write_to_file(cachePath);
                                vita_log::info("[SessionAppSelect] Boxart guardado en: %s", cachePath.c_str());

                                brls::sync([isAliveCopy, gridViewCopy, idx, cachePath]() {
                                    if (!*isAliveCopy) return;
                                    if (gridViewCopy) {
                                        gridViewCopy->setItemIcon(idx, cachePath);
                                    }
                                });
                            } else {
                                vita_log::warning("[SessionAppSelect] Fallo al descargar boxart para App: %s (ID: %d)", app.name.c_str(), appId);
                            }

                            // Prevent flooding the network/server
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }).detach();
                }

                // Give focus to the first element after a short delay
                brls::async([this, isAliveCopy]() {
                    brls::sync([this, isAliveCopy]() {
                        if (!*isAliveCopy) return;
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

    // Encapsulated startup flow so you can confirm it before
    auto startFlow = [this, app, forceStart]()
    {
        // Prepare streaming setup for PS Vita
        STREAM_CONFIGURATION streamConfig;
        memset(&streamConfig, 0, sizeof(streamConfig));

        // Load user configuration
        ConfigManager configManager;
        if (!configManager.load())
        {
            vita_log::warning("[SessionAppSelect] No se pudo cargar configuración, usando valores por defecto");
        }

        StreamConfiguration streamSettings = configManager.getStreamConfig();
        VideoSettings videoSettings        = configManager.getVideoSettings();

        // Send Moonmic handshake to configure Sunshine remapping only after confirmation
        {
            std::string micHost = videoSettings.microphone_host_ip.empty() ? this->host.ip : videoSettings.microphone_host_ip;
            int micPort         = videoSettings.microphone_port > 0 ? videoSettings.microphone_port : MOONMIC_DEFAULT_PORT;
            auto& bridge        = moonmic::MoonmicBridge::getInstance();
            bridge.loadConfig();
            if (streamSettings.width > 0 && streamSettings.height > 0)
            {
                bridge.setTargetResolution(static_cast<uint16_t>(streamSettings.width), static_cast<uint16_t>(streamSettings.height));
            }
            auto hsResult = bridge.sendResolutionHandshake(micHost, micPort);
            vita_log::info("[SessionAppSelect] Moonmic handshake %s (%s:%d)", hsResult.success ? "OK" : "FAIL", micHost.c_str(), micPort);
        }

        // Debug: show read configuration values
        vita_log::info("[SessionAppSelect] Configuración leída:");
        vita_log::info("[SessionAppSelect] - Stream: %dx%d @ %dfps, bitrate=%d",
            streamSettings.width, streamSettings.height, streamSettings.fps, streamSettings.bitrate);
        vita_log::info("[SessionAppSelect] - Video: render_mode=%d", videoSettings.render_mode);

        // FORCE 960x544 stream for PS Vita (optimal quality)
        // Settings resolution values are saved for HOST monitor control only
        // Sunshine will downscale from displayWidth x displayHeight to 960x544 with high quality
        streamConfig.width  = VITA_STREAM_WIDTH;
        streamConfig.height = VITA_STREAM_HEIGHT;
        streamConfig.fps    = streamSettings.fps > 0 ? streamSettings.fps : VITA_STREAM_DEFAULT_FPS;

        // Build RTSP launch URL without displayWidth/displayHeight
        // Display resolution is now controlled via moonmic protocol handshake
        // which configures Sunshine's mode_remapping before streaming starts:

        // Calculate bitrate: if automatic (-1), use formula based on resolution and fps
        if (streamSettings.bitrate == -1)
        {
            // Approximate formula: (range * high * fps * bits_per_pixel) / 1000000 for Mbps
            // Using VITA_STREAM_BITS_PER_PIXEL bits per pixel as a conservative approximation for H.264
            // Calculate step by step to avoid loss of precision
            long long total_pixels          = (long long)streamConfig.width * streamConfig.height * streamConfig.fps;
            long long total_bits_per_second = (total_pixels * (long long)(VITA_STREAM_BITS_PER_PIXEL * 10)) / 10;
            int calculatedBitrate           = (int)(total_bits_per_second / 1000); // Convert to Kbps directly
            // Limit to a reasonable range using global constants
            streamConfig.bitrate = std::max(VITA_STREAM_MIN_BITRATE, std::min(VITA_STREAM_MAX_BITRATE, calculatedBitrate));
        }
        else
        {
            streamConfig.bitrate = streamSettings.bitrate > 0 ? streamSettings.bitrate : VITA_STREAM_DEFAULT_BITRATE;
        }

        streamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        streamConfig.encryptionFlags    = ENCFLG_NONE; // No encryption for now

        vita_log::info("[SessionAppSelect] Configuración de streaming:");
        vita_log::info("[SessionAppSelect] - Resolución: %dx%d", streamConfig.width, streamConfig.height);
        vita_log::info("[SessionAppSelect] - FPS: %d", streamConfig.fps);
        vita_log::info("[SessionAppSelect] - Bitrate: %d Kbps", streamConfig.bitrate);

        // Ensure correct packetSize and flags if they have not been set
        if (streamConfig.packetSize <= 0)
        {
            streamConfig.packetSize = 1024; // safe default value
            vita_log::info("[SessionAppSelect] packetSize no definido -> usando 1024");
        }
        if (streamConfig.streamingRemotely == 0 && streamConfig.streamingRemotely != STREAM_CFG_LOCAL && streamConfig.streamingRemotely != STREAM_CFG_REMOTE && streamConfig.streamingRemotely != STREAM_CFG_AUTO)
        {
            streamConfig.streamingRemotely = STREAM_CFG_AUTO; // let the core decide
            vita_log::info("[SessionAppSelect] streamingRemotely no definido -> AUTO");
        }
        if (streamConfig.supportedVideoFormats == 0)
        {
#ifdef VIDEO_FORMAT_H264
            streamConfig.supportedVideoFormats = VIDEO_FORMAT_H264;
            vita_log::info("[SessionAppSelect] supportedVideoFormats vacío -> set H264");
#endif
        }
        vita_log::info("[SessionAppSelect] - packetSize: %d streamingRemotely=%d formats=0x%X",
            streamConfig.packetSize, streamConfig.streamingRemotely, streamConfig.supportedVideoFormats);

        // Initialize server using GameStreamClient (Moonlight-Switch pattern)

        // Show modal dialog with spinner indicating connection progress
        brls::Visibility prevGridVis = brls::Visibility::GONE;
        std::string connectingMsg    = brls::getStr("moonlight/session/app_select/connecting");
        auto* loadingDialog          = this->showConnectingDialog(connectingMsg, prevGridVis);

        // Run the connection and startup sequence in a background thread
        // We capture by value the necessary data to avoid using `this` in the background
        HostInfo hostCopy            = this->host;
        RemoteAppInfo appCopy        = app;
        STREAM_CONFIGURATION cfgCopy = streamConfig;
        auto isAliveCopy             = this->isAlive;
        std::thread([hostCopy, appCopy, cfgCopy, loadingDialog, this, prevGridVis, forceStart, isAliveCopy]() mutable
            {
        vita_log::info("[SessionAppSelect][async] Iniciando conexión en hilo de fondo para %s", hostCopy.ip.c_str());
        bool connected = GameStreamClient::instance().connect(hostCopy);
        // Update UI in main thread
        brls::sync([connected, hostCopy, appCopy, cfgCopy, loadingDialog, prevGridVis, forceStart, this, isAliveCopy]() mutable {
            if (!*isAliveCopy) {
                if (loadingDialog) { loadingDialog->close(); loadingDialog = nullptr; }
                return;
            }
            // Note: we do not immediately close the 'connecting' dialog if
            // we are connected — we will reuse it by changing its text to
            // 'starting'. We only close if the connection failed.
            if (!connected) {
                // Close 'connecting' dialog and restore UI in case of failure
                if (loadingDialog) { loadingDialog->close(); loadingDialog = nullptr; }
                // Restore inputs and state before notifying (helper)
                this->restoreGridViewAndInputs(prevGridVis);
                vita_log::error("[SessionAppSelect] Error al conectar con el servidor");
                brls::Application::notify(brls::getStr("moonlight/session/app_select/error_connect"));
                return;
            }

            // If the host is not paired, we do not abort the startup here. Before
            // we closed the dialogue and returned to the list; that prevented launching
            // new apps. Now we register the status and continue to
            // try startApp (the server will decide whether to allow the launch).
            if (!forceStart && !GameStreamClient::instance().isPaired(hostCopy.ip)) {
                vita_log::info("[SessionAppSelect] Host %s no emparejado, procediendo a intentar inicio (forceStart=%d)", hostCopy.ip.c_str(), forceStart ? 1 : 0);
            }

            // Keep the 'connecting' dialog as is and proceed to start
            // the application. The explicit change to 'starting' was redundant
            // (we already showed "connecting" and then it booted correctly) and
            // causes additional code and possibility of race conditions
            // when trying to update text in a dialog that may have been
            // dismiss() and deleted. We preserve the existing dialogue and
            // We continue with startApp.

            // Run startApp and VitaSession in a thread to not block UI
            std::thread([hostCopy, appCopy, cfgCopy, loadingDialog, this, prevGridVis, forceStart, isAliveCopy]() mutable {
                bool started = false;
                if (forceStart) {
                    // If forced (Resume), try explicit resume
                    started = GameStreamClient::instance().startApp(hostCopy.ip, cfgCopy, std::stoi(appCopy.id), GameStreamClient::StartMode::RESUME_ONLY);
                } else {
                    // Normal: allow auto behavior (resume or launch depending on server)
                    started = GameStreamClient::instance().startApp(hostCopy.ip, cfgCopy, std::stoi(appCopy.id), GameStreamClient::StartMode::AUTO);
                }
                brls::sync([started, hostCopy, appCopy, loadingDialog, this, prevGridVis, isAliveCopy]() {
                    if (!*isAliveCopy) {
                        if (loadingDialog) { loadingDialog->close(); }
                        return;
                    }
                    if (loadingDialog) loadingDialog->close();
                    // Restore inputs and GridView uniformly using helper
                    this->restoreGridViewAndInputs(prevGridVis);
                    if (!started) {
                        vita_log::error("[SessionAppSelect] Error al iniciar aplicación");
                        brls::Application::notify(brls::getStr("moonlight/session/app_select/error_start_app"));
                        return;
                    }

                    // Create and start VitaSession
                    SERVER_DATA& serverData = GameStreamClient::instance().serverData(hostCopy.ip);
                    int appId = std::stoi(appCopy.id);
                    bool isSunshine = false;
                    if (serverData.serverInfo.serverInfoAppVersion && std::string(serverData.serverInfo.serverInfoAppVersion).find("Sunshine") != std::string::npos)
                        isSunshine = true;
                    else if (serverData.serverInfo.serverCodecModeSupport != 0)
                        isSunshine = true;

                    auto* vitaSession = new VitaSession(hostCopy.ip, appId, isSunshine);
                    if (!vitaSession->start()) {
                        vita_log::error("[SessionAppSelect] VitaSession start() falló");
                        brls::Application::notify(brls::getStr("moonlight/session/app_select/error_start_stream"));
                        delete vitaSession;
                        return;
                    }

                    GameStreamClient::instance().setActiveStream(hostCopy.ip, appId, appCopy.name);
                    auto* sessionView = new SessionMainView(hostCopy, appCopy);
                    brls::Application::pushActivity(new brls::Activity(sessionView), brls::TransitionAnimation::NONE);
                });
            }).detach();
        }); })
            .detach();
    };

    startFlow();
}
