/*
    Copyright 2020-2021 natinusala
    Copyright 2019 p-sam

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#if defined(ANDROID) || defined(IOS)
#include <SDL2/SDL_main.h>
#endif

#include <atomic>
#include <borealis.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "ConfigManager.hpp"
#include "Settings.hpp"
#include "activity/main_activity.hpp"
#include "debug.hpp"
#include "session/vita_session.hpp"
#include "tab/add_host_tab.hpp"
#include "tab/hosts_tab.hpp"
#include "tab/settings_tab.hpp"
#include "utils/dialog_utils.h"

#if defined(__PSV__)
#include <psp2/shellutil.h>
#endif

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
// Needed for the OpenGL driver to work
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

using namespace brls::literals; // for _i18n

namespace
{

#if defined(__PSV__)
std::atomic<bool> appBackgrounded { false };
std::atomic<bool> resumeReconnectPending { false };

struct ResumeReconnectState
{
    brls::Dialog* dialog = nullptr;
    int retriesRemaining = 1;
};

void finishResumeReconnect(const std::shared_ptr<ResumeReconnectState>& state, bool reconnected)
{
    if (state->dialog)
    {
        state->dialog->close();
        state->dialog = nullptr;
    }
    brls::Application::unblockInputs();
    resumeReconnectPending.store(false);

    if (reconnected)
    {
        vita_log::info("[Main] Stream reconectado tras reanudar la aplicación");
    }
    else
    {
        vita_log::error("[Main] No se pudo reconectar el stream tras reanudar la aplicación");
        brls::Application::notify(brls::getStr("moonlight/session/app_select/reconnect_failed"));
    }
}

void reconnectStreamAfterResume(const std::shared_ptr<ResumeReconnectState>& state)
{
    auto session = VitaSession::active();
    if (!session)
    {
        brls::sync([state]()
            {
            if (state->dialog) {
                state->dialog->close();
                state->dialog = nullptr;
            }
            brls::Application::unblockInputs();
            resumeReconnectPending.store(false); });
        return;
    }

    bool reconnected = session->reconnectAfterResume();
    brls::sync([state, reconnected]()
        {
        if (reconnected) {
            finishResumeReconnect(state, true);
        } else if (state->retriesRemaining > 0) {
            state->retriesRemaining--;
            vita_log::warning("[Main] Reconnect tras resume falló; reintentando en 1 segundo");
            brls::delay(1000, [state]() {
                std::thread([state]() {
                    reconnectStreamAfterResume(state);
                }).detach();
            });
        } else {
            finishResumeReconnect(state, false);
        } });
}

void beginResumeReconnect()
{
    if (!VitaSession::active())
    {
        resumeReconnectPending.store(false);
        return;
    }

    auto state = std::make_shared<ResumeReconnectState>();
    brls::Application::blockInputs();
    state->dialog = createLoadingDialog(brls::getStr("moonlight/session/app_select/reconnecting"));

    brls::delay(150, [state]()
        { std::thread([state]()
              { reconnectStreamAfterResume(state); })
              .detach(); });
}

void handleWindowFocusChanged(bool focused)
{
    if (!focused)
    {
        appBackgrounded.store(true);
        vita_log::info("[Main] Aplicación suspendida o enviada a segundo plano");
        return;
    }

    if (!appBackgrounded.exchange(false) || resumeReconnectPending.exchange(true))
    {
        return;
    }

    vita_log::info("[Main] Aplicación reanudada; programando reconexión del stream");
    // Power callbacks run on Borealis' Vita callback thread.
    brls::sync([]()
        { brls::delay(750, []()
              { beginResumeReconnect(); }); });
}
#endif

}

int main(int argc, char* argv[])
{
    std::string configPath = ConfigManager::getConfigPath();
    size_t pos             = configPath.find_last_of("/\\");
    if (pos != std::string::npos)
    {
        std::string configDir = configPath.substr(0, pos);
#ifdef _WIN32
        CreateDirectoryA(configDir.c_str(), NULL);
#else
        mkdir(configDir.c_str(), 0755);
#endif
    }

    ConfigManager tempConfig;
    std::string keysDir = tempConfig.getKeysDir();
    pos                 = keysDir.find_last_of("/\\");
    if (pos != std::string::npos)
    {
        std::string keysParentDir = keysDir.substr(0, pos);
#ifdef _WIN32
        CreateDirectoryA(keysParentDir.c_str(), NULL);
#else
        mkdir(keysParentDir.c_str(), 0755);
#endif
    }

    // Create keyboard folder in data/moonlight and copy default CSS if it doesn't exist
    {
        std::string cfgPath     = ConfigManager::getConfigPath();
        size_t p                = cfgPath.find_last_of("/\\");
        std::string cfgDir      = (p != std::string::npos) ? cfgPath.substr(0, p) : ".";
        std::string keyboardDir = cfgDir + "/keyboard";
#ifdef _WIN32
        CreateDirectoryA(keyboardDir.c_str(), NULL);
#else
        mkdir(keyboardDir.c_str(), 0755);
#endif
        std::string destCss = keyboardDir + "/style.css";
        struct stat st {};
        if (stat(destCss.c_str(), &st) != 0)
        {
            // CSS does not exist in data, copy from resources
            std::string srcCss = "resources/keyboard/style.css";
            std::ifstream src(srcCss, std::ios::binary);
            if (src.is_open())
            {
                std::ofstream dst(destCss, std::ios::binary);
                if (dst.is_open())
                {
                    dst << src.rdbuf();
#if defined(__PSV__)
                    brls::Logger::info("[main] Copiado CSS teclado por defecto a {}", destCss);
#else
                    std::cout << "[main] Copiado CSS teclado por defecto a " << destCss << std::endl;
#endif
                }
            }
            else
            {
#if defined(__PSV__)
                brls::Logger::info("[main] No se encontró resources/keyboard/style.css para copiar");
#else
                std::cout << "[main] No se encontró resources/keyboard/style.css para copiar" << std::endl;
#endif
            }
        }
    }

    std::string lang = moonlight::settings::getLanguageFromConfig();
#if defined(__PSV__)
    brls::Logger::info("[DEBUG] Idioma forzado desde config: {}", lang);
#else
    std::cout << "[DEBUG] Idioma forzado desde config: " << lang << std::endl;
#endif
    if (!lang.empty())
    {
        moonlight::settings::applyLanguageEnv(lang);
        brls::Platform::APP_LOCALE_DEFAULT = lang;
    }

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-d") == 0)
        {
            vita_debug_log("[main] -d recibido: usando salida via vita_debug_log");
        }
        else if (std::strcmp(argv[i], "-o") == 0)
        {
            const char* path = (i + 1 < argc) ? argv[++i] : "borealis.log";
            vita_debug_log("[main] -o ignorado (ruta solicitada: %s), salida controlada por save_debug_log", path);
        }
        else if (std::strcmp(argv[i], "-v") == 0)
        {
            brls::Application::enableDebuggingView(true);
        }
    }

#if defined(__PSV__)
    sceShellUtilInitEvents(0);
#endif

    // Init the app and i18n. This also initializes the platform.
    vita_log::info("main: init app");
    if (!brls::Application::init())
    {
        vita_log::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    if (!lang.empty())
    {
        brls::loadTranslations();
#if defined(__PSV__)
        brls::Logger::info("[DEBUG] Traducciones cargadas para idioma: {}", lang);
#else
        std::cout << "[DEBUG] Traducciones cargadas para idioma: " << lang << std::endl;
#endif
    }

#if defined(__PSV__)
    brls::Logger::info("[DEBUG] Locale detectado por Borealis: {}", brls::Application::getLocale());
#else
    std::cout << "[DEBUG] Locale detectado por Borealis: " << brls::Application::getLocale() << std::endl;
#endif

    {
        ConfigManager cfg;
        cfg.load();
        enable_file_logging(cfg.getVideoSettings().save_debug_log);
    }

    brls::Application::createWindow("moonlight/title"_i18n);

#if defined(__PSV__)
    brls::Application::getWindowFocusChangedEvent()->subscribe(handleWindowFocusChanged);
#endif

    {
        ConfigManager startupCfg;
        startupCfg.load();
        VideoSettings vs = startupCfg.getVideoSettings();
        moonlight::settings::applySwapInterval(vs.swap_interval);
        vita_log::info("[Main] Display timing: swapInterval=%d", vs.swap_interval);
        vita_log::info("[Main] FPS status enabled: %d", brls::Application::getFPSStatus());
    }

    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    brls::Application::setGlobalQuit(false);

    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("HostsTab", HostsTab::create);

    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
    {
    }

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
