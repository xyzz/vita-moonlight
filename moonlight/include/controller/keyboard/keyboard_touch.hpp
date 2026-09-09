#pragma once

#include <borealis.hpp>
#include <functional>
#include <utility>

class KeyboardTouchRecognizer final : public brls::GestureRecognizer
{
  public:
    using Callback = std::function<bool(brls::GestureState, const brls::Point&)>;

    explicit KeyboardTouchRecognizer(Callback callback)
        : callback(std::move(callback))
    {
    }

    brls::GestureState recognitionLoop(
        brls::TouchState touch, brls::MouseState mouse, brls::View*, brls::Sound*) override
    {
        brls::TouchPhase phase = touch.phase;
        brls::Point position   = touch.position;
        if (phase == brls::TouchPhase::NONE)
        {
            phase    = mouse.leftButton;
            position = mouse.position;
        }

        if (!enabled || phase == brls::TouchPhase::NONE)
        {
            if (tracking)
            {
                callback(brls::GestureState::INTERRUPTED, position);
            }
            tracking = false;
            state    = brls::GestureState::FAILED;
            return state;
        }

        if (phase == brls::TouchPhase::START)
        {
            tracking = callback(brls::GestureState::START, position);
            state    = tracking ? brls::GestureState::START : brls::GestureState::FAILED;
            return state;
        }

        if (!tracking)
        {
            state = brls::GestureState::FAILED;
            return state;
        }

        state = phase == brls::TouchPhase::END ? brls::GestureState::END : brls::GestureState::STAY;
        callback(state, position);
        if (phase == brls::TouchPhase::END)
        {
            tracking = false;
        }
        return state;
    }

  private:
    Callback callback;
    bool tracking = false;
};
