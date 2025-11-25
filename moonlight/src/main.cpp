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
// Incluir wrapper de debug para pruebas de salida en consola / Vita
#include "debug.hpp"
// Para pruebas de conectividad / certificados
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

    // Crear carpeta para keyboard en data/moonlight y copiar CSS por defecto si no existe
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
            // No existe el css en data, copiar desde resources
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
        moonlight::settings::applyLanguageEnv(lang); // <-- Forzar el locale solo si hay config
        // Establecer el locale por defecto en la plataforma antes de init
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

    // Cambiar locale en la plataforma después de init
    if (!lang.empty())
    {
        // Para PS Vita no necesitamos cambiar el locale dinámicamente
        // El locale ya se estableció antes de init()
    }

    // Cargar traducciones después de aplicar el idioma desde config
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

    // --- BLOQUE DE PRUEBAS DE LOG ---
    // Imprimir varias formas para comparar comportamiento en consola/Vita
    std::string testName = "AorsiniYT-PC.local";
    std::string testUtf8 = u8"á>Àü↕"; // ejemplo con bytes no-ASCII
    // Correcto: pasar c_str() a funciones estilo printf
    // Habilitar vita_debug_log según configuración (se carga temprano para permitir prints desde init)
    {
        extern bool g_debug_log_enabled; // declarado en vita_globals.hpp
        ConfigManager cfg;
        cfg.load();
        VideoSettings vs    = cfg.getVideoSettings();
        g_debug_log_enabled = vs.save_debug_log;
    }
    vita_debug_log("[TEST] vita_debug_log c_str: %s", testName.c_str());
    // Otras salidas para comparar
#if defined(__PSV__)
    brls::Logger::info("[TEST] cout skipped on Vita, testName={} testUtf8={}", testName, testUtf8);
#else
    std::cout << "[TEST] cout: " << testName << " " << testUtf8 << std::endl;
#endif
    brls::Logger::info("[TEST] brls::Logger: {} {}", testName, testUtf8);
    // Ejecutar pruebas diagnósticas (conectividad / certificados)
    // moonlight::tests::run_cert_checks();
    // --- FIN BLOQUE DE PRUEBAS ---

    // Cargar settings visuales (selector) después de init
    moonlight::settings::loadSettingsFromConfig();

    brls::Application::createWindow("moonlight/title"_i18n);

    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    brls::Application::setGlobalQuit(false);

    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("HostsTab", HostsTab::create);

#if defined(__PSV__)
    int search_unk[2];
    if (_vshKernelSearchModuleByName("CapUnlocker", search_unk) >= 0)
    {
        brls::Logger::info("[CapUnlocker] ¡CapUnlocker detectado por _vshKernelSearchModuleByName!");
        brls::sync([]()
            { brls::Application::notify("moonlight/capunlocker_notify"_i18n); });
        int res1 = sceKernelChangeThreadPriority(SCE_KERNEL_THREAD_ID_SELF, 64);
        int res2 = sceKernelChangeThreadCpuAffinityMask(SCE_KERNEL_THREAD_ID_SELF, 0xF);
        brls::Logger::info("[CapUnlocker] Cambio de prioridad res={} | Cambio de afinidad res={}", res1, res2);
    }
    else
    {
        brls::Logger::info("[CapUnlocker] CapUnlocker NO detectado. Solo 1-2 núcleos y RAM limitada disponibles.");
    }
#endif

    // El estilo visual de los labels de "about" (fuente y color) se aplica en el controlador de la vista correspondiente
    // usando setFontSize y setTextColor tras obtener cada Label por su id.
    // Aquí solo se mantienen las métricas de padding/margen si el layout XML las utiliza.
    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
