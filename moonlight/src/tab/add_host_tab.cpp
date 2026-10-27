/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "tab/add_host_tab.hpp"

#include <algorithm>
#include <atomic>
#include <borealis/core/thread.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <vector>

#include "GameStreamClient.hpp"
#include "check_host.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "view/pccard.hpp"

std::atomic<AddHostTab*> AddHostTab::activeInstance { nullptr };

namespace
{

void queueDiscoveredHost(const char* pcname, const char* ip)
{
    AddHostTab* owner = AddHostTab::activeInstance.load(std::memory_order_acquire);
    if (!owner)
        return;
    auto token = owner->aliveToken;

    std::string displayName(pcname ? pcname : "");
    std::string address(ip ? ip : "");
    if (displayName.empty() && address.empty())
        return;
    if (displayName.size() > 50)
        displayName = displayName.substr(0, 47) + "...";

    brls::sync([owner, token, displayName, address]()
        {
        if (!token->load(std::memory_order_acquire)
            || AddHostTab::activeInstance.load(std::memory_order_acquire) != owner
            || !owner->hostsList)
            return;

        const auto duplicate = std::find(owner->discoveredHosts.begin(),
            owner->discoveredHosts.end(), std::make_pair(displayName, address));
        if (duplicate != owner->discoveredHosts.end())
            return;

        owner->discoveredHosts.emplace_back(displayName, address);
        owner->rebuildDiscoveredHostsUI(); });
}

}

AddHostTab::AddHostTab()
{
    this->inflateFromXMLRes("xml/tabs/add_host.xml");
    brls::View* spinner     = this->getView("spinner");
    brls::View* searchLabel = this->getView("search_label");
    if (spinner)
        spinner->setVisibility(brls::Visibility::VISIBLE);
    if (searchLabel)
        searchLabel->setVisibility(brls::Visibility::VISIBLE);

    if (this->ipField)
    {
        std::string placeholder = brls::getStr("moonlight/settings/add_host_ip_placeholder");
        this->ipField->init(brls::getStr("moonlight/settings/ip"), "", [this](const std::string& input)
            {
            if (input.empty())
                return;
            std::string address;
            int port = DEFAULT_GAMESTREAM_HTTP_PORT;
            if (!parseHostEndpoint(input, address, port))
            {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_invalid_address"));
                return;
            }
            this->openPortKeyboard(); }, placeholder, brls::getStr("moonlight/settings/add_host_ip_hint"), 15, brls::KEYBOARD_USE_EXTENDED_NUMBER);
    }
    if (this->portField)
        this->portField->init(brls::getStr("moonlight/settings/add_host_port"), DEFAULT_GAMESTREAM_HTTP_PORT, [](long) { }, brls::getStr("moonlight/settings/add_host_port_hint"), 5);
    if (this->nameField)
        this->nameField->init(brls::getStr("moonlight/settings/add_host_manual"), "", [](std::string) { }, brls::getStr("moonlight/settings/add_host_name_placeholder"), "", 50);

    if (this->preferExternalSelector)
    {
        std::vector<std::string> options = {
            brls::getStr("moonlight/settings/add_host_lan"),
            brls::getStr("moonlight/settings/add_host_online")
        };
        this->preferExternalSelector->init(
            brls::getStr("moonlight/settings/add_host_connection_type"),
            options,
            0,
            [](int) { });
    }

    if (this->addButton && this->ipField && this->portField && this->nameField && this->preferExternalSelector)
    {
        this->addButton->registerClickAction([this](brls::View*)
            {
            vita_log::info("[AddHostTab][MANUAL] Emparejamiento manual iniciado");
            std::string ipInput = this->ipField->getValue();
            std::string name    = this->nameField->getValue();
            if (ipInput.empty() || name.empty())
            {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_missing_fields"));
                return true;
            }

            std::string address;
            int ignoredPort = DEFAULT_GAMESTREAM_HTTP_PORT;
            if (!parseHostEndpoint(ipInput, address, ignoredPort))
            {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_invalid_address"));
                return true;
            }
            const long portInput = this->portField->getValue();
            if (portInput < 1 || portInput > 65535)
            {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_invalid_port"));
                return true;
            }
            const int port = static_cast<int>(portInput);

#if defined(__PSV__)
            AddHostTab* expected = this;
            if (AddHostTab::activeInstance.compare_exchange_strong(expected, nullptr,
                    std::memory_order_acq_rel, std::memory_order_acquire))
            {
                check_host::stopVitaDiscovery();
            }
            else if (expected)
            {
                return true;
            }
#endif

            this->pairingInProgress = true;
            this->inputsUnblockedByPin.store(false);
            brls::Application::blockInputs();
            if (this->addButton)
            {
                this->addButton->setState(brls::ButtonState::DISABLED);
                this->addButton->setFocusable(false);
                this->addButton->setHideHighlight(true);
            }

            auto token = this->aliveToken;
            HostInfo host;
            host.ip     = address;
            host.port   = port;
            host.name   = name;
            host.safeId = makeSafeHostId(name);

            GameStreamClient::instance().beginPairing(host,
                [this, token, name](bool paired)
                {
                    brls::sync([this, token, name, paired]()
                        {
                        if (!token->load(std::memory_order_acquire))
                            return;

                        this->pairingInProgress = false;
                        if (this->addButton)
                        {
                            this->addButton->setHideHighlight(false);
                            this->addButton->setState(brls::ButtonState::ENABLED);
                            this->addButton->setFocusable(true);
                        }
                        if (!this->inputsUnblockedByPin.load())
                            brls::Application::unblockInputs();

                        if (!paired)
                        {
                            brls::Application::notify(brls::getStr("host_dialog/add_host_paired_error_failed"));
                            return;
                        }

                        std::string message = brls::getStr("host_dialog/add_host_paired_success");
                        size_t marker       = message.find("$(name)");
                        if (marker != std::string::npos)
                            message.replace(marker, 7, name);
                        brls::Application::notify(message);
                        this->ipField->setValue("");
                        this->portField->setValue(DEFAULT_GAMESTREAM_HTTP_PORT);
                        this->nameField->setValue("");
                        this->preferExternalSelector->setSelection(0);
                        this->startDeviceDiscovery();
                    });
                },
                [this, token](const std::string&)
                {
                    brls::sync([this, token]()
                        {
                        if (!token->load(std::memory_order_acquire))
                            return;
                        if (!this->inputsUnblockedByPin.exchange(true))
                            brls::Application::unblockInputs();
                    });
                });
            return true; });
    }

    this->startDeviceDiscovery();
}

void AddHostTab::openPortKeyboard()
{
    auto token = this->aliveToken;
    brls::sync([this, token]()
        {
        if (!token->load(std::memory_order_acquire) || !this->portField)
            return;
        brls::Application::getImeManager()->openForNumber([this, token](long port)
            {
            if (!token->load(std::memory_order_acquire) || !this->portField)
                return;
            if (port < 1 || port > 65535)
            {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_invalid_port"));
                return;
            }
            this->portField->setValue(port); },
            brls::getStr("moonlight/settings/add_host_port"), brls::getStr("moonlight/settings/add_host_port_hint"), 5, std::to_string(this->portField->getValue())); });
}
#if defined(_WIN32)
extern "C" void AddHostTab::winHostFoundCb(int idx, const char* host, const char* pcname, const char* ip, int port)
{
    if (!AddHostTab::activeInstance.load(std::memory_order_acquire))
        return;
    vita_log::info("[AddHostTab] Host discovered (Windows): %s - %s:%d (host=%s)",
        pcname ? pcname : "", ip ? ip : "", port, host ? host : "");
    queueDiscoveredHost(pcname, ip);
}
#endif

void AddHostTab::startDeviceDiscovery()
{
    brls::View* spinnerRow = this->getView("spinner_row");
    if (!this->hostsList)
    {
        vita_log::error("[AddHostTab] hostsList is null; cannot clear host list.");
    }
    else if (!spinnerRow)
    {
        vita_log::error("[AddHostTab] spinnerRow is null; cannot show spinner.");
    }
    else
    {
        auto children = this->hostsList->getChildren();
        for (auto* child : children)
        {
            if (child != spinnerRow)
                this->hostsList->removeView(child);
        }
        spinnerRow->setVisibility(brls::Visibility::VISIBLE);
    }
    this->discoveredHosts.clear();
    AddHostTab::activeInstance.store(this, std::memory_order_release);
#if defined(__PSV__)
    check_host::startVitaDiscovery([](int idx, const char* host, const char* pcname, const char* ip, int port)
        {
        if (!AddHostTab::activeInstance.load(std::memory_order_acquire))
            return;
        vita_log::info("[AddHostTab] Host discovered (Vita): %s - %s:%d (host=%s)", pcname ? pcname : "(null)", ip ? ip : "(null)", port, host ? host : "(null)");
        queueDiscoveredHost(pcname, ip); });
#elif defined(_WIN32)
    check_host::startWinDiscovery(winHostFoundCb);
#endif
}

brls::View* AddHostTab::create()
{
    return new AddHostTab();
}

AddHostTab::~AddHostTab()
{
    aliveToken->store(false, std::memory_order_release);
    AddHostTab* expected     = this;
    const bool ownsDiscovery = AddHostTab::activeInstance.compare_exchange_strong(expected, nullptr,
        std::memory_order_acq_rel, std::memory_order_acquire);
    if (ownsDiscovery)
    {
#if defined(__PSV__)
        check_host::stopVitaDiscovery();
#elif defined(_WIN32)
        check_host::stopWinDiscovery();
#endif
    }
    if (this->pairingInProgress.exchange(false))
    {
        brls::Application::unblockInputs();
    }
}

void AddHostTab::rebuildDiscoveredHostsUI()
{
    if (!this->hostsList)
    {
        vita_log::error("[AddHostTab] hostsList is null during rebuild");
        return;
    }

    brls::View* spinnerRow = this->getView("spinner_row");
    auto children          = this->hostsList->getChildren();
    for (auto* child : children)
    {
        if (child != spinnerRow)
            this->hostsList->removeView(child);
    }

    const int cardsPerRow = 3;
    int count             = 0;
    brls::Box* currentRow = nullptr;
    for (const auto& host : this->discoveredHosts)
    {
        if (count % cardsPerRow == 0)
        {
            currentRow = new brls::Box(brls::Axis::ROW);
            currentRow->setMarginBottom(16);
            this->hostsList->addView(currentRow);
        }
        auto* card = new PCCard(host.first.c_str(), "img/moonlight/pc.png");
        card->setFocusable(true);
        card->setMarginRight(16);
        card->setMarginBottom(0);
        std::string ipStr = host.second;
        std::string name  = host.first;
        auto token        = this->aliveToken;
        card->setClickAction([this, token, ipStr, name]()
            {
            std::string msg = brls::getStr("moonlight/settings/add_host_connect_question_dialog");
            size_t pos_ip   = msg.find("$(ip)");
            if (pos_ip != std::string::npos)
                msg.replace(pos_ip, 5, ipStr);
            size_t pos_name = msg.find("$(name)");
            if (pos_name != std::string::npos)
                msg.replace(pos_name, 7, name);
            auto* dialog = new brls::Dialog(msg);
            dialog->addButton(brls::getStr("moonlight/settings/add_host_connect"), [this, token, ipStr, name]()
                {
                if (!token->load(std::memory_order_acquire))
                    return;
                this->ipField->setValue(ipStr);
                this->nameField->setValue(name);
            });
            dialog->addButton(brls::getStr("moonlight/settings/add_host_cancel"), []() { });
            dialog->open(); });
        if (currentRow)
            currentRow->addView(card);
        count++;
    }

    if (spinnerRow)
        spinnerRow->setVisibility(brls::Visibility::VISIBLE);
    else
        vita_log::error("[AddHostTab] spinnerRow es nulo tras rebuild");
}
