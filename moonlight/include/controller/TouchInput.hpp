#pragma once

#include <psp2/touch.h>
#include <stdint.h>

// Enumeración de modos de touch
enum TouchscreenMode
{
    TOUCHSCREEN_MODE_OFF            = 0, // Sin input táctil
    TOUCHSCREEN_MODE_TRACKPAD       = 1, // Trackpad (movimiento relativo)
    TOUCHSCREEN_MODE_DS4_TOUCHPAD   = 2, // DS4 touchpad (solo con PS4 gamepad)
    TOUCHSCREEN_MODE_MOUSE_ABSOLUTE = 3, // Mouse absoluto
    TOUCHSCREEN_MODE_TABLET         = 4 // Tablet/Sunshine
};

// Clase para manejar input táctil en PS Vita
class TouchInputManager
{
  public:
    TouchInputManager();
    ~TouchInputManager();

    // Procesar input táctil basado en modo
    void handleTouch(int touchscreenMode);

    // Resetear estados de touch
    void dropTouch(int touchscreenMode);

    // Validar que el modo sea compatible con el gamepad actual
    bool isModeSupportedByGamepad(int touchscreenMode, int gamepadType);

    // Cambiar modo con validación
    bool setTouchMode(int newMode, int gamepadType);

    // Métodos para cambios instantáneos de configuración del trackpad
    void setTrackpadSettings(int pointerSpeed, int deadZone, bool tapToClick,
        bool twoFingerRightClick, bool twoFingerScroll,
        bool invertScroll, bool multiTouch, int edgeZone);

    // Getters para la configuración actual del trackpad
    int getPointerSpeed() const;
    int getDeadZone() const;
    bool isTapToClickEnabled() const;
    bool isTwoFingerRightClickEnabled() const;
    bool isTwoFingerScrollEnabled() const;
    bool isInvertScrollEnabled() const;
    bool isMultiTouchEnabled() const;
    int getEdgeZone() const;

  private:
    SceTouchData touchData;
    SceTouchData lastTouchData;
    int currentMode;

    // Configuración del trackpad (para cambios instantáneos)
    int trackpadPointerSpeed         = 100; // 0-200
    int trackpadDeadZone             = 50; // 0-200px
    bool trackpadTapToClick          = true;
    bool trackpadTwoFingerRightClick = true;
    bool trackpadTwoFingerScroll     = true;
    bool trackpadInvertScroll        = false;
    bool trackpadMultiTouch          = true;
    int trackpadEdgeZone             = 15; // 0-50%

    // Estado de la máquina de estados del trackpad
    int trackpadState                   = 0;
    int trackpadFingerCount             = 0;
    uint64_t trackpadStateStartTime     = 0;
    SceTouchReport trackpadInitialTouch = { 0 };
    SceTouchReport trackpadSwipeStart   = { 0 };

    // Estados para cada modo
    struct
    {
        bool lastMouseDown;
        int lastAbsX, lastAbsY;
    } trackpadModeState;

    struct
    {
        uint8_t prevFingerActive[10];
        int prevX[10], prevY[10];
    } ds4State;

    struct
    {
        int frontState;
        short fingerCount;
        uint32_t twoFingerStartTime;
        int twoFingerStartY;
        int twoFingerLastY;
        bool twoFingerScroll;
        bool rightClickSent;
    } absoluteState;

    struct
    {
        uint8_t prevFingerActive[10];
    } tabletState;

    // Funciones específicas de cada modo
    void handleTrackpad(); // Modo 1: Trackpad (relative mouse)
    void handleDS4Touch(); // Modo 2: DS4 touchpad (PS4 only)
    void handleMouseAbsolute(); // Modo 3: Mouse absoluto
    void handleTabletTouch(); // Modo 4: Tablet/Sunshine

    // Funciones auxiliares
    void resetTrackpadState();
    void resetDS4State();
    void resetAbsoluteState();
    void resetTabletState();
};

extern TouchInputManager* g_touchInput;
