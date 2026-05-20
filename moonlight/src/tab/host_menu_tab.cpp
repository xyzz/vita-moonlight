#include "tab/host_menu_tab.hpp"

#include <borealis/core/application.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <filesystem>
#include <moonbeam.hpp>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "session/session_app_select.hpp"
#include "tab/edit_host_tab.hpp"
#include "tab/hosts_tab.hpp"
#include "utils/connection_test.hpp"
#include "utils/dialog_utils.h"
#include "utils/wol.hpp"

#ifdef __PSV__
#include <psp2/kernel/clib.h>
#define VITALOG sceClibPrintf
#else
#define VITALOG(...) ((void)0)
#endif

using namespace brls::literals;

HostMenuTab::HostMenuTab(const HostInfo& hostInfo)
    : host(hostInfo)
{
    // Column layout containing both columns or lists
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::SPACE_EVENLY);
    this->setPadding(30.0f);

    brls::Style style = brls::Application::getStyle();

    // ------------------ LEFT COLUMN: INFO CARD ------------------
    brls::Box* leftCol = new brls::Box(brls::Axis::COLUMN);
    leftCol->setAlignItems(brls::AlignItems::FLEX_START);
    leftCol->setJustifyContent(brls::JustifyContent::FLEX_START);
    leftCol->setWidth(380.0f);
    leftCol->setPadding(25.0f);
    leftCol->setMarginRight(20.0f);

    brls::Label* infoTitle = createLabel(brls::getStr("host_dialog/info/title"),
        style["brls/applet_frame/header_title_font_size"] * 0.9f,
        brls::HorizontalAlign::LEFT, 20.0f);
    leftCol->addView(infoTitle);

    std::vector<std::pair<std::string, std::string>> infoRows = {
        { brls::getStr("host_dialog/info/name"), host.name },
        { brls::getStr("host_dialog/info/ip"), host.ip },
        { brls::getStr("host_dialog/info/mac"), host.mac.empty() ? "-" : host.mac }
    };
    brls::Box* infoBox = createInfoBox(infoRows, style["brls/label/default_font_size"], 12.0f);
    leftCol->addView(infoBox);

    this->addView(leftCol);

    // ------------------ RIGHT COLUMN: ACTIONS ------------------
    brls::Box* rightCol = new brls::Box(brls::Axis::COLUMN);
    rightCol->setAlignItems(brls::AlignItems::CENTER);
    rightCol->setJustifyContent(brls::JustifyContent::CENTER);
    rightCol->setWidth(420.0f);

    // 1. Connect Button
    brls::Button* connectBtn = new brls::Button();
    connectBtn->setText(brls::getStr("host_dialog/dialog/connect"));
    connectBtn->setWidth(400.0f);
    connectBtn->setMarginBottom(12.0f);
    connectBtn->registerClickAction([this](brls::View*)
        {
        std::string hostName = this->host.name;
        brls::sync([hostName]() {
            // Pop the host menu activity
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            brls::sync([hostName]() {
                // Find main activity at the top of the stack and push SessionAppSelect onto its AppletFrame
                auto activities = brls::Application::getActivitiesStack();
                if (!activities.empty()) {
                    brls::Activity* top = activities.back();
                    if (top) {
                        brls::View* content = top->getContentView();
                        brls::AppletFrame* applet = dynamic_cast<brls::AppletFrame*>(content);
                        if (applet) {
                            applet->pushContentView(new SessionAppSelect(hostName));
                        }
                    }
                }
            });
        });
        return true; });
    rightCol->addView(connectBtn);

    // 2. Test Connection Button
    brls::Button* testBtn = new brls::Button();
    testBtn->setText(brls::getStr("host_dialog/dialog/test_connection"));
    testBtn->setWidth(400.0f);
    testBtn->setMarginBottom(12.0f);
    testBtn->registerClickAction([this](brls::View*)
        {
        brls::sync([this]() {
            utils::startConnectionTest(this->host);
        });
        return true; });
    rightCol->addView(testBtn);

    // 3. Wake-on-LAN Button (only if mac exists)
    if (!host.mac.empty())
    {
        brls::Button* wolBtn = new brls::Button();
        wolBtn->setText(brls::getStr("host_dialog/wol/button"));
        wolBtn->setWidth(400.0f);
        wolBtn->setMarginBottom(12.0f);
        wolBtn->registerClickAction([this](brls::View*)
            {
            brls::sync([this]() {
                bool ok = utils::sendWOLPacket(this->host.mac, this->host.ip);
                if (ok) {
                    brls::Application::notify(brls::getStr("host_dialog/wol/success"));
                } else {
                    brls::Application::notify(brls::getStr("host_dialog/wol/failure"));
                }
            });
            return true; });
        rightCol->addView(wolBtn);
    }

    // 4. Edit Host Button
    brls::Button* editBtn = new brls::Button();
    editBtn->setText(brls::getStr("host_dialog/dropdown/edit_host"));
    editBtn->setWidth(400.0f);
    editBtn->setMarginBottom(12.0f);
    editBtn->registerClickAction([this](brls::View*)
        {
        brls::sync([this]() {
            auto* editView = EditHostTab::create(this->host);
            auto* frame = new brls::AppletFrame(editView);
            frame->setTitle(brls::getStr("host_dialog/edit_host_title"));
            brls::Application::pushActivity(new brls::Activity(frame));
        });
        return true; });
    rightCol->addView(editBtn);

    // 5. Delete Host Button
    brls::Button* deleteBtn = new brls::Button();
    deleteBtn->setText(brls::getStr("host_dialog/dropdown/delete"));
    deleteBtn->setWidth(400.0f);
    deleteBtn->setMarginBottom(12.0f);
    deleteBtn->registerClickAction([this](brls::View*)
        {
        std::string hostNameCopy = this->host.name;
        brls::sync([this, hostNameCopy]() {
            std::string confirmMsg = brls::getStr("host_dialog/confirm_delete_msg") + "\n" + hostNameCopy;
            auto* confirm = new brls::Dialog(confirmMsg);
            confirm->setCancelable(false);
            confirm->addButton(brls::getStr("host_dialog/yes"), [this, hostNameCopy]() {
                // Perform deletion in background
                std::thread([this, hostNameCopy]() {
                    bool ok = false;
                    try {
                        auto hopt = HostStorage::findHost(hostNameCopy);
                        if (hopt.has_value()) {
                            HostInfo h = *hopt;
                            auto &gsc = GameStreamClient::instance();
                            if (!h.ip.empty()) {
                                if (!gsc.isConnected(h.ip)) {
                                    gsc.connect(h);
                                }
                                if (gsc.isConnected(h.ip)) {
                                    try {
                                        if (gsc.serverData(h.ip).currentGame != 0) {
                                            gsc.quitApp(h.ip);
                                        }
                                    } catch (...) {}
                                    try {
                                        gsc.unpair(h.ip);
                                    } catch (...) {}
                                }
                            }
                        }
                    } catch (...) {}

                    try {
                        ConfigManager cfg; cfg.load();
                        std::string baseDir = cfg.getKeysDir();
                        auto hopt = HostStorage::findHost(hostNameCopy);
                        std::string keyDir;
                        if (hopt.has_value()) {
                            std::string safe = hopt->safeId.empty() ? hostNameCopy : hopt->safeId;
                            keyDir = baseDir + "/" + safe;
                        } else {
                            std::string safeGuess = hostNameCopy;
                            for (char &c : safeGuess) { if (c == '/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|') c = '_'; }
                            keyDir = baseDir + "/" + safeGuess;
                        }
                        namespace fs = std::filesystem;
                        if (fs::exists(keyDir)) {
                            std::string backup = keyDir + ".deleted." + std::to_string(time(NULL));
                            try {
                                fs::rename(keyDir, backup);
                                fs::remove_all(backup);
                                ok = true;
                            } catch (...) {
                                ok = HostStorage::removeHost(hostNameCopy);
                            }
                        } else {
                            ok = HostStorage::removeHost(hostNameCopy);
                        }
                    } catch (...) {
                        ok = HostStorage::removeHost(hostNameCopy);
                    }

                    brls::sync([this, hostNameCopy, ok]() {
                        if (ok) {
                            std::string msg = brls::getStr("host_dialog/notification_deleted") + ": " + hostNameCopy;
                            brls::Application::notify(msg);
                            // Close host menu
                            brls::Application::popActivity(brls::TransitionAnimation::NONE);
                        } else {
                            brls::Application::notify(brls::getStr("host_dialog/change_ip_failure"));
                        }
                        HostsTab::requestGlobalRefresh();
                    });
                }).detach();
            });
            confirm->addButton(brls::getStr("host_dialog/no"), [confirm]() { confirm->close(); });
            confirm->open();
        });
        return true; });
    rightCol->addView(deleteBtn);

    this->addView(rightCol);
}

HostMenuTab::~HostMenuTab()
{
}

brls::View* HostMenuTab::create(const HostInfo& host)
{
    return new HostMenuTab(host);
}
