/*
    session_main.cpp - Pantalla principal de sesión activa (streaming) para Moonlight PSVita/Windows
    Autor: aorsini + comunidad
*/
#include "session/session_main.hpp"

#include <borealis/extern/nanovg/nanovg.h>
#include <vita2d.h>

#include <borealis/core/application.hpp>
#include <borealis/core/frame_context.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/style.hpp>
#include <borealis/views/dialog.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib> // getenv
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "borealis.hpp"
#include "controller/ControllerInput.hpp"
#include "debug.hpp"
#include "model/HostStorage.hpp"
#include "session/hotkey_manager.hpp"
#include "session/overlay/ingame_overlay_view.hpp"
#include "session/overlay/test_overlay_stream.hpp"
#include "session/overlay/vita_pause_overlay.hpp"
#include "session/vita_session.hpp"
#include "video/VitaVideoRenderer.hpp"
#include "video/legacy/modules/vita_globals.hpp"
#include "video/legacy/vita.hpp"

// Implementación del constructor
SessionMainView::SessionMainView(const HostInfo& host, const RemoteAppInfo& app)
    : brls::Box()
    , host(host)
    , app(app)
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 255));

    this->inflateFromXMLRes("xml/views/session_main.xml");

    // Asegurar que vita2d esté listo ANTES de cualquier potencial draw (defensivo)
    extern bool vita2d_inited;
    if (!vita2d_inited)
    {
        vita2d_init();
        vita2d_inited = true;
        vita2d_set_vblank_wait(0);
    }

    // Inicializar input manager
    g_controllerInput = new ControllerInputManager();

    // Notificar al servidor del tipo de gamepad guardado en config
    VitaSession::notifyGamepadType();

    // Registrar callback de pausa en ControllerInputManager (START+L+R)
    // Evitar abrir múltiples overlays si se mantiene la combinación pulsada.
    g_controllerInput->setPauseCallback([this]()
        {
        // Atomically check-and-set the pause flag to avoid duplicate overlays
        if (SessionMainView::pauseOverlayOpen.exchange(true)) return;
        // Deshabilitar envío de input mientras el overlay esté abierto para
        // evitar que los botones interactúen con la transmisión.
        if (g_controllerInput) g_controllerInput->setInputEnabled(false);
        auto overlay = new VitaPauseOverlay([this]() {
            // restablecer el flag y reactivar el input cuando el overlay se cierre
            SessionMainView::pauseOverlayOpen.store(false);
            if (g_controllerInput) g_controllerInput->setInputEnabled(true);
            // pop the overlay activity (Application::popActivity will manage input tokens and focus)
            brls::Application::popActivity();
        }, this->host);
        auto* activity = new brls::Activity(overlay);
        brls::Application::pushActivity(activity);
        brls::Application::giveFocus(overlay->getDefaultFocus()); }); // Resetear input para evitar estados residuales de la UI anterior
    if (g_controllerInput)
        g_controllerInput->dropInput();

    // Ocultar UI base para dejar solo video y overlay
    if (title)
        title->setVisibility(brls::Visibility::GONE);
    if (appLabel)
        appLabel->setVisibility(brls::Visibility::GONE);
    if (info)
        info->setVisibility(brls::Visibility::GONE);
    if (endBtn)
        endBtn->setVisibility(brls::Visibility::GONE);

    // Nota: la gestión de atajo de pausa se realiza desde ControllerInputManager.
    // Antes se registraba también en HotkeyManager pero esto causaba que el
    // overlay se creara dos veces en condiciones de carrera. Se evita la
    // duplicación registrando el callback solo en el input manager.


    // Intentar forzar loop de render a 60fps si el host negocia >30.
    // Recuperar configuración de streaming y video.
    ConfigManager cfgMgr;
    cfgMgr.load();
    StreamConfiguration streamCfg = cfgMgr.getStreamConfig();
    VideoSettings videoSettings   = cfgMgr.getVideoSettings();

    g_controllerInput->setTouchscreenMode(videoSettings.touchscreen_mode);

    if (!overlayStatsView)
    {
        overlayStatsView = std::make_unique<VitaStreamOverlayView>();
    }
    if (overlayStatsView)
    {
        this->addView(overlayStatsView.get());
    }

    // Overlay de prueba: desactivado
    // if (!testOverlay) {
    //     testOverlay = std::make_unique<TestOverlayStream>();
    // }
    // if (testOverlay) {
    //     testOverlay->setVisibility(brls::Visibility::GONE); // Desactivado
    //     this->addView(testOverlay.get());
    //     // Dar foco al overlay de prueba para que pueda recibir navegación
    //     // brls::sync([this]() {
    //     //     if (testOverlay) brls::Application::giveFocus(testOverlay.get());
    //     // });
    // }

    g_video_settings_snapshot.show_fps = videoSettings.show_fps;

    unsigned targetFps = (unsigned)streamCfg.fps;
    if (targetFps == 0)
        targetFps = 60;
    if (targetFps > 60)
        targetFps = 60;
    brls::Application::setLimitedFPS(targetFps); // limitar FPS según configuración
    brls::Application::setSwapInterval(1); // habilitar vsync para estabilidad
    brls::Logger::info("[SessionMainView] Init render config (cfg_fps={} -> limitedFPS={} swapInterval=1)", targetFps, targetFps);

    // Modo Direct GXM eliminado. render_mode se normaliza: 0=legacy,1=ffmpeg (futuro)
    bool settingsChanged = false;
    if (videoSettings.render_mode > 1)
    {
        videoSettings.render_mode = 0;
        brls::Logger::info("[SessionMainView] render_mode deprecated (>1) normalizado a 0 (legacy)");
        settingsChanged = true;
    }
    if (settingsChanged)
    {
        cfgMgr.setVideoSettings(videoSettings);
        cfgMgr.save();
    }

    brls::Application::giveFocus(this);
}
void SessionMainView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
{
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    if (g_controllerInput)
        g_controllerInput->handleInput();
    auto t1 = high_resolution_clock::now();

    // Nueva ruta: usar NanoVG si está disponible para pintar el frame sin tocar el ciclo vita2d
    if (vg)
    {
        VitaVideoRenderer::instance().drawNVG(vg, width, height, 1.0f);
    }
    else
    {
        // Fallback (sin vg): ruta directa vita2d (no debería suceder normalmente en Borealis)
        VitaVideoRenderer::instance().draw(width, height);
    }
    auto t2 = high_resolution_clock::now();

    Box::draw(vg, x, y, width, height, style, ctx);
    auto t3 = high_resolution_clock::now();

    // Throttled logging (una vez cada ~500ms) para evitar spam
    static uint64_t lastFrameLogMs = 0;
    uint64_t now_ms                = duration_cast<milliseconds>(t3.time_since_epoch()).count();
    if (now_ms - lastFrameLogMs > 500)
    {
        lastFrameLogMs = now_ms;
        auto input_us  = duration_cast<microseconds>(t1 - t0).count();
        auto video_us  = duration_cast<microseconds>(t2 - t1).count();
        auto ui_us     = duration_cast<microseconds>(t3 - t2).count();
        auto total_us  = duration_cast<microseconds>(t3 - t0).count();
        int fps_i      = (int)std::lround(brls::Application::getFPS());
        // vita_debug_log("[SessionMain][PERF] fps=%d input=%lldus video=%lldus ui=%lldus total=%lldus", fps_i,
        //                (long long)input_us, (long long)video_us, (long long)ui_us, (long long)total_us);
    }
}

// Función para lanzar la pantalla principal de sesión
void showSessionMain(const HostInfo& host, const RemoteAppInfo& app)
{
    auto* view = new SessionMainView(host, app);
    brls::Application::pushActivity(new brls::Activity(view));
}

SessionMainView::~SessionMainView()
{
    overlayStatsView.release();
    testOverlay.release();
    if (g_controllerInput)
    {
        delete g_controllerInput;
        g_controllerInput = nullptr;
    }
    g_controllerInput = new ControllerInputManager();
}
