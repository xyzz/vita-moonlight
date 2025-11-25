/*
    session_app_select.cpp - Selección de aplicación para iniciar streaming en Moonlight PSVita/Windows
    Autor: aorsini + comunidad
*/
#include "session/session_app_select.hpp"

#include <sys/stat.h>

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <cstdlib> // Para malloc/free
#include <cstring> // Para strcpy
#include <set>
#include <string> // Para std::string

#include "ConfigManager.hpp"
#include "GameStreamClient.hpp"
#include "Limelight.h"
#include "client.h"
#include "model/HostStorage.hpp"
#include "session/session_main.hpp"
#include "session/vita_session.hpp"
#include "utils/dialog_utils.h"
#include "video/legacy/modules/vita_globals.hpp" // For VITA_STREAM constants
#include "view/pccard.hpp"

namespace
{
// Hosts en los que se suprimirá temporalmente el diálogo de sesión activa
static std::set<std::string> g_suppressedActiveHosts;
// Hosts para los que ya mostramos el diálogo 'resume' durante la entrada
static std::set<std::string> g_resumeShownHosts;

// NOTE: Implementations moved to class methods so they can access
// private members (gridView) directly.
}

brls::Dialog* SessionAppSelect::showConnectingDialog(const std::string& msg, brls::Visibility& outPrevGridVis)
{
    outPrevGridVis = brls::Visibility::GONE;
    brls::Application::blockInputs();

    if (this->gridView)
    {
        outPrevGridVis = this->gridView->getVisibility();
        // Ocultar por completo el GridView y quitar foco/highlight
        this->gridView->setVisibility(brls::Visibility::GONE);
        this->gridView->setHideHighlight(true);
        this->gridView->setHideHighlightBackground(true);
        this->gridView->setHideHighlightBorder(true);
        this->gridView->setFocusable(false);
        brls::Application::giveFocus(nullptr);

        // Ocultar highlights de todos los descendientes
        std::function<void(brls::View*)> walkHideLocal;
        walkHideLocal = [&walkHideLocal](brls::View* v)
        {
            if (!v)
                return;
            v->setHideHighlight(true);
            v->setHideHighlightBackground(true);
            v->setHideHighlightBorder(true);
            v->setFocusable(false);
            brls::Box* box = dynamic_cast<brls::Box*>(v);
            if (!box)
                return;
            auto ch = box->getChildren();
            for (auto* cc : ch)
                walkHideLocal(cc);
        };
        auto rows = this->gridView->getChildren();
        for (auto* row : rows)
            walkHideLocal(row);
    }

    return createLoadingDialog(msg);
}

void SessionAppSelect::restoreGridViewAndInputs(brls::Visibility prevGridVis)
{
    brls::Application::unblockInputs();
    if (this->gridView)
    {
        this->gridView->setHideHighlight(false);
        this->gridView->setHideHighlightBackground(false);
        this->gridView->setHideHighlightBorder(false);
        this->gridView->setFocusable(true);
        if (prevGridVis == brls::Visibility::VISIBLE)
            brls::Application::giveFocus(this->gridView);
        this->gridView->setVisibility(prevGridVis);

        std::function<void(brls::View*)> walkRestoreLocal;
        walkRestoreLocal = [&walkRestoreLocal](brls::View* v)
        {
            if (!v)
                return;
            v->setHideHighlight(false);
            v->setHideHighlightBackground(false);
            v->setHideHighlightBorder(false);
            v->setFocusable(true);
            brls::Box* box = dynamic_cast<brls::Box*>(v);
            if (!box)
                return;
            auto ch = box->getChildren();
            for (auto* cc : ch)
                walkRestoreLocal(cc);
        };
        auto rows2 = this->gridView->getChildren();
        for (auto* row : rows2)
            walkRestoreLocal(row);
    }
}

using namespace brls::literals;

SessionAppSelect::SessionAppSelect(const std::string& hostName)
    : brls::Box(brls::Axis::COLUMN)
    , host()
{
    brls::Logger::info("View: SessionAppSelect para host: %s", hostName.c_str());

    // Buscar el host real por nombre
    auto found = HostStorage::findHost(hostName);
    if (found)
    {
        this->host = *found;
    }
    else
    {
        brls::Logger::error("[SessionAppSelect] No se encontró el host '%s' en HostStorage", hostName.c_str());
        // Dejar host vacío, mostrará error en populateAppList
    }

    this->inflateFromXMLRes("xml/views/session_app_select.xml");

    // Configurar títulos
    app_select_title->setText(hostName);
    app_select_subtitle->setText(brls::getStr("moonlight/session/app_select/subtitle"));

    // Crear y configurar el GridView dinámicamente
    gridView = new GridView();
    gridView->setColumns(3); // 3 columnas para PS Vita (mejor ajuste)
    gridView->setWidth(brls::Box::AUTO);
    gridView->setGrow(1.0f);
    grid_placeholder->addView(gridView);

    // Configurar el spinner con tamaño apropiado
    BRLS_BIND(brls::ProgressSpinner, loading_spinner, "loading_spinner");
    spinner = loading_spinner;
    if (!spinner)
    {
        spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::NORMAL);
    }

    // Comprobar si existe una sesión activa — delegando la lógica a GameStreamClient
    HostInfo hostCopy = this->host;
    brls::async([this, hostCopy]() mutable
        {
        RemoteAppInfo running;
        bool active = GameStreamClient::instance().probeActiveSession(hostCopy, running);
        if (active) {
            brls::sync([this, running, hostCopy]() {
                // Si la vista ya decidió suprimir el diálogo (por ejemplo el
                // usuario pulsó Resume antes de que esta comprobación termine),
                // evitamos volver a mostrarlo.
                if (this->suppressActiveDialog) {
                    brls::Logger::info("[SessionAppSelect] suppressActiveDialog set -> omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // Chequear si a nivel global se suprimió el diálogo para este host
                if (g_suppressedActiveHosts.count(hostCopy.ip)) {
                    brls::Logger::info("[SessionAppSelect] g_suppressedActiveHosts contains host -> omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // Si el GridView no está visible es porque ya estamos en un flujo
                // de conexión/inicio (AppSelected), por lo que NO debemos mostrar
                // el diálogo de sesión activa en ese momento.
                if (this->gridView && this->gridView->getVisibility() != brls::Visibility::VISIBLE) {
                    brls::Logger::info("[SessionAppSelect] gridView no visible -> omitiendo diálogo de sesión activa");
                    return;
                }

                // Si ya mostramos el diálogo en esta instancia, no volver a hacerlo
                if (this->activeDialogShown) {
                    brls::Logger::info("[SessionAppSelect] activeDialogShown set — omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                // Si ya mostramos el diálogo para este host durante la visita (global), omitir
                if (g_resumeShownHosts.count(hostCopy.ip)) {
                    brls::Logger::info("[SessionAppSelect] g_resumeShownHosts contains host — omitiendo diálogo de sesión activa");
                    this->populateAppList();
                    return;
                }
                brls::Logger::info("[SessionAppSelect] Sesión activa detectada para {} -> mostrar diálogo Reanudar/Empezar nuevo (suppressActiveDialog={}, g_suppressedActiveHosts={}, activeDialogShown={}, g_resumeShownHosts={})",
                                    hostCopy.ip,
                                    this->suppressActiveDialog ? "1" : "0",
                                    g_suppressedActiveHosts.count(hostCopy.ip) ? "1" : "0",
                                    this->activeDialogShown ? "1" : "0",
                                    g_resumeShownHosts.count(hostCopy.ip) ? "1" : "0");
                auto* holder = new brls::Box(brls::Axis::COLUMN);
                auto* label = new brls::Label();
                std::string msg = brls::getStr("moonlight/session/app_select/active_session_msg");
                size_t pos = msg.find("$(app)");
                if (pos != std::string::npos) msg.replace(pos, 6, running.name);
                label->setText(msg);
                label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                label->setMarginBottom(18);

                auto* btnBox = new brls::Box(brls::Axis::ROW);
                btnBox->setJustifyContent(brls::JustifyContent::CENTER);
                btnBox->setAlignItems(brls::AlignItems::CENTER);

                auto* resumeBtn = new brls::Button();
                resumeBtn->setText(brls::getStr("moonlight/session/app_select/button_resume"));
                resumeBtn->setStyle(&brls::BUTTONSTYLE_HIGHLIGHT);
                resumeBtn->setMargins(0, 8, 0, 0);

                auto* newBtn = new brls::Button();
                newBtn->setText(brls::getStr("moonlight/session/app_select/button_start_new"));
                newBtn->setStyle(&brls::BUTTONSTYLE_PRIMARY);

                // No mostrar botón 'Re-pair' en este diálogo para reducir fricción UX.
                btnBox->addView(resumeBtn);
                btnBox->addView(newBtn);
                holder->addView(label);
                holder->addView(btnBox);
                holder->setPadding(18,18,18,18);

                auto* dialog = new brls::Dialog(holder);
                dialog->setCancelable(true);
                dialog->open();

                // Marcar que ya mostramos el diálogo en esta instancia y a nivel de host
                this->activeDialogShown = true;
                g_resumeShownHosts.insert(hostCopy.ip);

                resumeBtn->registerClickAction([this, running, dialog, hostCopy](brls::View*) -> bool {
                    dialog->dismiss();
                        // Marcar para que no se vuelva a mostrar el diálogo activo en esta vista
                        this->suppressActiveDialog = true;
                        // Marcar a nivel de host para evitar reaparecer si la vista se recrea
                        g_resumeShownHosts.insert(hostCopy.ip);
                        // También suprimir globalmente el diálogo para este host mientras
                        // procesamos el resume — esto evita condiciones de carrera donde
                        // probeActiveSession vuelva a disparar la UI.
                        g_suppressedActiveHosts.insert(hostCopy.ip);
                        brls::Logger::info("[SessionAppSelect] resumeBtn: inserted host into g_suppressedActiveHosts for {}", hostCopy.ip);
                        // Log current suppression set size for diagnostics
                        brls::Logger::info("[SessionAppSelect] g_suppressedActiveHosts size={} (contains host={})", (int)g_suppressedActiveHosts.size(), g_suppressedActiveHosts.count(hostCopy.ip)?1:0);
                    // Reanudar la sesión activa forzando el inicio aunque PairStatus sea 0
                    this->AppSelected(running, true);
                    return true;
                });

                // repairBtn removed: pairing via this dialog no longer exposed here.

                newBtn->registerClickAction([this, running, dialog, hostCopy](brls::View*) -> bool {
                    dialog->dismiss();
                    // Evitar que el diálogo de sesión activa reaparezca
                    this->suppressActiveDialog = true;
                    auto* waitDlg = createLoadingDialog(brls::getStr("moonlight/session/app_select/ending_session"));
                    std::thread([hostCopy, this, waitDlg]() mutable {
                        brls::Logger::info("[SessionAppSelect] Enviando quitApp para {} antes de mostrar apps", hostCopy.ip);
                        if (!GameStreamClient::instance().isConnected(hostCopy.ip)) {
                            GameStreamClient::instance().connect(hostCopy);
                        }
                        bool ok = GameStreamClient::instance().quitApp(hostCopy.ip);
                        brls::sync([this, ok, waitDlg]() {
                            if (waitDlg) waitDlg->close();
                            if (!ok) {
                                brls::Application::notify(brls::getStr("moonlight/session/app_select/error_end_session"));
                            }
                            this->populateAppList();
                        });
                    }).detach();
                    return true;
                });
            });
        } else {
            // Si probeActiveSession devolvió false, no mostramos diálogo de mismatch aquí.
            brls::sync([this]() {
                this->populateAppList();
            });
        } });
}

SessionAppSelect::~SessionAppSelect()
{
    // El destructor de la vista se encarga de liberar los hijos (gridView, spinner, etc)
    // Limpiar la marca global para que la próxima entrada al host pueda volver a mostrar el diálogo.
    try
    {
        if (!this->host.ip.empty() && g_resumeShownHosts.count(this->host.ip))
            g_resumeShownHosts.erase(this->host.ip);
        if (!this->host.ip.empty() && g_suppressedActiveHosts.count(this->host.ip))
            g_suppressedActiveHosts.erase(this->host.ip);
    }
    catch (...)
    {
        // No hacer nada si erase lanza (no debería)
    }
}

void SessionAppSelect::onLayout()
{
    Box::onLayout();
}

void SessionAppSelect::populateAppList()
{
    brls::Logger::info("[SessionAppSelect] populateAppList llamado para host: {} (ip: {})", host.name, host.ip);

    // Mostrar spinner y ocultar contenido
    if (spinner)
        spinner->setVisibility(brls::Visibility::VISIBLE);
    if (gridView)
        gridView->setVisibility(brls::Visibility::INVISIBLE);
    if (app_select_empty)
        app_select_empty->setVisibility(brls::Visibility::GONE);

    if (host.name.empty() || host.ip.empty())
    {
        if (spinner)
            spinner->setVisibility(brls::Visibility::GONE);
        if (app_select_empty)
        {
            app_select_empty->setText(brls::getStr("moonlight/session/app_select/error_no_host"));
            app_select_empty->setVisibility(brls::Visibility::VISIBLE);
        }
        return;
    }

    brls::async([this]()
        {
    brls::Logger::info("[SessionAppSelect] Llamando a GameStreamClient::getAppList para host: {} (ip: {})", host.name, host.ip);
    std::vector<RemoteAppInfo> apps;
    GameStreamClient::instance().getAppList(this->host.ip, [&apps](const std::vector<RemoteAppInfo>& a){ apps = a; });
    brls::Logger::info("[SessionAppSelect] getAppList devolvió {} apps", apps.size());
        for (const auto& app : apps) {
            brls::Logger::info("[SessionAppSelect] App recibida: id='{}', name='{}', iconUrl='{}'", app.id, app.name, app.iconUrl);
        }
        brls::sync([this, apps]() {
            if (spinner) spinner->setVisibility(brls::Visibility::GONE);
            if (apps.empty()) {
                brls::Logger::info("[SessionAppSelect] No se encontraron aplicaciones en este host.");
                if (app_select_empty) {
                    app_select_empty->setText(brls::getStr("moonlight/session/app_select/no_apps"));
                    app_select_empty->setVisibility(brls::Visibility::VISIBLE);
                }
                return;
            }

            std::vector<std::string> appNames;
            std::vector<std::string> appIcons;
            for (const auto& app : apps) {
                appNames.push_back(app.name);
                appIcons.push_back("img/moonlight/pc.png");  // Icono genérico por ahora
            }

            if (gridView) {
                gridView->setItems(appNames, appIcons);
                gridView->setOnItemSelect([this, apps](int index) {
                    if (index >= 0 && index < (int)apps.size()) {
                        this->AppSelected(apps[index]);
                    }
                });
                gridView->setVisibility(brls::Visibility::VISIBLE);

                // Dar foco al primer elemento después de un breve delay
                brls::async([this]() {
                    brls::sync([this]() {
                        // Intentar dar foco al GridView primero
                        if (gridView) {
                            brls::Application::giveFocus(gridView);
                            brls::Logger::info("[SessionAppSelect] Foco dado al GridView");
                        }
                    });
                });
            }
        }); });
}

void SessionAppSelect::AppSelected(const RemoteAppInfo& app, bool forceStart)
{
    brls::Logger::info("App seleccionada: {} (ID: {})", app.name, app.id);

    // Preparar configuración de streaming para PS Vita
    STREAM_CONFIGURATION streamConfig;
    memset(&streamConfig, 0, sizeof(streamConfig));

    // Cargar configuración del usuario
    ConfigManager configManager;
    if (!configManager.load())
    {
        brls::Logger::warning("[SessionAppSelect] No se pudo cargar configuración, usando valores por defecto");
    }

    StreamConfiguration streamSettings = configManager.getStreamConfig();
    VideoSettings videoSettings        = configManager.getVideoSettings();

    // Debug: mostrar valores leídos de configuración
    brls::Logger::info("[SessionAppSelect] Configuración leída:");
    brls::Logger::info("[SessionAppSelect] - Stream: {}x{} @ {}fps, bitrate={}",
        streamSettings.width, streamSettings.height, streamSettings.fps, streamSettings.bitrate);
    brls::Logger::info("[SessionAppSelect] - Video: render_mode={}", videoSettings.render_mode);

    // FORCE 960x544 stream for PS Vita (optimal quality)
    // Settings resolution values are saved for HOST monitor control only
    // Sunshine will downscale from displayWidth x displayHeight to 960x544 with high quality
    streamConfig.width  = VITA_STREAM_WIDTH;
    streamConfig.height = VITA_STREAM_HEIGHT;
    streamConfig.fps    = streamSettings.fps > 0 ? streamSettings.fps : VITA_STREAM_DEFAULT_FPS;

    // Save display resolution hint for passing to gs_start_app
    // If width/height from settings is 0 (Auto), Sunshine uses current host display
    // Otherwise, Sunshine will change host display to these dimensions before streaming
    int displayWidth  = streamSettings.width; // Host monitor width
    int displayHeight = streamSettings.height; // Host monitor height

    // Calcular bitrate: si es automático (-1), usar fórmula basada en resolución y fps
    if (streamSettings.bitrate == -1)
    {
        // Fórmula aproximada: (ancho * alto * fps * bits_per_pixel) / 1000000 para Mbps
        // Usando VITA_STREAM_BITS_PER_PIXEL bits por pixel como aproximación conservadora para H.264
        // Calcular paso a paso para evitar pérdida de precisión
        long long total_pixels          = (long long)streamConfig.width * streamConfig.height * streamConfig.fps;
        long long total_bits_per_second = (total_pixels * (long long)(VITA_STREAM_BITS_PER_PIXEL * 10)) / 10;
        int calculatedBitrate           = (int)(total_bits_per_second / 1000); // Convertir a Kbps directamente
        // Limitar a un rango razonable usando constantes globales
        streamConfig.bitrate = std::max(VITA_STREAM_MIN_BITRATE, std::min(VITA_STREAM_MAX_BITRATE, calculatedBitrate));
    }
    else
    {
        streamConfig.bitrate = streamSettings.bitrate > 0 ? streamSettings.bitrate : VITA_STREAM_DEFAULT_BITRATE;
    }

    streamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    streamConfig.encryptionFlags    = ENCFLG_NONE; // Sin encriptación por ahora

    brls::Logger::info("[SessionAppSelect] Configuración de streaming:");
    brls::Logger::info("[SessionAppSelect] - Resolución: {}x{}", streamConfig.width, streamConfig.height);
    brls::Logger::info("[SessionAppSelect] - FPS: {}", streamConfig.fps);
    brls::Logger::info("[SessionAppSelect] - Bitrate: {} Kbps", streamConfig.bitrate);

    // Asegurar packetSize y flags correctos si no han sido establecidos
    if (streamConfig.packetSize <= 0)
    {
        streamConfig.packetSize = 1024; // valor seguro por defecto
        brls::Logger::info("[SessionAppSelect] packetSize no definido -> usando 1024");
    }
    if (streamConfig.streamingRemotely == 0 && streamConfig.streamingRemotely != STREAM_CFG_LOCAL && streamConfig.streamingRemotely != STREAM_CFG_REMOTE && streamConfig.streamingRemotely != STREAM_CFG_AUTO)
    {
        streamConfig.streamingRemotely = STREAM_CFG_AUTO; // dejar al core decidir
        brls::Logger::info("[SessionAppSelect] streamingRemotely no definido -> AUTO");
    }
    if (streamConfig.supportedVideoFormats == 0)
    {
#ifdef VIDEO_FORMAT_H264
        streamConfig.supportedVideoFormats = VIDEO_FORMAT_H264;
        brls::Logger::info("[SessionAppSelect] supportedVideoFormats vacío -> set H264");
#endif
    }
    brls::Logger::info("[SessionAppSelect] - packetSize: {} streamingRemotely={} formats=0x{:X}",
        streamConfig.packetSize, streamConfig.streamingRemotely, streamConfig.supportedVideoFormats);

    // Inicializar servidor usando GameStreamClient (patrón Moonlight-Switch)

    // Mostrar diálogo modal con spinner indicando el progreso de conexión
    brls::Visibility prevGridVis = brls::Visibility::GONE;
    std::string connectingMsg    = brls::getStr("moonlight/session/app_select/connecting");
    auto* loadingDialog          = this->showConnectingDialog(connectingMsg, prevGridVis);

    // Ejecutar la secuencia de conexión e inicio en un hilo de fondo
    // Capturamos por valor los datos necesarios para evitar uso de `this` en background
    HostInfo hostCopy            = this->host;
    RemoteAppInfo appCopy        = app;
    STREAM_CONFIGURATION cfgCopy = streamConfig;
    int displayW                 = displayWidth;
    int displayH                 = displayHeight;
    std::thread([hostCopy, appCopy, cfgCopy, displayW, displayH, loadingDialog, this, prevGridVis, forceStart]() mutable
        {
        brls::Logger::info("[SessionAppSelect][async] Iniciando conexión en hilo de fondo para {}", hostCopy.ip);
        bool connected = GameStreamClient::instance().connect(hostCopy);
        // Actualizar UI en hilo principal
    brls::sync([connected, hostCopy, appCopy, cfgCopy, displayW, displayH, loadingDialog, prevGridVis, forceStart, this]() mutable {
            // Nota: no cerramos inmediatamente el diálogo de 'connecting' si
            // estamos conectados — lo reutilizaremos cambiando su texto a
            // 'starting'. Solo cerramos si la conexión falló.
            if (!connected) {
                // Cerrar diálogo de 'connecting' y restaurar UI en caso de fallo
                if (loadingDialog) { loadingDialog->close(); loadingDialog = nullptr; }
                // Restaurar inputs y estado antes de notificar (helper)
                this->restoreGridViewAndInputs(prevGridVis);
                brls::Logger::error("[SessionAppSelect] Error al conectar con el servidor");
                brls::Application::notify(brls::getStr("moonlight/session/app_select/error_connect"));
                return;
            }

            // Si el host no está emparejado, no abortamos el inicio aquí. Antes
            // cerrábamos el diálogo y volvíamos a la lista; eso impedía lanzar
            // apps nuevas. Ahora registramos el estado y continuamos para
            // intentar startApp (el servidor decidirá si permite el lanzamiento).
            if (!forceStart && !GameStreamClient::instance().isPaired(hostCopy.ip)) {
                brls::Logger::info("[SessionAppSelect] Host %s no emparejado, procediendo a intentar inicio (forceStart=%d)", hostCopy.ip.c_str(), forceStart ? 1 : 0);
            }

            // Mantener el diálogo de 'connecting' tal cual y proceder a iniciar
            // la aplicación. El cambio explícito a 'starting' era redundante
            // (mostramos ya "connecting" y luego arrancó correctamente) y
            // provoca código adicional y posibilidad de condiciones de carrera
            // al intentar actualizar texto de un diálogo que pudo haberse
            // dismiss() y eliminado. Conservamos el diálogo existente y
            // continuamos con startApp.

            // Ejecutar startApp y VitaSession en un hilo para no bloquear UI
            std::thread([hostCopy, appCopy, cfgCopy, displayW, displayH, loadingDialog, this, prevGridVis, forceStart]() mutable {
                bool started = false;
                if (forceStart) {
                    // Si se forzó (Resume), intentar resume explícito
                    started = GameStreamClient::instance().startApp(hostCopy.ip, cfgCopy, std::stoi(appCopy.id), GameStreamClient::StartMode::RESUME_ONLY, displayW, displayH);
                } else {
                    // Normal: permitir auto behavior (resume o launch según servidor)
                    started = GameStreamClient::instance().startApp(hostCopy.ip, cfgCopy, std::stoi(appCopy.id), GameStreamClient::StartMode::AUTO, displayW, displayH);
                }
                brls::sync([started, hostCopy, appCopy, loadingDialog, this, prevGridVis]() {
                    if (loadingDialog) loadingDialog->close();
                    // Restaurar inputs y GridView uniformemente usando helper
                    this->restoreGridViewAndInputs(prevGridVis);
                    if (!started) {
                        brls::Logger::error("[SessionAppSelect] Error al iniciar aplicación");
                        brls::Application::notify(brls::getStr("moonlight/session/app_select/error_start_app"));
                        return;
                    }

                    // Crear y arrancar VitaSession
                    SERVER_DATA& serverData = GameStreamClient::instance().serverData(hostCopy.ip);
                    int appId = std::stoi(appCopy.id);
                    bool isSunshine = false;
                    if (serverData.serverInfo.serverInfoAppVersion && std::string(serverData.serverInfo.serverInfoAppVersion).find("Sunshine") != std::string::npos)
                        isSunshine = true;
                    else if (serverData.serverInfo.serverCodecModeSupport != 0)
                        isSunshine = true;

                    auto* vitaSession = new VitaSession(hostCopy.ip, appId, isSunshine);
                    if (!vitaSession->start()) {
                        brls::Logger::error("[SessionAppSelect] VitaSession start() falló");
                        brls::Application::notify(brls::getStr("moonlight/session/app_select/error_start_stream"));
                        delete vitaSession;
                        return;
                    }

                    GameStreamClient::instance().setActiveStream(hostCopy.ip, appId, appCopy.name);
                    auto* sessionView = new SessionMainView(hostCopy, appCopy);
                    brls::Application::pushActivity(new brls::Activity(sessionView), brls::TransitionAnimation::NONE);
                });
            }).detach();
        }); })
        .detach();
    return;
}
