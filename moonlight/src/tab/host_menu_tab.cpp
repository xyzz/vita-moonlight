#include "tab/host_menu_tab.hpp"

#include <borealis/core/application.hpp>
#include <borealis/views/applet_frame.hpp>
#include <thread>

#include "GameStreamClient.hpp"
#include "session/session_app_select.hpp"
#include "tab/edit_host_tab.hpp"
#include "tab/hosts_tab.hpp"
#include "utils/connection_test.hpp"
#include "utils/dialog_utils.h"
#include "utils/wol.hpp"

HostMenuTab::HostMenuTab(const HostInfo& hostInfo)
    : host(hostInfo)
{
    this->setAxis(brls::Axis::ROW);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setJustifyContent(brls::JustifyContent::SPACE_EVENLY);
    this->setPadding(30.0f);

    brls::Style style = brls::Application::getStyle();

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

    brls::Box* rightCol = new brls::Box(brls::Axis::COLUMN);
    rightCol->setAlignItems(brls::AlignItems::CENTER);
    rightCol->setJustifyContent(brls::JustifyContent::CENTER);
    rightCol->setWidth(420.0f);

    brls::Button* connectBtn = new brls::Button();
    connectBtn->setText(brls::getStr("host_dialog/dialog/connect"));
    connectBtn->setWidth(400.0f);
    connectBtn->setMarginBottom(12.0f);
    connectBtn->registerClickAction([hostName = host.name](brls::View*)
        {
        brls::Application::popActivity(brls::TransitionAnimation::NONE, [hostName]() {
            auto activities = brls::Application::getActivitiesStack();
            if (activities.empty())
                return;
            auto* applet = dynamic_cast<brls::AppletFrame*>(activities.back()->getContentView());
            if (applet)
                applet->pushContentView(new SessionAppSelect(hostName));
        });
        return true; });
    rightCol->addView(connectBtn);

    brls::Button* testBtn = new brls::Button();
    testBtn->setText(brls::getStr("host_dialog/dialog/test_connection"));
    testBtn->setWidth(400.0f);
    testBtn->setMarginBottom(12.0f);
    testBtn->registerClickAction([hostInfo = host](brls::View*)
        {
        utils::startConnectionTest(hostInfo);
        return true; });
    rightCol->addView(testBtn);

    if (!host.mac.empty())
    {
        brls::Button* wolBtn = new brls::Button();
        wolBtn->setText(brls::getStr("host_dialog/wol/button"));
        wolBtn->setWidth(400.0f);
        wolBtn->setMarginBottom(12.0f);
        wolBtn->registerClickAction([mac = host.mac, address = host.ip](brls::View*)
            {
            bool ok = utils::sendWOLPacket(mac, address);
            brls::Application::notify(brls::getStr(ok
                    ? "host_dialog/wol/success"
                    : "host_dialog/wol/failure"));
            return true; });
        rightCol->addView(wolBtn);
    }

    brls::Button* editBtn = new brls::Button();
    editBtn->setText(brls::getStr("host_dialog/dropdown/edit_host"));
    editBtn->setWidth(400.0f);
    editBtn->setMarginBottom(12.0f);
    editBtn->registerClickAction([hostInfo = host](brls::View*)
        {
        auto* editView = EditHostTab::create(hostInfo);
        auto* frame    = new brls::AppletFrame(editView);
        frame->setTitle(brls::getStr("host_dialog/edit_host_title"));
        brls::Application::pushActivity(new brls::Activity(frame));
        return true; });
    rightCol->addView(editBtn);

    brls::Button* deleteBtn = new brls::Button();
    deleteBtn->setText(brls::getStr("host_dialog/dropdown/delete"));
    deleteBtn->setWidth(400.0f);
    deleteBtn->setMarginBottom(12.0f);
    deleteBtn->registerClickAction([hostName = host.name](brls::View*)
        {
        std::string confirmMsg = brls::getStr("host_dialog/confirm_delete_msg") + "\n" + hostName;
            auto* confirm = new brls::Dialog(confirmMsg);
            confirm->setCancelable(false);
        confirm->addButton(brls::getStr("host_dialog/yes"), [hostName]() {
            std::thread([hostName]() {
                    bool ok = false;
                    try {
                    auto hopt = HostStorage::findHost(hostName);
                        if (hopt.has_value()) {
                            HostInfo h = *hopt;
                            auto &gsc = GameStreamClient::instance();
                            if (!h.ip.empty()) {
                                if (!gsc.isConnected(h.ip)) {
                                    gsc.connect(h);
                                }
                                if (gsc.isConnected(h.ip)) {
                                    try {
                                        if (gsc.currentGame(h.ip) != 0) {
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
                    ok = HostStorage::removeHost(hostName);
                    } catch (...) {}

                brls::sync([hostName, ok]() {
                        if (ok) {
                        std::string msg = brls::getStr("host_dialog/notification_deleted") + ": " + hostName;
                            brls::Application::notify(msg);
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
        return true; });
    rightCol->addView(deleteBtn);

    this->addView(rightCol);
}

brls::View* HostMenuTab::create(const HostInfo& host)
{
    return new HostMenuTab(host);
}
