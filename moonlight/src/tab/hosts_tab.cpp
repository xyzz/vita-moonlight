#if 1
/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#endif

#include "tab/hosts_tab.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/edit_text_dialog.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <cctype>
#include <fstream>
#include <thread>

#include "activity/main_activity.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "tab/host_menu_tab.hpp"
#include "utils/host_search.hpp"
#include "view/pccard.hpp"
#ifdef _WIN32
#include <direct.h>
#endif

#ifdef __PSV__
#include <psp2/kernel/clib.h>
#define VITALOG sceClibPrintf
#else
#define VITALOG(...) ((void)0)
#endif

namespace
{

std::string trim_copy(const std::string& value)
{
    auto begin = value.begin();
    auto end   = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;
    return std::string(begin, end);
}
}

void HostsTab::requestGlobalRefresh()
{
    // Safer approach: clear the activities stack and push a fresh MainActivity.
    // Use a short delayed task so we don't delete the current activity while
    // still handling its input/event (prevents reentrancy crashes on Vita).
    brls::delay(50, []()
        {
        vita_log::info("[HostsTab::requestGlobalRefresh] Clearing stack and pushing new MainActivity");
        // Pop all existing activities to prevent duplicates in the stack.
        // Application::clear() is private, so we pop one by one.
        auto stack = brls::Application::getActivitiesStack();
        size_t initialCount = stack.size();
        for (size_t i = 0; i < initialCount; ++i) {
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
        }
        vita_log::info("[HostsTab::requestGlobalRefresh] Popped %u activities", (unsigned)initialCount);
        // Push a fresh MainActivity
        brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
        vita_log::info("[HostsTab::requestGlobalRefresh] New MainActivity pushed");
        auto activities = brls::Application::getActivitiesStack();
        if (!activities.empty()) {
            brls::Application::giveFocus(nullptr); // Clear focus from previous activity
            brls::Application::giveFocus(activities.back()->getContentView());
            vita_log::info("[HostsTab::requestGlobalRefresh] Focus given to new MainActivity");
        } });
}

HostsTab::HostsTab()
{
    VITALOG("[HostsTab::HostsTab] Constructor llamado\n");
    this->inflateFromXMLRes("xml/tabs/hosts_tab.xml");
    VITALOG("[HostsTab::HostsTab] Antes de refreshHostsList\n");
    this->refreshHostsList();
    VITALOG("[HostsTab::HostsTab] Después de refreshHostsList\n");
}

brls::View* HostsTab::create()
{
    return new HostsTab();
}

void HostsTab::refreshHostsList()
{
    VITALOG("[HostsTab::refreshHostsList] INICIO\n");
    if (!this->hostsList)
    {
        VITALOG("[HostsTab::refreshHostsList] hostsList es nullptr, saliendo\n");
        return;
    }
    this->hostsList->clearViews(true);
    constexpr int CARDS_PER_ROW = 3;

    // --- Checking saved hosts ---
    auto hosts = HostStorage::loadHosts();
    if (hosts.empty())
    {
        auto* emptyItem = new brls::Label();
        emptyItem->setText(brls::getStr("host_dialog/host_list_empty"));
        emptyItem->setFontSize(16);
        emptyItem->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        this->hostsList->addView(emptyItem);
        return;
    }
    int count      = 0;
    brls::Box* row = nullptr;
    for (const auto& host : hosts)
    {
        // Logging multiplataforma
#ifdef __PSV__
        VITALOG("[HostsTab::refreshHostsList] Procesando host: %s (%s)\n", host.name.c_str(), host.ip.c_str());
#else
        vita_log::info("[HostsTab::refreshHostsList] Procesando host: %s (%s)", host.name.c_str(), host.ip.c_str());
#endif
        if (count % CARDS_PER_ROW == 0)
        {
            row = new brls::Box(brls::Axis::ROW);
            this->hostsList->addView(row);
            VITALOG("[HostsTab::refreshHostsList] Nueva fila creada\n");
        }
        auto* card = new PCCard(host.name, "img/moonlight/pc.png");
        card->setClickAction([this, host, card]()
            {
            VITALOG("[PCCard] Click en card de host: %s (%s)\n", host.name.c_str(), host.ip.c_str());
            auto* menuView = HostMenuTab::create(host);
            auto* frame = new brls::AppletFrame(menuView);
            frame->setTitle(brls::getStr("host_dialog/dialog/title"));
            brls::Application::pushActivity(new brls::Activity(frame)); });
        if ((count + 1) % CARDS_PER_ROW != 0)
        {
            card->setMarginRight(16);
        }
        row->addView(card);
#ifdef __PSV__
        VITALOG("[HostsTab::refreshHostsList] Card añadida para host: %s\n", host.name.c_str());
#endif
        count++;
    }
}
