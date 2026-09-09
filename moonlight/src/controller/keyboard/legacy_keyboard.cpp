#include "controller/keyboard/legacy_keyboard.hpp"

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
#include <psp2/ime_dialog.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/libime.h>
#include <psp2/sysmodule.h>
#endif

#include <borealis/extern/nanovg/nanovg.h>
#include <string.h>

#include <borealis.hpp>
#include <cstdint>
#include <deque>

#include "ConfigManager.hpp"
#include "Limelight.h"
#include "controller/keyboard/keyboard_focus.hpp"
#include "controller/keyboard/keyboard_utf8.hpp"
#include "controller/keyboard/keyboardloader.hpp"
#include "debug.hpp"
#include "session/vita_session.hpp"
#include "video/VitaVideoRenderer.hpp"

namespace
{
constexpr int LEGACY_IME_VIEWPORT_HEIGHT = 294;
constexpr float FOCUS_BUTTON_SIZE        = 44.0f;
constexpr float FOCUS_BUTTON_MARGIN      = 8.0f;
}

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)

static KeyboardLayout g_legacy_keyboard_layout = KB_LAYOUT_EN_US;
static bool g_legacy_force_utf8                = false;

static constexpr SceUInt32 IME_VIRTUAL_TEXT_LENGTH  = 3;
static constexpr SceUInt32 IME_MAX_TEXT_LENGTH      = 4;
static constexpr SceUInt32 IME_OUTPUT_BUFFER_LENGTH = SCE_IME_MAX_PREEDIT_LENGTH + IME_MAX_TEXT_LENGTH + 1;
static uint8_t g_ime_work[SCE_IME_WORK_BUFFER_SIZE] __attribute__((aligned(16)));
static SceWChar16 g_ime_initial[IME_VIRTUAL_TEXT_LENGTH + 1] __attribute__((aligned(4))) = { 1, 1, 1, 0 };
static SceWChar16 g_ime_empty_initial[1] __attribute__((aligned(4)))                     = { 0 };
static SceWChar16 g_ime_output[IME_OUTPUT_BUFFER_LENGTH] __attribute__((aligned(4)))     = { 0 };
static SceImeCaret g_ime_caret;
static std::atomic<bool> g_ime_close_requested { false };
static std::atomic<bool> g_ime_just_opened { false };
static std::atomic<std::uint32_t> g_pending_events { 0 };
static std::atomic<int> g_ime_viewport_height { LEGACY_IME_VIEWPORT_HEIGHT };
static std::atomic<bool> g_ime_dialog_active { false };
static std::mutex g_pending_chars_mutex;
static std::deque<std::uint32_t> g_pending_chars;

enum PendingImeEvent : std::uint32_t
{
    PendingEventNone             = 0,
    PendingEventBackspace        = 1u << 0,
    PendingEventEnter            = 1u << 1,
    PendingEventResetVirtualText = 1u << 2,
    PendingEventLeft             = 1u << 3,
    PendingEventRight            = 1u << 4,
};

static void send_vk_event(short keyCode, char action)
{
    LiSendKeyboardEvent2(keyCode, action, 0, SS_KBE_FLAG_NON_NORMALIZED);
}

static void send_char_as_keypress(wchar_t ch)
{
    if (ch == 0)
    {
        return;
    }

    if (ch == '\n' || ch == '\r')
    {
        send_vk_event(0x0D, KEY_ACTION_DOWN);
        send_vk_event(0x0D, KEY_ACTION_UP);
        return;
    }
    if (ch == '\t')
    {
        send_vk_event(0x09, KEY_ACTION_DOWN);
        send_vk_event(0x09, KEY_ACTION_UP);
        return;
    }
    if (ch == ' ')
    {
        send_vk_event(0x20, KEY_ACTION_DOWN);
        send_vk_event(0x20, KEY_ACTION_UP);
        return;
    }

    // Try VK code mapping first for better Linux compatibility
    // (Sunshine on Linux uses an unreliable Ctrl+Shift+U hack for UTF-8 text)
    // If force UTF-8 is enabled (for Windows hosts), skip VK mapping
    if (!g_legacy_force_utf8)
    {
        VkMapping mapping;
        if (lookup_vk_mapping(g_legacy_keyboard_layout, static_cast<std::uint32_t>(ch), mapping))
        {
            if (mapping.needs_shift)
            {
                send_vk_event(0x10, KEY_ACTION_DOWN);
            }
            if (mapping.needs_altgr)
            {
                send_vk_event(0x11, KEY_ACTION_DOWN); // Ctrl
                send_vk_event(0x12, KEY_ACTION_DOWN); // Alt
            }
            send_vk_event(mapping.vk, KEY_ACTION_DOWN);
            send_vk_event(mapping.vk, KEY_ACTION_UP);
            if (mapping.needs_altgr)
            {
                send_vk_event(0x12, KEY_ACTION_UP); // Alt
                send_vk_event(0x11, KEY_ACTION_UP); // Ctrl
            }
            if (mapping.needs_shift)
            {
                send_vk_event(0x10, KEY_ACTION_UP);
            }
            return;
        }
    }

    // Fall back to UTF-8 text input (works well on Windows Sunshine)
    if (!send_utf8_codepoint(static_cast<std::uint32_t>(ch)))
    {
        vita_log::error("[LegacyKB] UTF-8 text send failed for U+%04X", (unsigned)ch);
    }
}

static void send_backspace_keypress()
{
    send_vk_event(0x08, KEY_ACTION_DOWN);
    send_vk_event(0x08, KEY_ACTION_UP);
}

static void send_dialog_text(const std::string& text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        std::size_t length       = 1;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
        }

        if (offset + length > text.size())
        {
            break;
        }

        std::uint32_t codepoint = 0;
        if (decode_single_utf8(text.substr(offset, length), codepoint))
        {
            send_char_as_keypress(static_cast<wchar_t>(codepoint));
            offset += length;
        }
        else
        {
            ++offset;
        }
    }
}

static bool reset_ime_virtual_text()
{
    sceClibMemset(g_ime_output, 0, sizeof(g_ime_output));
    for (SceUInt32 i = 0; i < IME_VIRTUAL_TEXT_LENGTH; ++i)
    {
        g_ime_output[i] = 1;
    }
    sceClibMemset(&g_ime_caret, 0, sizeof(g_ime_caret));
    g_ime_caret.index = 1;

    const int textRes  = sceImeSetText(g_ime_initial, IME_VIRTUAL_TEXT_LENGTH);
    const int caretRes = sceImeSetCaret(&g_ime_caret);
    if (textRes < 0 || caretRes < 0)
    {
        vita_log::error("[LegacyKB] IME reset failed: text=0x%08X caret=0x%08X", textRes, caretRes);
        return false;
    }

    return true;
}

static wchar_t extract_committed_char()
{
    for (SceUInt32 i = 0; i < IME_OUTPUT_BUFFER_LENGTH; ++i)
    {
        SceWChar16 ch = g_ime_output[i];
        if (ch != 0 && ch != 1)
        {
            return (wchar_t)ch;
        }
    }
    return 0;
}

static void legacykb_ime_event_handler(void* /*arg*/, const SceImeEventData* e)
{
    if (!e)
    {
        return;
    }

    int caretIndexLog = -1;
    if (e->id == SCE_IME_EVENT_UPDATE_TEXT)
    {
        caretIndexLog = static_cast<int>(e->param.text.caretIndex);
    }
    else if (e->id == SCE_IME_EVENT_UPDATE_CARET)
    {
        caretIndexLog = static_cast<int>(e->param.caretIndex);
    }
    vita_log::info(
        "[LegacyKB][IME] id=%d, caretIndex=%d, buffer=[%04X %04X %04X %04X]",
        static_cast<int>(e->id),
        caretIndexLog,
        static_cast<unsigned>(g_ime_output[0]),
        static_cast<unsigned>(g_ime_output[1]),
        static_cast<unsigned>(g_ime_output[2]),
        static_cast<unsigned>(g_ime_output[3]));

    switch (e->id)
    {
        case SCE_IME_EVENT_CHANGE_SIZE:
        {
            const int visibleHeight = static_cast<int>(e->param.rect.y);
            if (visibleHeight > 0 && visibleHeight < 544)
            {
                g_ime_viewport_height.store(visibleHeight, std::memory_order_release);
            }
            vita_log::info("[LegacyKB][IME] rect=%ux%u at %u,%u viewport=%d",
                e->param.rect.width, e->param.rect.height, e->param.rect.x, e->param.rect.y,
                g_ime_viewport_height.load(std::memory_order_acquire));
            break;
        }
        case SCE_IME_EVENT_UPDATE_TEXT:
        {
            const int caretIndex = e->param.text.caretIndex;
            wchar_t ch           = extract_committed_char();

            if (g_ime_just_opened.load(std::memory_order_acquire) && caretIndex == 0 && ch == 0)
            {
                g_ime_just_opened.store(false, std::memory_order_release);
                g_pending_events.fetch_or(PendingEventResetVirtualText, std::memory_order_acq_rel);
                break;
            }

            g_ime_just_opened.store(false, std::memory_order_release);

            if (caretIndex != 1)
            {
                if (ch == 0 || ch == 0x08 || ch == 0x7F)
                {
                    g_pending_events.fetch_or(PendingEventBackspace | PendingEventResetVirtualText, std::memory_order_acq_rel);
                    break;
                }
            }

            if (ch != 0)
            {
                std::lock_guard<std::mutex> lock(g_pending_chars_mutex);
                g_pending_chars.push_back(static_cast<std::uint32_t>(ch));
            }
            g_pending_events.fetch_or(PendingEventResetVirtualText, std::memory_order_acq_rel);
            break;
        }
        case SCE_IME_EVENT_UPDATE_CARET:
        {
            const int caretIndex = e->param.caretIndex;
            if (caretIndex == 0)
            {
                g_pending_events.fetch_or(PendingEventLeft | PendingEventResetVirtualText, std::memory_order_acq_rel);
            }
            else if (caretIndex == 2)
            {
                g_pending_events.fetch_or(PendingEventRight | PendingEventResetVirtualText, std::memory_order_acq_rel);
            }
            break;
        }
        case SCE_IME_EVENT_PRESS_ENTER:
            g_pending_events.fetch_or(PendingEventEnter | PendingEventResetVirtualText, std::memory_order_acq_rel);
            break;
        case SCE_IME_EVENT_PRESS_CLOSE:
            g_ime_close_requested.store(true, std::memory_order_release);
            break;
        default:
            break;
    }
}

#endif // __PSV__

LegacyKeyboard::LegacyKeyboard() { }

LegacyKeyboard::~LegacyKeyboard()
{
    close();
}

void LegacyKeyboard::open()
{
    if (isOpenFlag.load(std::memory_order_acquire) || imeThread.joinable())
    {
        vita_log::info("[LegacyKB] Already open, ignoring");
        return;
    }

#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    {
        ConfigManager config;
        config.load();
        VideoSettings vs         = config.getVideoSettings();
        g_legacy_keyboard_layout = static_cast<KeyboardLayout>(vs.keyboard_layout);
        g_legacy_force_utf8      = vs.keyboard_input_mode;
    }
    stopRequested.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(startMutex);
        startFinished  = false;
        startSucceeded = false;
    }

    imeThread = std::thread(&LegacyKeyboard::runIme, this);

    std::unique_lock<std::mutex> lock(startMutex);
    startCondition.wait(lock, [this]()
        { return startFinished; });
    const bool opened = startSucceeded;
    lock.unlock();

    if (!opened && imeThread.joinable())
    {
        imeThread.join();
    }
    else if (opened)
    {
        showBottomVideo    = false;
        focusButtonPressed = false;
        manualVideoFocus   = false;
        viewportHeight     = g_ime_viewport_height.load(std::memory_order_acquire);
        updateVideoViewport();
        request_keyboard_typing_focus([this](float normalizedY)
            {
            if (!isOpenFlag.load(std::memory_order_acquire) || manualVideoFocus) {
                return;
            }
            showBottomVideo = normalizedY * 544.0f > static_cast<float>(viewportHeight);
            updateVideoViewport(); });
    }
#else
    vita_log::info("[LegacyKB] IME not available on this platform");
#endif
}

void LegacyKeyboard::close()
{
    cancel_keyboard_typing_focus();
    VitaVideoRenderer::instance().clearKeyboardViewport();
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    stopRequested.store(true, std::memory_order_release);
    if (imeThread.joinable())
    {
        imeThread.join();
    }
#endif

    g_ime_close_requested.store(false, std::memory_order_release);
    g_ime_just_opened.store(false, std::memory_order_release);
    isOpenFlag.store(false, std::memory_order_release);
}

bool LegacyKeyboard::isOpen() const
{
    return isOpenFlag.load(std::memory_order_acquire);
}

bool LegacyKeyboard::allowsStreamTouch() const
{
    return isOpenFlag.load(std::memory_order_acquire) && viewportHeight > 0;
}

int LegacyKeyboard::streamTouchHeight() const
{
    return allowsStreamTouch() ? viewportHeight : 0;
}

bool LegacyKeyboard::blocksStreamTouch() const
{
    return focusButtonPressed;
}

void LegacyKeyboard::update()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    if (!isOpenFlag.load(std::memory_order_acquire))
    {
        return;
    }

    const int reportedHeight = g_ime_viewport_height.load(std::memory_order_acquire);
    if (reportedHeight != viewportHeight)
    {
        viewportHeight = reportedHeight;
        updateVideoViewport();
        if (!manualVideoFocus)
        {
            request_keyboard_typing_focus([this](float normalizedY)
                {
                if (!isOpenFlag.load(std::memory_order_acquire) || manualVideoFocus) {
                    return;
                }
                showBottomVideo = normalizedY * 544.0f > static_cast<float>(viewportHeight);
                updateVideoViewport(); });
        }
    }

#endif
}

void LegacyKeyboard::drawOverlay(NVGcontext* vg)
{
    if (!vg || !isOpenFlag.load(std::memory_order_acquire))
    {
        return;
    }

    float buttonX = 0.0f;
    float buttonY = 0.0f;
    focusButtonPosition(buttonX, buttonY);

    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, buttonX, buttonY, FOCUS_BUTTON_SIZE, FOCUS_BUTTON_SIZE, 6.0f);
    nvgFillColor(vg, nvgRGBA(45, 105, 190, 235));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(230, 240, 255, 255));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    nvgFontFace(vg, "regular");
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, buttonX + FOCUS_BUTTON_SIZE * 0.5f, buttonY + FOCUS_BUTTON_SIZE * 0.5f,
        showBottomVideo ? "^" : "v", nullptr);
    nvgRestore(vg);
}

bool LegacyKeyboard::beginFocusButtonTouch(float x, float y)
{
    if (!isOpenFlag.load(std::memory_order_acquire))
    {
        return false;
    }

    float buttonX = 0.0f;
    float buttonY = 0.0f;
    focusButtonPosition(buttonX, buttonY);
    if (x < buttonX || x > buttonX + FOCUS_BUTTON_SIZE || y < buttonY || y > buttonY + FOCUS_BUTTON_SIZE)
    {
        return false;
    }

    focusButtonPressed = true;
    toggleVideoFocus();
    return true;
}

void LegacyKeyboard::endFocusButtonTouch()
{
    focusButtonPressed = false;
}

void LegacyKeyboard::toggleVideoFocus()
{
    manualVideoFocus = true;
    showBottomVideo  = !showBottomVideo;
    cancel_keyboard_typing_focus();
    updateVideoViewport();
    vita_log::info("[LegacyKB] Manual video view=%s viewport=%d",
        showBottomVideo ? "bottom" : "top", viewportHeight);
}

void LegacyKeyboard::updateVideoViewport()
{
    VitaVideoRenderer::instance().setKeyboardViewport(viewportHeight, showBottomVideo);
}

void LegacyKeyboard::focusButtonPosition(float& x, float& y) const
{
    const float scale       = brls::Application::windowScale > 0.0f ? brls::Application::windowScale : 1.0f;
    const float viewportTop = static_cast<float>(viewportHeight) / scale;
    x                       = brls::Application::contentWidth - FOCUS_BUTTON_SIZE - FOCUS_BUTTON_MARGIN;
    y                       = viewportTop - FOCUS_BUTTON_SIZE - FOCUS_BUTTON_MARGIN;
}

void LegacyKeyboard::runIme()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    g_ime_close_requested.store(false, std::memory_order_release);
    g_ime_just_opened.store(true, std::memory_order_release);
    g_ime_viewport_height.store(LEGACY_IME_VIEWPORT_HEIGHT, std::memory_order_release);
    g_pending_events.store(PendingEventNone, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_pending_chars_mutex);
        g_pending_chars.clear();
    }

    const int loadRes = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    vita_log::info("[LegacyKB] IME module load: 0x%08X", loadRes);

    SceImeParam param;
    sceImeParamInit(&param);

    switch (g_legacy_keyboard_layout)
    {
        case KB_LAYOUT_EN_US:
            param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
            break;
        case KB_LAYOUT_ES_ES:
        case KB_LAYOUT_ES_LATAM:
            param.supportedLanguages = SCE_IME_LANGUAGE_SPANISH;
            break;
        default:
            param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
            break;
    }

    param.languagesForced = SCE_TRUE;
    param.type            = SCE_IME_TYPE_DEFAULT;
    param.option          = SCE_IME_OPTION_NO_ASSISTANCE;
    param.work            = g_ime_work;
    param.handler         = legacykb_ime_event_handler;
    param.initialText     = g_ime_empty_initial;
    param.maxTextLength   = IME_MAX_TEXT_LENGTH;
    param.inputTextBuffer = g_ime_output;
    param.enterLabel      = SCE_IME_ENTER_LABEL_DEFAULT;
    param.arg             = nullptr;

    vita_log::info(
        "[LegacyKB] Opening IME sdk=0x%08X language=0x%llX forced=%d max=%u",
        param.sdkVersion,
        static_cast<unsigned long long>(param.supportedLanguages),
        static_cast<int>(param.languagesForced),
        param.maxTextLength);

    int openRes = sceImeOpen(&param);
    if (openRes == static_cast<int>(SCE_IME_ERROR_INVALID_PARAM))
    {
        vita_log::info("[LegacyKB] Retrying IME with system language");
        param.supportedLanguages = 0;
        param.languagesForced    = SCE_FALSE;
        openRes                  = sceImeOpen(&param);
    }
    if (openRes < 0)
    {
        {
            std::lock_guard<std::mutex> lock(startMutex);
            startSucceeded = false;
            startFinished  = true;
        }
        startCondition.notify_one();
        vita_log::error("[LegacyKB] sceImeOpen failed: 0x%08X", openRes);
        return;
    }

    if (!reset_ime_virtual_text())
    {
        const int closeRes = sceImeClose();
        {
            std::lock_guard<std::mutex> lock(startMutex);
            startSucceeded = false;
            startFinished  = true;
        }
        startCondition.notify_one();
        vita_log::error("[LegacyKB] IME initialization failed (close=0x%08X)", closeRes);
        return;
    }
    isOpenFlag.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(startMutex);
        startSucceeded = true;
        startFinished  = true;
    }
    startCondition.notify_one();
    vita_log::info("[LegacyKB] IME opened");

    int updateRes = 0;
    while (!stopRequested.load(std::memory_order_acquire))
    {
        updateRes = sceImeUpdate();

        if (updateRes < 0)
        {
            break;
        }

        const std::uint32_t pendingEvents = g_pending_events.exchange(PendingEventNone, std::memory_order_acq_rel);
        std::deque<std::uint32_t> pendingChars;
        {
            std::lock_guard<std::mutex> lock(g_pending_chars_mutex);
            pendingChars.swap(g_pending_chars);
        }

        if ((pendingEvents & PendingEventBackspace) != 0u)
        {
            send_backspace_keypress();
        }
        if ((pendingEvents & PendingEventEnter) != 0u)
        {
            send_vk_event(0x0D, KEY_ACTION_DOWN);
            send_vk_event(0x0D, KEY_ACTION_UP);
        }
        if ((pendingEvents & PendingEventLeft) != 0u)
        {
            send_vk_event(0x25, KEY_ACTION_DOWN);
            send_vk_event(0x25, KEY_ACTION_UP);
        }
        if ((pendingEvents & PendingEventRight) != 0u)
        {
            send_vk_event(0x27, KEY_ACTION_DOWN);
            send_vk_event(0x27, KEY_ACTION_UP);
        }
        for (std::uint32_t pendingChar : pendingChars)
        {
            send_char_as_keypress(static_cast<wchar_t>(pendingChar));
        }
        if ((pendingEvents & PendingEventResetVirtualText) != 0u)
        {
            if (!reset_ime_virtual_text())
            {
                updateRes = SCE_IME_ERROR_INVALID_TEXT;
                break;
            }
        }

        if (g_ime_close_requested.load(std::memory_order_acquire))
        {
            break;
        }

        sceKernelDelayThread(1000);
    }

    const int closeRes = sceImeClose();
    if (closeRes < 0 && closeRes != static_cast<int>(SCE_IME_ERROR_NOT_OPENED))
    {
        vita_log::error("[LegacyKB] sceImeClose failed: 0x%08X", closeRes);
    }
    isOpenFlag.store(false, std::memory_order_release);
    vita_log::info("[LegacyKB] IME closed (status=0x%08X)", updateRes);
#endif
}

bool open_legacy_dialog_keyboard()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    ConfigManager config;
    config.load();
    const VideoSettings settings = config.getVideoSettings();
    g_legacy_keyboard_layout     = static_cast<KeyboardLayout>(settings.keyboard_layout);
    g_legacy_force_utf8          = settings.keyboard_input_mode;

    auto* ime = brls::Application::getImeManager();
    if (!ime)
    {
        return false;
    }

    const bool opened = ime->openForText(
        [](std::string text)
        {
            VitaSession* session = VitaSession::active();
            if (session && session->isActive())
            {
                send_dialog_text(text);
            }
        },
        brls::getStr("moonlight/keyboard/legacy_dialog_prompt"), "", 512);
    g_ime_dialog_active.store(opened, std::memory_order_release);
    return opened;
#else
    return false;
#endif
}

bool is_legacy_dialog_keyboard_active()
{
#if defined(__PSV__) || defined(__psp2__) || defined(__PSP2__)
    if (!g_ime_dialog_active.load(std::memory_order_acquire))
    {
        return false;
    }
    if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_NONE)
    {
        g_ime_dialog_active.store(false, std::memory_order_release);
        return false;
    }
    return true;
#else
    return false;
#endif
}
