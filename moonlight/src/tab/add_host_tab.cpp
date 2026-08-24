#if 1
/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#endif
#include "tab/add_host_tab.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <cstring>
#include <fstream>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "view/pccard.hpp"
#ifdef _WIN32
#include <direct.h>
#include <windows.h>

#include <thread>
// Advance declaration for global use
std::vector<std::pair<std::string, std::string>>& getDiscoveredHostsWin();
#endif
#include <algorithm>
#include <atomic>
#include <borealis/core/thread.hpp>
#include <sstream>
#include <thread>
#include <vector>

#if defined(__PSV__)
#include <psp2/kernel/threadmgr.h>
#endif

#include "check_host.hpp"

using namespace brls::literals;

AddHostTab::AddHostTab()
{
    vita_log::info("[AddHostTab] Constructor: Iniciando inflateFromXMLRes");
    this->inflateFromXMLRes("xml/tabs/add_host.xml");
    // --- Diagnosis of available CPU cores and current usage ---
// --- Diagnosis of available CPU cores and current usage ---
#if defined(__PSV__)
    int cpuCoreCount = 4; // Theoretical
    vita_log::info("[AddHostTab][CPU] Núcleos CPU PSVita (teórico): %d", cpuCoreCount);
    // Get affinity mask from main thread
    int affinityMask = sceKernelGetThreadCpuAffinityMask(0);
    vita_log::info("[AddHostTab][CPU] Affinity mask del hilo principal: 0x%X", affinityMask);
    // Show in the log which cores are actually available
    std::string availableCores;
    for (int i = 0; i < 4; ++i)
    {
        if (affinityMask & (1 << i))
        {
            availableCores += std::to_string(i) + " ";
        }
    }
    if (!availableCores.empty())
        vita_log::info("[AddHostTab][CPU] Núcleos disponibles para este hilo: %s", availableCores.c_str());
    else
        vita_log::warning("[AddHostTab][CPU] ¡Ningún núcleo disponible para este hilo! (mask=0x%X)", affinityMask);
#else
    unsigned int cpuCoreCount = std::thread::hardware_concurrency();
    vita_log::info("[AddHostTab][CPU] Núcleos CPU detectados: %u", cpuCoreCount);
#endif
    // Log of thread IDs and affinity (diagnosis)
#if defined(__PSV__)
    SceUID threadId = sceKernelGetThreadId();
    vita_log::info("[AddHostTab][CPU] ID del hilo principal: 0x%X", threadId);
#else
    std::stringstream ss;
    ss << std::this_thread::get_id();
    vita_log::info("[AddHostTab][CPU] ID del hilo principal: %s", ss.str().c_str());
#endif

    // The spinner and the label are already in the XML, control visibility by id
    brls::View* spinner     = this->getView("spinner");
    brls::View* searchLabel = this->getView("search_label");
    if (spinner)
        spinner->setVisibility(brls::Visibility::VISIBLE);
    if (searchLabel)
        searchLabel->setVisibility(brls::Visibility::VISIBLE);

    // Static texts (title) are already handled in XML with i18n.
    // XML placeholders are not localized at runtime.
    // Manual translation and log testing
    std::string testKey         = "moonlight/settings/add_host_ip_placeholder";
    std::string testTranslation = brls::getStr(testKey);
    vita_log::info("[AddHostTab] Prueba manual: clave='%s', traducción='%s'", testKey.c_str(), testTranslation.c_str());
    vita_log::info("[AddHostTab] Locale activo: %s", brls::Application::getLocale().c_str());
    vita_log::info("[AddHostTab] Ruta recursos esperada: resources/i18n/es/moonlight.json");

    if (this->ipField)
    {
        std::string placeholder = brls::getStr("moonlight/settings/add_host_ip_placeholder");
        vita_log::info("[AddHostTab] Placeholder resolved: %s", placeholder.c_str());
        // Allow IP:PORT up to 22 characters (ex: 255.255.255.255:65535)
        this->ipField->init(brls::getStr("moonlight/settings/ip"), "", [](std::string) { }, placeholder, "", 22);
    }
    if (this->nameField)
        // Extended limit for PC name: 50 characters
        this->nameField->init(brls::getStr("moonlight/settings/add_host_manual"), "", [](std::string) { }, brls::getStr("moonlight/settings/add_host_name_placeholder"), "", 50);

    // Initialize the LAN/Online preference selector
    // Use init(title, options, default selection, change callback)
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
            [](int) { } // empty callback, customizable
        );
    }

    if (this->addButton && this->ipField && this->nameField && this->preferExternalSelector)
    {
        // Manual Pair Button (Add Host Manual)
        this->addButton->registerClickAction([this](brls::View*)
            {
            vita_log::info("[AddHostTab][MANUAL] Emparejamiento manual iniciado");
#if defined(__PSV__)
            if (AddHostTab::vitaInstance != nullptr) {
                check_host::stopVitaDiscovery();
                if (AddHostTab::vitaInstance != this) {
                    vita_log::warning("[AddHostTab][SAFE] Descubrimiento ya fue cerrado por otra instancia, abortando.");
                    return true;
                }
                AddHostTab::vitaInstance = nullptr;
            }
#endif
            std::string ipInput = this->ipField->getValue();
            std::string name = this->nameField->getValue();
            if (ipInput.empty() || name.empty()) {
                brls::Application::notify(brls::getStr("moonlight/settings/add_host_missing_fields"));
                return true;
            }
#if defined(__PSV__)
            if (AddHostTab::vitaInstance != nullptr) {
                brls::Application::notify("Por favor espera a que termine la búsqueda de dispositivos antes de emparejar.");
                vita_log::warning("[AddHostTab][SAFE] Emparejamiento bloqueado: descubrimiento aún activo.");
                return true;
            }
#endif
            // Cancel any previous pairing
            if (this->pairingContext) this->pairingContext->cancelled = true;
            if (this->pairingThread.joinable()) this->pairingThread.join();
            this->pairingContext = std::make_shared<PairingContext>();
            auto context = this->pairingContext;
            // Pairing logic: delegate dialog UI to pairing.cpp
            // Mark that pairing is in progress to prevent the window from closing
            // and disable the Add button to prevent multiple simultaneous launches.
            this->pairingInProgress = true;
            // Reset PIN Unlock Flag
            this->inputsUnblockedByPin.store(false);
            brls::Application::blockInputs();
            if (this->addButton) {
                this->addButton->setState(brls::ButtonState::DISABLED);
                this->addButton->setFocusable(false);
                this->addButton->setHideHighlight(true);
            }
            auto weakSelf = this->weak_from_this();
            HostInfo h; h.ip = ipInput; h.name = name; h.safeId = makeSafeHostId(name.empty()? ipInput : name);
            // onPinReady unlocks inputs when the PIN appears so the user can
            // insert it into the host machine. `inputsUnblockedByPin` prevents duplicate unlocks.
            GameStreamClient::instance().beginPairing(h, [this, name](bool ok){
                // Restore UI state and unlock inputs in UI thread
                brls::sync([this, ok, name]() {
                    this->pairingInProgress = false;
                    if (this->addButton) {
                        this->addButton->setHideHighlight(false);
                        this->addButton->setState(brls::ButtonState::ENABLED);
                        this->addButton->setFocusable(true);
                    }
                    // Unlock inputs to allow closing the window or other actions
                    // If the inputs have already been unlocked by onPinReady, we do not call again
                    if (!this->inputsUnblockedByPin.load()) {
                        brls::Application::unblockInputs();
                    }
                    if (ok) {
                        // Show localized notification with host name
                        std::string msg = brls::getStr("host_dialog/add_host_paired_success");
                        size_t pos = msg.find("$(name)"); if (pos != std::string::npos) msg.replace(pos, 7, name);
                        brls::Application::notify(msg);
                        if (this->ipField) this->ipField->setValue("");
                        if (this->nameField) this->nameField->setValue("");
                        if (this->preferExternalSelector) this->preferExternalSelector->setSelection(0);
                        this->refreshHostsList();
                    } else {
                        brls::Application::notify(brls::getStr("host_dialog/add_host_paired_error_failed"));
                    }
                });
            }, [this](const std::string& pin){
                // Callback when the PIN is already ready and displayed in the pairing dialog.
                // We unlock inputs in the UI thread so that the user can switch to the
                // host machine and enter the PIN.
                if (!this->inputsUnblockedByPin.exchange(true)) {
                    brls::sync([this]() {
                        brls::Application::unblockInputs();
                        // Optional: we could move focus to a control if necessary
                    });
                }
            });
            return true; });
    }
    // The found devices label is translated into XML, not necessary here

    this->startDeviceDiscovery();
}

// Discovery callbacks.

// Static instances for discovery callbacks
#if defined(__PSV__)
AddHostTab* AddHostTab::vitaInstance = nullptr;
// Advance declaration for use in the entire file
#if defined(_WIN32)
std::vector<std::pair<std::string, std::string>>& getDiscoveredHostsWin();
#endif

#elif defined(_WIN32)
AddHostTab* AddHostTab::winInstance = nullptr;
extern "C" void AddHostTab::winHostFoundCb(int idx, const char* host, const char* pcname, const char* ip, int port)
{
    if (!AddHostTab::winInstance)
        return;
    vita_log::info("[AddHostTab] Host detectado (WIN): %s - %s:%d (host=%s)", pcname, ip, port, host);
    vita_log::info("[AddHostTab] Datos callback WIN: host='%s' pcname='%s' ip='%s' port=%d", host, pcname, ip, port);
    // Use the global list of discovered hosts in Windows
    auto& discoveredHostsWin = getDiscoveredHostsWin();
    std::string name(pcname ? pcname : "");
    std::string ipStr(ip ? ip : "");
    std::string displayName        = name;
    constexpr size_t maxDisplayLen = 50;
    if (displayName.length() > maxDisplayLen && maxDisplayLen > 3)
    {
        displayName = displayName.substr(0, maxDisplayLen - 3) + "...";
    }
    else if (displayName.length() > maxDisplayLen && maxDisplayLen <= 3)
    {
        displayName = std::string("...").substr(0, maxDisplayLen);
    }
    if (name.empty() && ipStr.empty())
    {
        vita_log::error("[AddHostTab] Host detectado pero sin datos válidos (WIN)");
        return;
    }
    // Avoid duplicates
    for (const auto& h : discoveredHostsWin)
    {
        if (h.first == displayName && h.second == ipStr)
            return;
    }
    discoveredHostsWin.push_back({ displayName, ipStr });
    brls::sync([=]()
        {
        if (!AddHostTab::winInstance->hostsList) {
            vita_log::error("[AddHostTab] hostsList es nulo (WIN)");
            return;
        }
        // Clean everything except the spinner
        brls::View* spinnerRow = AddHostTab::winInstance->getView("spinner_row");
        auto children = AddHostTab::winInstance->hostsList->getChildren();
        for (auto* child : children) {
            if (child != spinnerRow)
                AddHostTab::winInstance->hostsList->removeView(child);
        }
        // grid parameters
        const int cardsPerRow = 3;
        int count = 0;
        brls::Box* currentRow = nullptr;
        for (const auto& host : discoveredHostsWin) {
            if (count % cardsPerRow == 0) {
                currentRow = new brls::Box(brls::Axis::ROW);
                currentRow->setMarginBottom(16);
                AddHostTab::winInstance->hostsList->addView(currentRow);
            }
            auto* card = new PCCard(host.first.c_str(), "img/moonlight/pc.png");
            card->setFocusable(true);
            card->setMarginRight(16);
            card->setMarginBottom(0);
            // Truncation/animation logic is handled internally in PCCard
            // Capture the host pair (name, ip) by value
            card->setClickAction([host]() {
                std::string msg = brls::getStr("moonlight/settings/add_host_connect_question_dialog");
                size_t pos_ip = msg.find("$(ip)");
                if (pos_ip != std::string::npos)
                    msg.replace(pos_ip, 5, host.second);
                size_t pos_name = msg.find("$(name)");
                if (pos_name != std::string::npos)
                    msg.replace(pos_name, 7, host.first);
                auto* dialog = new brls::Dialog(msg);
                dialog->addButton(brls::getStr("moonlight/settings/add_host_connect"), [host, dialog]() {
                    if (AddHostTab::winInstance && AddHostTab::winInstance->ipField) AddHostTab::winInstance->ipField->setValue(host.second);
                    if (AddHostTab::winInstance && AddHostTab::winInstance->nameField) AddHostTab::winInstance->nameField->setValue(host.first);

                });
                dialog->addButton(brls::getStr("moonlight/settings/add_host_cancel"), [dialog]() {

                });
                dialog->open();
            });
            if (currentRow)
                currentRow->addView(card);
            count++;
        }
        // Spinner always at the end
        if (spinnerRow)
            spinnerRow->setVisibility(brls::Visibility::VISIBLE);
        else
            vita_log::error("[AddHostTab] spinnerRow es nulo tras añadir host (WIN)"); });
}
#endif

void AddHostTab::startDeviceDiscovery()
{
    // Clear host list and show spinner
    brls::View* spinnerRow = this->getView("spinner_row");
    if (!this->hostsList)
    {
        vita_log::error("[AddHostTab] hostsList es nulo. No se puede limpiar la lista de hosts.");
    }
    else if (!spinnerRow)
    {
        vita_log::error("[AddHostTab] spinnerRow es nulo. No se puede mostrar el spinner.");
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
    // Launch discovery using the new centralized interface
#if defined(__PSV__)
    AddHostTab::vitaInstance = this;
    this->discoveredHosts.clear();
    check_host::startVitaDiscovery([](int idx, const char* host, const char* pcname, const char* ip, int port)
        {
        if (!AddHostTab::vitaInstance) {
            vita_log::error("[check_host] vitaInstance es nulo");
            return;
        }
        vita_log::info("[AddHostTab] Host detectado (VITA): %s - %s:%d (host=%s)", pcname ? pcname : "(null)", ip ? ip : "(null)", port, host ? host : "(null)");
        vita_log::info("[AddHostTab] Datos callback VITA: host='%s' pcname='%s' ip='%s' port=%d", host ? host : "(null)", pcname ? pcname : "(null)", ip ? ip : "(null)", port);
        std::string name(pcname ? pcname : "");
        std::string ipStr(ip ? ip : "");
        std::string displayName = name;
        constexpr size_t maxDisplayLen = 50;
        if (displayName.length() > maxDisplayLen && maxDisplayLen > 3) {
            displayName = displayName.substr(0, maxDisplayLen - 3) + "...";
        } else if (displayName.length() > maxDisplayLen && maxDisplayLen <= 3) {
            displayName = std::string("...").substr(0, maxDisplayLen);
        }
        if (name.empty() && ipStr.empty()) {
            vita_log::error("[AddHostTab] Host detectado pero sin datos válidos (VITA)");
            return;
        }
        brls::sync([displayName, ipStr]() {
            AddHostTab* self = AddHostTab::vitaInstance;
            if (!self) {
                vita_log::error("[check_host] vitaInstance liberada antes de procesar host");
                return;
            }
            if (!self->hostsList) {
                vita_log::error("[check_host] hostsList es nulo");
                return;
            }
            auto duplicate = std::find_if(self->discoveredHosts.begin(), self->discoveredHosts.end(), [&](const auto& h) {
                return h.first == displayName && h.second == ipStr;
            });
            if (duplicate != self->discoveredHosts.end())
                return;
            self->discoveredHosts.emplace_back(displayName, ipStr);
            self->rebuildDiscoveredHostsUI();
        }); });
#elif defined(_WIN32)
    AddHostTab::winInstance = this;
    // Clear the list of hosts discovered when searching
    getDiscoveredHostsWin().clear();
    check_host::startWinDiscovery(winHostFoundCb);
#endif
    // When the search is finished, the callback is responsible for hiding the spinner if there are no hosts
}

brls::View* AddHostTab::create()
{
    // Create the instance as a raw pointer, Borealis manages the life cycle
    return new AddHostTab();
}

void AddHostTab::refreshHostsList()
{
    // Do nothing here, the list is handled in startDeviceDiscovery
}

AddHostTab::~AddHostTab()
{
    vita_log::info("[AddHostTab] (DEBUG) Destructor: deteniendo hilo de descubrimiento y pairing...");
#if defined(__PSV__)
    AddHostTab::vitaInstance = nullptr;
    check_host::stopVitaDiscovery();
#elif defined(_WIN32)
    check_host::stopWinDiscovery();
    AddHostTab::winInstance = nullptr;
    getDiscoveredHostsWin().clear();
#endif
    // Cancel secure pairing
    if (this->pairingContext)
        this->pairingContext->cancelled = true;
    if (this->pairingThread.joinable())
        this->pairingThread.join();
    // If for some reason the pairing was in progress, unblock inputs so as not to leave the UI blocked
    if (this->pairingInProgress)
    {
        brls::Application::unblockInputs();
        this->pairingInProgress = false;
    }
}

#if defined(_WIN32)
// Global access to the list of discovered hosts in Windows
std::vector<std::pair<std::string, std::string>>& getDiscoveredHostsWin()
{
    static std::vector<std::pair<std::string, std::string>> discoveredHostsWin;
    return discoveredHostsWin;
}
#endif

#if defined(__PSV__)
void AddHostTab::rebuildDiscoveredHostsUI()
{
    if (!this->hostsList)
    {
        vita_log::error("[AddHostTab] hostsList es nulo durante rebuild");
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
        card->setClickAction([this, ipStr, name]()
            {
            std::string msg = brls::getStr("moonlight/settings/add_host_connect_question_dialog");
            size_t pos_ip = msg.find("$(ip)");
            if (pos_ip != std::string::npos)
                msg.replace(pos_ip, 5, ipStr);
            size_t pos_name = msg.find("$(name)");
            if (pos_name != std::string::npos)
                msg.replace(pos_name, 7, name);
            auto* dialog = new brls::Dialog(msg);
            dialog->addButton(brls::getStr("moonlight/settings/add_host_connect"), [this, ipStr, name, dialog]() {
                if (this->ipField)
                    this->ipField->setValue(ipStr);
                if (this->nameField)
                    this->nameField->setValue(name);
            });
            dialog->addButton(brls::getStr("moonlight/settings/add_host_cancel"), [dialog]() {
            });
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
#endif
