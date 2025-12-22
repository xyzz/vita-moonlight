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
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/edit_text_dialog.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "activity/main_activity.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "session/session_app_select.hpp"
#include "tab/edit_host_tab.hpp"
#include "utils/dialog_utils.h"
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
        brls::Logger::info("[HostsTab::requestGlobalRefresh] Pushing new MainActivity (safe reload)");
        // Push a fresh MainActivity on top. We avoid clearing the whole stack because
        // Application::clear() is private. This may leave the old MainActivity below,
        // but pushing a fresh one provides a clean UI state without in-place mutations
        // that previously caused crashes on PSVita.
        brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
        vita_debug_log("[HostsTab::requestGlobalRefresh] New MainActivity pushed");
        auto activities = brls::Application::getActivitiesStack();
        if (!activities.empty()) {
            brls::Application::giveFocus(nullptr); // Clear focus from previous activity
            brls::Application::giveFocus(activities.back()->getContentView());
            vita_debug_log("[HostsTab::requestGlobalRefresh] Focus given to new MainActivity");
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

    // --- Comprobación de hosts guardados ---
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
        brls::Logger::info("[HostsTab::refreshHostsList] Procesando host: %s (%s)", host.name.c_str(), host.ip.c_str());
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
            // Logging multiplataforma para click
            VITALOG("[PCCard] Click en card de host: %s (%s)\n", host.name.c_str(), host.ip.c_str());
            auto* dialog = new brls::Dialog(brls::getStr("host_dialog/dialog/title"));

            // Evita que el diálogo se cierre automáticamente al pulsar un botón
            dialog->setCancelable(false);

            dialog->addButton(brls::getStr("host_dialog/dialog/connect"), [this, host](/*dialog*/) {
                sceClibPrintf("[PCCard] Conectar a %s (%s)\n", host.name.c_str(), host.ip.c_str());
                brls::sync([this, host] {
                    this->present(new SessionAppSelect(host.name));
                });
            });
            dialog->addButton(brls::getStr("host_dialog/dialog/info"), [dialog, host]() {
                sceClibPrintf("[PCCard] Info para %s (%s)\n", host.name.c_str(), host.ip.c_str());

                // Crear diálogo de información del host usando dialog_utils
                brls::Style style = brls::Application::getStyle();

                // Crear el contenido con las filas de información
                std::vector<std::pair<std::string, std::string>> infoRows = {
                    {brls::getStr("host_dialog/info/name"), host.name},
                    {brls::getStr("host_dialog/info/ip"), host.ip},
                    {brls::getStr("host_dialog/info/mac"), host.mac}
                };

                brls::Box* infoContent = new brls::Box(brls::Axis::COLUMN);
                infoContent->setAlignItems(brls::AlignItems::FLEX_START);
                infoContent->setJustifyContent(brls::JustifyContent::FLEX_START);
                infoContent->setWidth(620.0f);

                // Título del diálogo
                auto* titleLabel = createLabel(brls::getStr("host_dialog/info/title"),
                                               style["brls/applet_frame/header_title_font_size"],
                                               brls::HorizontalAlign::CENTER, 18.0f);
                infoContent->addView(titleLabel);

                // Filas de información
                brls::Box* infoBox = createInfoBox(infoRows,
                                                    style["brls/label/default_font_size"],
                                                    10.0f);
                infoContent->addView(infoBox);

                // Crear diálogo personalizado
                DialogOptions options;
                options.contentPadding = style["brls/dialog/paddingTopBottom"] * 0.6f;
                options.contentWidth = 620.0f;
                options.alignItems = brls::AlignItems::FLEX_START;
                options.cancelable = true;

                auto* infoDialog = createCustomDialog(infoContent, options);
                infoDialog->addButton(brls::getStr("host_dialog/dialog/ok"), []() {
                    // Cerrar automáticamente
                });
                infoDialog->open();

                // FORCE FOCUS FIX: The selector disappears because labels aren't focusable.
                // We need to kick the focus manager to find the OK button.
                brls::sync([infoDialog]() {
                    brls::Application::giveFocus(infoDialog);
                });
            });
            dialog->addButton(brls::getStr("host_dialog/dialog/settings"), [this, host](/*dialog*/) {
                sceClibPrintf("[PCCard] Settings para %s (%s)\n", host.name.c_str(), host.ip.c_str());
                // El diálogo se cerrará automáticamente, solo abrir el dropdown en el siguiente frame
                brls::sync([this, host]() {
                    VITALOG("[hosts_tab.cpp] Entrando en brls::sync para crear Dropdown de settings para host: %s\n", host.name.c_str());
                    std::vector<std::string> options = {
                        brls::getStr("host_dialog/dropdown/edit_host"),
                        brls::getStr("host_dialog/dropdown/delete"),
                        brls::getStr("host_dialog/dropdown/pair_online")
                    };
                    std::string dropdownTitle = brls::getStr("host_dialog/dropdown/title") + ": " + host.name;
                    VITALOG("[hosts_tab.cpp] Creando Dropdown con título: %s\n", dropdownTitle.c_str());
                    // Crear el dropdown con un callback que cierre la activity actual (dropdown)
                    // de forma diferida y luego abra la activity de edición. Usamos brls::sync
                    // para evitar reentrancias al hacer pop/push de activities.
                    auto* dropdown = new brls::Dropdown(dropdownTitle, options, [this, host](int selected) {
                        VITALOG("[Dropdown] Opción seleccionada: %d para host: %s\n", selected, host.name.c_str());
                        if (selected == 0) {
                            brls::sync([this, host]() {
                                brls::Application::pushActivity(new brls::Activity(EditHostTab::create(host)));
                            });
                        } else if (selected == 1) {
                            // Confirm delete with spinner and background removal to avoid UI jank
                            std::string hostNameCopy = host.name;
                            brls::sync([this, hostNameCopy]() {
                                std::string confirmMsg = brls::getStr("host_dialog/confirm_delete_msg") + "\n" + hostNameCopy;
                                auto* confirm = new brls::Dialog(confirmMsg);
                                confirm->setCancelable(false);
                                std::string hostToRemove = hostNameCopy;
                                    confirm->addButton(brls::getStr("host_dialog/yes"), [hostToRemove]() {
#ifdef __PSV__
                                    sceClibPrintf("[PCCard] Eliminando host: %s\n", hostToRemove.c_str());
#endif
                                    // show spinner in the top HostsTab if present
                                    brls::sync([]() {
                                        auto activities = brls::Application::getActivitiesStack();
                                        if (!activities.empty()) {
                                            brls::Activity* top = activities.back();
                                            if (top) {
                                                brls::View* content = top->getContentView();
                                                HostsTab* hostsTab = dynamic_cast<HostsTab*>(content);
                                                if (hostsTab && hostsTab->hostsList) {
                                                    auto* s = new brls::ProgressSpinner(brls::ProgressSpinnerSize::NORMAL);
                                                    s->setMarginTop(12);
                                                    s->setMarginBottom(12);
                                                    hostsTab->hostsList->addView(s);
                                                }
                                            }
                                        }
                                    });

                                    // perform graceful remote cleanup then removal in background
                                    std::thread([hostToRemove]() {
                                        bool ok = false;
                                        try {
                                            // Intentar obtener la información completa del host (incluye IP)
                                            auto hopt = HostStorage::findHost(hostToRemove);
                                            if (hopt.has_value()) {
                                                HostInfo h = *hopt;
                                                // Intentar conectar y, si procede, terminar app remota y unpair
                                                auto &gsc = GameStreamClient::instance();
                                                if (!h.ip.empty()) {
                                                    // intentar connect si no está conectado
                                                    if (!gsc.isConnected(h.ip)) {
                                                        gsc.connect(h);
                                                    }
                                                    if (gsc.isConnected(h.ip)) {
                                                        // Si hay una sesión activa, intentar cerrarla
                                                        try {
                                                            if (gsc.serverData(h.ip).currentGame != 0) {
                                                                gsc.quitApp(h.ip);
                                                            }
                                                        } catch (...) {
                                                            // Ignorar fallos en quitApp
                                                        }
                                                        // Intentar desemparejar en el host
                                                        try {
                                                            gsc.unpair(h.ip);
                                                        } catch (...) {
                                                            // Ignorar fallos en unpair
                                                        }
                                                    }
                                                }
                                            }
                                        } catch (...) {
                                            // Ignorar cualquier excepción y proceder a la eliminación local
                                        }

                                            // Intentar renombrar el keyDir a un backup temporal y luego eliminarlo.
                                            try {
                                                ConfigManager cfg; cfg.load();
                                                std::string baseDir = cfg.getKeysDir();
                                                // Intentar resolver host para obtener safeId
                                                auto hopt = HostStorage::findHost(hostToRemove);
                                                std::string keyDir;
                                                if (hopt.has_value()) {
                                                    std::string safe = hopt->safeId.empty() ? hostToRemove : hopt->safeId;
                                                    keyDir = baseDir + "/" + safe;
                                                } else {
                                                    // Si no se encuentra, intentar con safe derivado
                                                    std::string safeGuess = hostToRemove;
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
                                                        // Si el rename o remove_all falla intentar el método tradicional
                                                        ok = HostStorage::removeHost(hostToRemove);
                                                    }
                                                } else {
                                                    // fallback
                                                    ok = HostStorage::removeHost(hostToRemove);
                                                }
                                            } catch (...) {
                                                // Si algo falla, intentar eliminación directa
                                                ok = HostStorage::removeHost(hostToRemove);
                                            }
                                        brls::sync([hostToRemove, ok]() {
                                            if (ok) {
                                                std::string msg = brls::getStr("host_dialog/notification_deleted") + ": " + hostToRemove;
                                                brls::Application::notify(msg);
                                            } else {
                                                brls::Application::notify(brls::getStr("host_dialog/change_ip_failure"));
                                            }
                                            // Solicitar recarga global segura del home
                                            HostsTab::requestGlobalRefresh();
                                        });
                                    }).detach();
                                });
                                confirm->addButton(brls::getStr("host_dialog/no"), [confirm]() { confirm->close(); });
                                confirm->open();
                            });

                        } else if (selected == 2) {
                            sceClibPrintf("[PCCard] Emparejar online fuera de casa: %s\n", host.name.c_str());
                            // Lógica real para emparejar online aquí
                        }
                    }, -1);
                    brls::Application::pushActivity(new brls::Activity(dropdown));
                });
            });
            dialog->addButton(brls::getStr("main/cancel"), [dialog]() {
                dialog->close();
            });
            dialog->open(); });
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
