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

    virtual void open()         = 0;
    virtual void close()        = 0;
    virtual bool isOpen() const = 0;
    virtual void update() { }

    virtual KeyboardState getKeyboardState() const
    {
        KeyboardState s;
        return s;
    }

    virtual bool sendsDirectly() const { return false; }
    virtual bool usesNonNormalizedVk() const { return false; }

    virtual bool allowsStreamTouch() const { return false; }

    virtual int streamTouchHeight() const { return 0; }

    virtual bool selfDestructs() const { return false; }
};
