#pragma once

#include <cstring>

struct KeyboardState
{
    bool keys[256];

    KeyboardState()
    {
        memset(keys, 0, sizeof(keys));
    }
};

class IKeyboard
{
  public:
    virtual ~IKeyboard() = default;

    virtual void open() = 0;

    virtual void close() = 0;

    virtual bool isOpen() const = 0;

    // Called every frame from the main thread (ControllerInputManager::handleInput).
    // Legacy keyboard uses this to call sceImeUpdate().
    // Modern keyboard does nothing here (it draws via Borealis).
    virtual void update() { }

    virtual KeyboardState getKeyboardState() const
    {
        KeyboardState s;
        return s;
    }

    virtual bool sendsDirectly() const { return false; }

    virtual bool usesNonNormalizedVk() const { return false; }

    virtual bool selfDestructs() const { return false; }
};
