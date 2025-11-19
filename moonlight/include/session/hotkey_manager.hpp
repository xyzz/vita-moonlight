#pragma once
#include <chrono>
#include <functional>
#include <vector>

#include "borealis.hpp"

// HotkeyManager: gestiona combinaciones de botones para acciones globales (ej: abrir overlay de pausa).
// Diseño inicial: hardcode START+L+R; luego se ampliará a configuración dinámica y persistencia.
// Uso previsto: instancia singleton ligera; registrar callback para "pauseCombo".

class HotkeyManager
{
  public:
    static HotkeyManager& instance();

    // Registra callback que se dispara cuando se detecta la combinación de pausa.
    void setPauseCallback(const std::function<void()>& cb) { pauseCallback = cb; }

    // Llamado desde un hook central de input (por ahora se invocará manualmente desde vistas).
    void onButtonEvent(brls::ControllerButton button, bool pressed);

    // Config futura: establecer ventana máxima ms para detección simultánea (default 220ms)
    void setComboWindowMs(int ms) { comboWindowMs = ms; }

  private:
    HotkeyManager()                                = default;
    HotkeyManager(const HotkeyManager&)            = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    std::function<void()> pauseCallback;
    std::function<void()> menuCallback;

    // Estado de botones relevantes
    bool btnStart = false;
    bool btnL     = false;
    bool btnR     = false;

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastStart {};
    Clock::time_point lastL {};
    Clock::time_point lastR {};

    int comboWindowMs = 220; // ventana para considerar combinación

    void tryTrigger();
};
