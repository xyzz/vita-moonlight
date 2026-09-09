#pragma once

#include <functional>

using KeyboardTypingFocusCallback = std::function<void(float)>;

void request_keyboard_typing_focus(KeyboardTypingFocusCallback callback);
void cancel_keyboard_typing_focus();
