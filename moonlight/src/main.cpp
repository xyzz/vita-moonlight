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

#include <borealis.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "ConfigManager.hpp"
#include "activity/main_activity.hpp"
#include "settings.hpp"
#include "tab/add_host_tab.hpp"
#include "tab/hosts_tab.hpp"
#include "tab/settings_tab.hpp"
#include "utils/host_search.hpp"
// Include debug wrapper for console/Vita output testing
#include "debug.hpp"
// For connectivity tests/certificates
// #include "check_test.hpp"

#if defined(__PSV__)
#include <psp2/kernel/threadmgr.h>
#include <psp2/vshbridge.h>
#endif

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
// Needed for the OpenGL driver to work
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

using namespace brls::literals; // for _i18n

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
        moonlight::settings::applyLanguageEnv(lang); // <-- Force locale only if there is config
        // Set default locale on platform before init
        if (lang == "es")
        {
            brls::Platform::APP_LOCALE_DEFAULT = "es";
        }
        else if (lang == "en-US")
        {
            brls::Platform::APP_LOCALE_DEFAULT = "en-US";
        }
    }

    // We recommend to use INFO for real apps
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-d") == 0)
        { // Set log level
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        }
        else if (std::strcmp(argv[i], "-o") == 0)
        {
            const char* path = (i + 1 < argc) ? argv[++i] : "borealis.log";
            brls::Logger::setLogOutput(std::fopen(path, "w+"));
        }
        else if (std::strcmp(argv[i], "-v") == 0)
        {
            brls::Application::enableDebuggingView(true);
        }
    }

    // Init the app and i18n. This also initializes the platform.
    brls::Logger::info("main: init app");
    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    // Change locale on platform after init
    if (!lang.empty())
    {
        // For PS Vita we don't need to change the locale dynamically
        // The locale was already set before init()
    }

    // Load translations after applying language from config
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

    // --- LOG TEST BLOCK ---
    // Print various forms to compare behavior on console/Vita
    std::string testName = "AorsiniYT-PC.local";
    std::string testUtf8 = u8"á>Àü↕"; // example with non-ASCII bytes
    // Correct: pass c_str() to printf-style functions
    // Enable vita_debug_log as configured (loads early to allow prints from init)
    {
        extern bool g_debug_log_enabled; // declared in vita_globals.hpp
        ConfigManager cfg;
        cfg.load();
        VideoSettings vs    = cfg.getVideoSettings();
        g_debug_log_enabled = vs.save_debug_log;
    }
    vita_debug_log("[TEST] vita_debug_log c_str: %s", testName.c_str());
    // Other outputs to compare
#if defined(__PSV__)
    brls::Logger::info("[TEST] cout skipped on Vita, testName={} testUtf8={}", testName, testUtf8);
#else
    std::cout << "[TEST] cout: " << testName << " " << testUtf8 << std::endl;
#endif
    brls::Logger::info("[TEST] brls::Logger: {} {}", testName, testUtf8);
    // Run diagnostic tests (connectivity/certificates)
    // moonlight::tests::run_cert_checks();
    // --- END OF TESTING BLOCK ---

    // Load visual settings (selector) after init
    moonlight::settings::loadSettingsFromConfig();

    brls::Application::createWindow("moonlight/title"_i18n);

    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    brls::Application::setGlobalQuit(false);

    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("HostsTab", HostsTab::create);

#if defined(__PSV__)
    // CapUnlocker disabled: we prevent affinity/priority change and any behavior
    // that depends on external modules to expand limits (reduces RAM consumption/fragmentation).
    /*
    int search_unk[2];
    if(_vshKernelSearchModuleByName("CapUnlocker", search_unk) >= 0) {
        brls::Logger::info("[CapUnlocker] ¡CapUnlocker detectado por _vshKernelSearchModuleByName!");
        brls::sync([]() {
            brls::Application::notify("moonlight/capunlocker_notify"_i18n);
        });
        int res1 = sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        int res2 = sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, 0xF);
        brls::Logger::info("[CapUnlocker] Cambio de prioridad res={} | Cambio de afinidad res={}", res1, res2);
    } else {
        brls::Logger::info("[CapUnlocker] CapUnlocker NO detectado. Solo 1-2 núcleos y RAM limitada disponibles.");
    }
    */
#endif

    // The visual style of the "about" labels (font and color) is applied in the corresponding view controller
    // using setFontSize and setTextColor after getting each Label by its id.
    // Padding/margin metrics are only maintained here if the XML layout uses them.
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
