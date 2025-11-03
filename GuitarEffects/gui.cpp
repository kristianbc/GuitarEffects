// gui.cpp - WinAPI GUI for AudioProcessor key rebinding and effect sliders
#include <windows.h>
#include <commctrl.h> // For trackbars (sliders)
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include "AudioProcessor.h"
#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib")
#pragma comment(lib, "comctl32.lib")
#include <windowsx.h>

// Effect/action names for keybinds (toggles and special actions only)
const char* actions[] = {
    "Tremolo Toggle", "Chorus Toggle", "Overdrive Toggle", "Reverb Toggle",
    "Warm Toggle", "Blues Toggle", "Compressor Toggle", "Reset All"
};
const int NUM_ACTIONS = sizeof(actions) / sizeof(actions[0]);

// Default key bindings (VK_*)
int defaultKeys[NUM_ACTIONS] = {
    'T', 'C', 'O', 'V', 'W', 'B', 'P', 'R'
};

// XInput button definitions
#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

// Key binding state
enum class InputType { Keyboard, Joystick, Mouse };
struct ActionBinding {
    InputType type;
    int keyOrButton; // VK_* for keyboard, XINPUT_GAMEPAD_* for joystick, mouse button for mouse
};
std::map<int, ActionBinding> keyBindings; // action idx -> binding
std::map<int, HWND> editBoxes;  // action idx -> edit box HWND
int rebindingAction = -1;

AudioProcessor* processor = nullptr;
bool tremoloState = false;
bool chorusState = false;
bool overdriveState = false;
bool reverbState = false;
bool warmState = false;
bool bluesState = false; // Add global bluesState
bool compState = false;

// Effect parameter state
float currentRate = 5.0f;
float currentDepth = 0.5f;
float currentChorusRate = 1.5f;
float currentChorusDepth = 0.02f;
float currentChorusFeedback = 0.3f;
float currentChorusWidth = 0.5f;
float currentMainVolume = 1.0f;
float currentOverdriveDrive = 3.0f;
float currentOverdriveThreshold = 0.3f;
float currentOverdriveTone = 0.5f;
float currentOverdriveMix = 0.8f;
float currentReverbSize = 0.5f;
float currentReverbDamping = 0.5f;
float currentReverbWidth = 1.0f;
float currentReverbMix = 0.3f;
float currentWarmAmount = 0.5f;
float currentWarmTone = 0.5f;
float currentWarmSaturation = 0.5f;
float currentBluesGain = 1.5f;
float currentBluesTone = 0.5f;
float currentBluesLevel = 0.8f;
float currentCompLevel = 1.0f;
float currentCompTone = 0.5f;
float currentCompAttack = 10.0f; // ms
float currentCompSustain = 300.0f; // ms

// Slider IDs
enum {
    SLIDER_TREMOLO_RATE = 3000,
    SLIDER_TREMOLO_DEPTH,
    SLIDER_CHORUS_RATE,
    SLIDER_CHORUS_DEPTH,
    SLIDER_CHORUS_FEEDBACK,
    SLIDER_CHORUS_WIDTH,
    SLIDER_MAIN_VOLUME,
    SLIDER_OVERDRIVE_DRIVE,
    SLIDER_OVERDRIVE_THRESHOLD,
    SLIDER_OVERDRIVE_TONE,
    SLIDER_OVERDRIVE_MIX,
    SLIDER_REVERB_SIZE,
    SLIDER_REVERB_DAMPING,
    SLIDER_REVERB_WIDTH,
    SLIDER_REVERB_MIX,
    SLIDER_WARM_AMOUNT,
    SLIDER_WARM_TONE,
    SLIDER_WARM_SATURATION,
    SLIDER_BLUES_GAIN,
    SLIDER_BLUES_TONE,
    SLIDER_BLUES_LEVEL,
    SLIDER_COMP_LEVEL,
    SLIDER_COMP_TONE,
    SLIDER_COMP_ATTACK,
    SLIDER_COMP_SUSTAIN
};

// Input state tracking
XINPUT_STATE g_prevJoyState = {};
bool g_prevKeyStates[256] = {};
bool g_actionPressed[NUM_ACTIONS] = {}; // Track if action is currently pressed

// Device selector globals
std::vector<AudioDevice> g_devices;
HWND hDeviceCombo = nullptr;

HWND createSlider(HWND parent, int id, int x, int y, int min, int max, int pos) {
    // Use a fixed width for all sliders to ensure proper rendering
    int sliderWidth = 180;
    HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        x, y, sliderWidth, 24, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
    SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(min, max));
    SendMessageW(h, TBM_SETPOS, TRUE, pos);
    return h;
}

// Change handleAction to accept HWND hwnd
void handleAction(int action, HWND hwnd = nullptr) {
    switch (action) {
    case 0: // Tremolo Toggle
        tremoloState = !tremoloState;
        processor->SetTremoloEnabled(tremoloState);
        break;
    case 1: // Chorus Toggle
        chorusState = !chorusState;
        processor->SetChorusEnabled(chorusState);
        break;
    case 2: // Overdrive Toggle
        overdriveState = !overdriveState;
        processor->SetOverdriveEnabled(overdriveState);
        break;
    case 3: // Reverb Toggle
        reverbState = !reverbState;
        processor->SetReverbEnabled(reverbState);
        break;
    case 4: // Warm Toggle
        warmState = !warmState;
        processor->SetWarmEnabled(warmState);
        break;
    case 5: // Blues Toggle
        bluesState = !bluesState;
        processor->SetBluesEnabled(bluesState);
        break;
    case 6: // Compressor Toggle
        compState = !compState;
        processor->SetCompressorEnabled(compState);
        break;
    case 7: // Reset All
        processor->Reset();
        tremoloState = false;
        chorusState = false;
        overdriveState = false;
        reverbState = false;
        warmState = false;
        bluesState = false;
        currentRate = 5.0f;
        currentDepth = 0.5f;
        currentChorusRate = 1.5f;
        currentChorusDepth = 0.02f;
        currentChorusFeedback = 0.3f;
        currentChorusWidth = 0.5f;
        currentMainVolume = 1.0f;
        currentOverdriveDrive = 3.0f;
        currentOverdriveThreshold = 0.3f;
        currentOverdriveTone = 0.5f;
        currentOverdriveMix = 0.8f;
        currentReverbSize = 0.5f;
        currentReverbDamping = 0.5f;
        currentReverbWidth = 1.0f;
        currentReverbMix = 0.3f;
        currentWarmAmount = 0.5f;
        currentWarmTone = 0.5f;
        currentWarmSaturation = 0.5f;
        currentBluesGain = 1.5f;
        currentBluesTone = 0.5f;
        currentBluesLevel = 0.8f;
        currentCompLevel = 1.0f;
        currentCompTone = 0.5f;
        currentCompAttack = 10.0f;
        currentCompSustain = 300.0f;
        // Update all sliders to reflect reset values if hwnd is provided
        if (hwnd) {
            SendMessageW(GetDlgItem(hwnd, SLIDER_TREMOLO_RATE), TBM_SETPOS, TRUE, (int)(currentRate));
            SendMessageW(GetDlgItem(hwnd, SLIDER_TREMOLO_DEPTH), TBM_SETPOS, TRUE, (int)(currentDepth * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_RATE), TBM_SETPOS, TRUE, (int)(currentChorusRate * 10));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_DEPTH), TBM_SETPOS, TRUE, (int)(currentChorusDepth * 1000));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_FEEDBACK), TBM_SETPOS, TRUE, (int)(currentChorusFeedback * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_WIDTH), TBM_SETPOS, TRUE, (int)(currentChorusWidth * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_MAIN_VOLUME), TBM_SETPOS, TRUE, (int)(currentMainVolume * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_OVERDRIVE_DRIVE), TBM_SETPOS, TRUE, (int)(currentOverdriveDrive));
            SendMessageW(GetDlgItem(hwnd, SLIDER_OVERDRIVE_THRESHOLD), TBM_SETPOS, TRUE, (int)(currentOverdriveThreshold * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_OVERDRIVE_TONE), TBM_SETPOS, TRUE, (int)(currentOverdriveTone * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_OVERDRIVE_MIX), TBM_SETPOS, TRUE, (int)(currentOverdriveMix * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_REVERB_SIZE), TBM_SETPOS, TRUE, (int)(currentReverbSize * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_REVERB_DAMPING), TBM_SETPOS, TRUE, (int)(currentReverbDamping * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_REVERB_WIDTH), TBM_SETPOS, TRUE, (int)(currentReverbWidth * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_REVERB_MIX), TBM_SETPOS, TRUE, (int)(currentReverbMix * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_WARM_AMOUNT), TBM_SETPOS, TRUE, (int)(currentWarmAmount * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_WARM_TONE), TBM_SETPOS, TRUE, (int)(currentWarmTone * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_WARM_SATURATION), TBM_SETPOS, TRUE, (int)(currentWarmSaturation * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_BLUES_GAIN), TBM_SETPOS, TRUE, (int)(currentBluesGain));
            SendMessageW(GetDlgItem(hwnd, SLIDER_BLUES_TONE), TBM_SETPOS, TRUE, (int)(currentBluesTone * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_BLUES_LEVEL), TBM_SETPOS, TRUE, (int)(currentBluesLevel * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_COMP_LEVEL), TBM_SETPOS, TRUE, (int)(currentCompLevel * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_COMP_TONE), TBM_SETPOS, TRUE, (int)(currentCompTone * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_COMP_ATTACK), TBM_SETPOS, TRUE, (int)(currentCompAttack));
            SendMessageW(GetDlgItem(hwnd, SLIDER_COMP_SUSTAIN), TBM_SETPOS, TRUE, (int)(currentCompSustain));
        }
        break;
    }
}

// Helper to get button name from XInput mask
std::wstring getButtonName(WORD buttonMask) {
    switch (buttonMask) {
    case XINPUT_GAMEPAD_A: return L"Joy A";
    case XINPUT_GAMEPAD_B: return L"Joy B";
    case XINPUT_GAMEPAD_X: return L"Joy X";
    case XINPUT_GAMEPAD_Y: return L"Joy Y";
    case XINPUT_GAMEPAD_LEFT_SHOULDER: return L"Joy LB";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER: return L"Joy RB";
    case XINPUT_GAMEPAD_LEFT_THUMB: return L"Joy LS";
    case XINPUT_GAMEPAD_RIGHT_THUMB: return L"Joy RS";
    case XINPUT_GAMEPAD_START: return L"Joy Start";
    case XINPUT_GAMEPAD_BACK: return L"Joy Back";
    case XINPUT_GAMEPAD_DPAD_UP: return L"Joy D-Up";
    case XINPUT_GAMEPAD_DPAD_DOWN: return L"Joy D-Down";
    case XINPUT_GAMEPAD_DPAD_LEFT: return L"Joy D-Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT: return L"Joy D-Right";
    default: return L"Joy ?";
    }
}

// Helper to display binding as string
std::wstring bindingToString(const ActionBinding& binding) {
    if (binding.type == InputType::Keyboard) {
        if (binding.keyOrButton >= 'A' && binding.keyOrButton <= 'Z') {
            wchar_t wbuf[2] = { (wchar_t)binding.keyOrButton, 0 };
            return wbuf;
        }
        else {
            wchar_t wbuf[16];
            swprintf(wbuf, 16, L"Key%d", binding.keyOrButton);
            return wbuf;
        }
    }
    else if (binding.type == InputType::Joystick) {
        return getButtonName((WORD)binding.keyOrButton);
    }
    else if (binding.type == InputType::Mouse) {
        switch (binding.keyOrButton) {
        case VK_LBUTTON: return L"Mouse L";
        case VK_RBUTTON: return L"Mouse R";
        case VK_MBUTTON: return L"Mouse M";
        case VK_XBUTTON1: return L"Mouse X1";
        case VK_XBUTTON2: return L"Mouse X2";
        default: return L"Mouse ?";
        }
    }
    return L"Unknown";
}

// Global variables for rebinding logic
HHOOK g_keyboardHook = nullptr;

// Keyboard hook procedure for rebinding
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && rebindingAction >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        // Skip system keys
        if (p->vkCode == VK_ESCAPE || p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) {
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }

        keyBindings[rebindingAction] = { InputType::Keyboard, (int)p->vkCode };
        SetWindowTextW(editBoxes[rebindingAction], bindingToString(keyBindings[rebindingAction]).c_str());
        rebindingAction = -1;
        if (g_keyboardHook) {
            UnhookWindowsHookEx(g_keyboardHook);
            g_keyboardHook = nullptr;
        }
        KillTimer(GetForegroundWindow(), 1);
        return 1; // eat event
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int windowWidth = 600;
int windowHeight = 500; // Increased height for reverb controls

// Store all slider and label HWNDs in arrays for easy management
const int NUM_SLIDERS = 25; // increased for compressor
HWND sliderLabels[NUM_SLIDERS] = { nullptr };
HWND sliders[NUM_SLIDERS] = { nullptr };


// Helper to split label into effect and parameter
std::pair<std::wstring, std::wstring> splitLabel(const std::wstring& label) {
    size_t pos = label.find(L' ');
    if (pos != std::wstring::npos) {
        return { label.substr(0, pos), label.substr(pos + 1) };
    }
    return { label, L"" };
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int leftPanelMinWidth = 320;
    static int rightPanelMinWidth = 250;
    static int effectLabelWidth = 80; // Effect name column
    static int paramLabelWidth = 80;  // Parameter name column
    static int sliderLabelWidth = effectLabelWidth + paramLabelWidth;
    static int sliderYStart = 10;
    static int sliderYStep = 40; // More space for two rows
    static int minGap = 20;
    static HWND paramLabels[NUM_SLIDERS] = { nullptr };

    switch (msg) {
    case WM_CREATE: {
        InitCommonControls();

        // Device ComboBox
        g_devices = processor->EnumerateDevices();
        hDeviceCombo = CreateWindowW(L"COMBOBOX", NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
            10, 10 + NUM_ACTIONS * 30 + 10, 280, 200, hwnd, (HMENU)4000, NULL, NULL);
        for (size_t i = 0; i < g_devices.size(); ++i) {
            SendMessageW(hDeviceCombo, CB_ADDSTRING, 0, (LPARAM)g_devices[i].name.c_str());
        }
        SendMessageW(hDeviceCombo, CB_SETCURSEL, 0, 0);
        // Start with first device
        if (!g_devices.empty()) {
            processor->StartProcessing(g_devices[0].id);
        }

        // Keybind UI (left side)
        for (int i = 0; i < NUM_ACTIONS; ++i) {
            // Convert action name to wide string
            int len = MultiByteToWideChar(CP_UTF8, 0, actions[i], -1, NULL, 0);
            std::wstring waction(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, actions[i], -1, &waction[0], len);
            CreateWindowW(L"STATIC", waction.c_str(), WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                10, 10 + i * 30, 180, 24, hwnd, NULL, NULL, NULL);
            HWND edit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY | WS_CLIPSIBLINGS,
                200, 10 + i * 30, 60, 24, hwnd, (HMENU)(UINT_PTR)(1000 + i), NULL, NULL);
            editBoxes[i] = edit;
            keyBindings[i] = { InputType::Keyboard, defaultKeys[i] };
            SetWindowTextW(edit, bindingToString(keyBindings[i]).c_str());
            CreateWindowW(L"BUTTON", L"Rebind", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                270, 10 + i * 30, 60, 24, hwnd, (HMENU)(UINT_PTR)(2000 + i), NULL, NULL);
        }

        // Sliders (right side) - now includes reverb
        int sliderY = sliderYStart;
        const wchar_t* effectLabels[NUM_SLIDERS] = {
            L"Tremolo Rate", L"Tremolo Depth", L"Chorus Rate", L"Chorus Depth", L"Chorus Feedback", L"Chorus Width", L"Main Volume",
            L"Overdrive Drive", L"Overdrive Threshold", L"Overdrive Tone", L"Overdrive Mix",
            L"Reverb Size", L"Reverb Damping", L"Reverb Width", L"Reverb Mix", L"Warm Amount", L"Warm Tone", L"Warm Saturation",
            L"Blues Gain", L"Blues Tone", L"Blues Level",
            L"Comp Level", L"Comp Tone", L"Comp Attack", L"Comp Sustain"
        };
        for (int i = 0; i < NUM_SLIDERS; ++i) {
            auto split = splitLabel(effectLabels[i]);
            sliderLabels[i] = CreateWindowW(L"STATIC", split.first.c_str(), WS_VISIBLE | WS_CHILD | SS_LEFT, leftPanelMinWidth + minGap, sliderY, effectLabelWidth, 20, hwnd, NULL, NULL, NULL);
            paramLabels[i] = CreateWindowW(L"STATIC", split.second.c_str(), WS_VISIBLE | WS_CHILD | SS_LEFT, leftPanelMinWidth + minGap + effectLabelWidth, sliderY, paramLabelWidth, 20, hwnd, NULL, NULL, NULL);
            int min = 0, max = 100, initialPos = 0;
            switch (i) {
            case 0: min = 1; max = 20; initialPos = (int)currentRate; break;
            case 1: min = 0; max = 100; initialPos = (int)(currentDepth * 100); break;
            case 2: min = 1; max = 50; initialPos = (int)(currentChorusRate * 10); break;
            case 3: min = 0; max = 100; initialPos = (int)(currentChorusDepth * 1000); break;
            case 4: min = 0; max = 100; initialPos = (int)(currentChorusFeedback * 100); break;
            case 5: min = 0; max = 100; initialPos = (int)(currentChorusWidth * 100); break;
            case 6: min = 0; max = 200; initialPos = (int)(currentMainVolume * 100); break;
            case 7: min = 1; max = 10; initialPos = (int)(currentOverdriveDrive); break;
            case 8: min = 1; max = 90; initialPos = (int)(currentOverdriveThreshold * 100); break;
            case 9: min = 0; max = 100; initialPos = (int)(currentOverdriveTone * 100); break;
            case 10: min = 0; max = 100; initialPos = (int)(currentOverdriveMix * 100); break;
            case 11: min = 0; max = 100; initialPos = (int)(currentReverbSize * 100); break;
            case 12: min = 0; max = 100; initialPos = (int)(currentReverbDamping * 100); break;
            case 13: min = 0; max = 100; initialPos = (int)(currentReverbWidth * 100); break;
            case 14: min = 0; max = 100; initialPos = (int)(currentReverbMix * 100); break;
            case 15: min = 0; max = 100; initialPos = (int)(currentWarmAmount * 100); break;
            case 16: min = 0; max = 100; initialPos = (int)(currentWarmTone * 100); break;
            case 17: min = 0; max = 100; initialPos = (int)(currentWarmSaturation * 100); break;
            case 18: min = 1; max = 10; initialPos = (int)(currentBluesGain); break;
            case 19: min = 0; max = 100; initialPos = (int)(currentBluesTone * 100); break;
            case 20: min = 0; max = 100; initialPos = (int)(currentBluesLevel * 100); break;
            case 21: min = 0; max = 200; initialPos = (int)(currentCompLevel * 100); break; // level 0..2 mapped to 0..200
            case 22: min = 0; max = 100; initialPos = (int)(currentCompTone * 100); break;
            case 23: min = 0; max = 100; initialPos = (int)(currentCompAttack); break; // attack ms 0..100
            case 24: min = 50; max = 2000; initialPos = (int)(currentCompSustain); break; // sustain ms range
            }

            sliders[i] = createSlider(hwnd, SLIDER_TREMOLO_RATE + i, leftPanelMinWidth + minGap + effectLabelWidth + paramLabelWidth, sliderY, min, max, initialPos);
            sliderY += sliderYStep;
        }

        // Initialize state tracking
        ZeroMemory(&g_prevJoyState, sizeof(XINPUT_STATE));
        ZeroMemory(g_prevKeyStates, sizeof(g_prevKeyStates));
        ZeroMemory(g_actionPressed, sizeof(g_actionPressed));

        // Start continuous input polling timer
        SetTimer(hwnd, 2, 16, NULL); // ~60 FPS polling for actions

        // Initialize processor blues params
        processor->SetBluesGain(currentBluesGain);
        processor->SetBluesTone(currentBluesTone);
        processor->SetBluesLevel(currentBluesLevel);
        processor->SetBluesEnabled(false);

        // Initialize processor compressor params
        processor->SetCompressorLevel(currentCompLevel);
        processor->SetCompressorTone(currentCompTone);
        processor->SetCompressorAttack(currentCompAttack);
        processor->SetCompressorSustain(currentCompSustain);
        processor->SetCompressorEnabled(false);
        break;
    }

    case WM_KEYDOWN: {
        // Handle direct keyboard input for actions (when not rebinding)
        if (rebindingAction < 0) {
            for (int i = 0; i < NUM_ACTIONS; ++i) {
                if (keyBindings[i].type == InputType::Keyboard &&
                    keyBindings[i].keyOrButton == (int)wParam &&
                    !g_actionPressed[i]) {
                    handleAction(i, hwnd);
                    g_actionPressed[i] = true;
                    return 0;
                }
            }
        }
        break;
    }

    case WM_KEYUP: {
        // Reset action pressed state on key release
        if (rebindingAction < 0) {
            for (int i = 0; i < NUM_ACTIONS; ++i) {
                if (keyBindings[i].type == InputType::Keyboard &&
                    keyBindings[i].keyOrButton == (int)wParam) {
                    g_actionPressed[i] = false;
                    break;
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: {
        if (rebindingAction >= 0) {
            // Mouse button rebinding
            int mouseButton = 0;
            switch (msg) {
            case WM_LBUTTONDOWN: mouseButton = VK_LBUTTON; break;
            case WM_RBUTTONDOWN: mouseButton = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: mouseButton = VK_MBUTTON; break;
            case WM_XBUTTONDOWN:
                mouseButton = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
                break;
            }

            if (mouseButton) {
                keyBindings[rebindingAction] = { InputType::Mouse, mouseButton };
                SetWindowTextW(editBoxes[rebindingAction], bindingToString(keyBindings[rebindingAction]).c_str());
                rebindingAction = -1;
                if (g_keyboardHook) {
                    UnhookWindowsHookEx(g_keyboardHook);
                    g_keyboardHook = nullptr;
                }
                KillTimer(hwnd, 1);
                return 0;
            }
        }
        else {
            // Mouse button actions
            int mouseButton = 0;
            switch (msg) {
            case WM_LBUTTONDOWN: mouseButton = VK_LBUTTON; break;
            case WM_RBUTTONDOWN: mouseButton = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: mouseButton = VK_MBUTTON; break;
            case WM_XBUTTONDOWN:
                mouseButton = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
                break;
            }

            for (int i = 0; i < NUM_ACTIONS; ++i) {
                if (keyBindings[i].type == InputType::Mouse &&
                    keyBindings[i].keyOrButton == mouseButton &&
                    !g_actionPressed[i]) {
                    handleAction(i, hwnd);
                    g_actionPressed[i] = true;
                    return 0;
                }
            }
        }
        break;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
        // Reset mouse button action states
        int mouseButton = 0;
        switch (msg) {
        case WM_LBUTTONUP: mouseButton = VK_LBUTTON; break;
        case WM_RBUTTONUP: mouseButton = VK_RBUTTON; break;
        case WM_MBUTTONUP: mouseButton = VK_MBUTTON; break;
        case WM_XBUTTONUP:
            mouseButton = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
            break;
        }

        for (int i = 0; i < NUM_ACTIONS; ++i) {
            if (keyBindings[i].type == InputType::Mouse &&
                keyBindings[i].keyOrButton == mouseButton) {
                g_actionPressed[i] = false;
                break;
            }
        }
        break;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        int leftPanelWidth = leftPanelMinWidth;
        int rightPanelWidth = rightPanelMinWidth;
        int gap = minGap;
        int sliderWidth = rightPanelWidth - sliderLabelWidth;
        int sliderY = sliderYStart;
        // If window is too small, stack right panel below left panel
        bool stackPanels = (width < leftPanelMinWidth + rightPanelMinWidth + minGap + 40);
        int sliderPanelX = stackPanels ? 10 : leftPanelWidth + gap;
        int sliderPanelY = stackPanels ? (NUM_ACTIONS * 30 + 30) : sliderYStart;
        for (int i = 0; i < NUM_SLIDERS; ++i) {
            if (sliderLabels[i]) MoveWindow(sliderLabels[i], sliderPanelX, sliderPanelY + i * sliderYStep, effectLabelWidth, 20, TRUE);
            if (paramLabels[i]) MoveWindow(paramLabels[i], sliderPanelX + effectLabelWidth, sliderPanelY + i * sliderYStep, paramLabelWidth, 20, TRUE);
            if (sliders[i]) MoveWindow(sliders[i], sliderPanelX + effectLabelWidth + paramLabelWidth, sliderPanelY + i * sliderYStep, sliderWidth, 24, TRUE);
        }
        break;
    }

    case WM_HSCROLL: {
        HWND slider = (HWND)lParam;
        int pos = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
        int id = GetDlgCtrlID(slider);
        switch (id) {
        case SLIDER_TREMOLO_RATE:
            currentRate = (float)pos;
            processor->SetTremoloRate(currentRate);
            break;
        case SLIDER_TREMOLO_DEPTH:
            currentDepth = (float)pos / 100.0f;
            processor->SetTremoloDepth(currentDepth);
            break;
        case SLIDER_CHORUS_RATE:
            currentChorusRate = (float)pos / 10.0f;
            processor->SetChorusRate(currentChorusRate);
            break;
        case SLIDER_CHORUS_DEPTH:
            currentChorusDepth = (float)pos / 1000.0f;
            processor->SetChorusDepth(currentChorusDepth);
            break;
        case SLIDER_CHORUS_FEEDBACK:
            processor->SetChorusFeedback((float)pos / 100.0f);
            break;
        case SLIDER_CHORUS_WIDTH:
            processor->SetChorusWidth((float)pos / 100.0f);
            break;
        case SLIDER_MAIN_VOLUME:
            currentMainVolume = (float)pos / 100.0f;
            processor->SetMainVolume(currentMainVolume);
            break;
        case SLIDER_OVERDRIVE_DRIVE:
            currentOverdriveDrive = (float)pos;
            processor->SetOverdriveDrive(currentOverdriveDrive);
            break;
        case SLIDER_OVERDRIVE_THRESHOLD:
            currentOverdriveThreshold = (float)pos / 100.0f;
            processor->SetOverdriveThreshold(currentOverdriveThreshold);
            break;
        case SLIDER_OVERDRIVE_TONE:
            currentOverdriveTone = (float)pos / 100.0f;
            processor->SetOverdriveTone(currentOverdriveTone);
            break;
        case SLIDER_OVERDRIVE_MIX:
            currentOverdriveMix = (float)pos / 100.0f;
            processor->SetOverdriveMix(currentOverdriveMix);
            break;
        case SLIDER_REVERB_SIZE:
            currentReverbSize = (float)pos / 100.0f;
            processor->SetReverbSize(currentReverbSize);
            break;
        case SLIDER_REVERB_DAMPING:
            currentReverbDamping = (float)pos / 100.0f;
            processor->SetReverbDamping(currentReverbDamping);
            break;
        case SLIDER_REVERB_WIDTH:
            currentReverbWidth = (float)pos / 100.0f;
            processor->SetReverbWidth(currentReverbWidth);
            break;
        case SLIDER_REVERB_MIX:
            currentReverbMix = (float)pos / 100.0f;
            processor->SetReverbMix(currentReverbMix);
            break;
        case SLIDER_WARM_AMOUNT:
            currentWarmAmount = (float)pos / 100.0f;
            processor->SetWarmAmount(currentWarmAmount);
            break;
        case SLIDER_WARM_TONE:
            currentWarmTone = (float)pos / 100.0f;
            processor->SetWarmTone(currentWarmTone);
            break;
        case SLIDER_WARM_SATURATION:
            currentWarmSaturation = (float)pos / 100.0f;
            processor->SetWarmSaturation(currentWarmSaturation);
            break;
        case SLIDER_BLUES_GAIN:
            currentBluesGain = (float)pos;
            processor->SetBluesGain(currentBluesGain);
            break;
        case SLIDER_BLUES_TONE:
            currentBluesTone = (float)pos / 100.0f;
            processor->SetBluesTone(currentBluesTone);
            break;
        case SLIDER_BLUES_LEVEL:
            currentBluesLevel = (float)pos / 100.0f;
            processor->SetBluesLevel(currentBluesLevel);
            break;
        case SLIDER_COMP_LEVEL:
            currentCompLevel = (float)pos / 100.0f * 2.0f; // map 0..200 -> 0..2
            processor->SetCompressorLevel(currentCompLevel);
            break;
        case SLIDER_COMP_TONE:
            currentCompTone = (float)pos / 100.0f;
            processor->SetCompressorTone(currentCompTone);
            break;
        case SLIDER_COMP_ATTACK:
            currentCompAttack = (float)pos;
            processor->SetCompressorAttack(currentCompAttack);
            break;
        case SLIDER_COMP_SUSTAIN:
            currentCompSustain = (float)pos;
            processor->SetCompressorSustain(currentCompSustain);
            break;
        }
        SetFocus(hwnd);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 4000 && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = (int)SendMessageW(hDeviceCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_devices.size()) {
                processor->StartProcessing(g_devices[sel].id);
            }
            break;
        }
        if (id >= 2000 && id < 2000 + NUM_ACTIONS) {
            rebindingAction = id - 2000;
            SetWindowTextW(editBoxes[rebindingAction], L"Press key/button...");
            // Set keyboard hook for keyboard rebinding
            if (!g_keyboardHook) {
                g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
            }
            // Save current joystick state for edge detection
            XInputGetState(0, &g_prevJoyState);
            SetTimer(hwnd, 1, 16, NULL); // Fast polling for rebinding detection
        }
        SetFocus(hwnd);
        break;
    }

    case WM_TIMER: {
        if (wParam == 1 && rebindingAction >= 0) {
            // Rebinding detection timer

            // Check multiple controllers for rebinding
            for (DWORD controllerId = 0; controllerId < XUSER_MAX_COUNT; controllerId++) {
                XINPUT_STATE state;
                ZeroMemory(&state, sizeof(XINPUT_STATE));

                if (XInputGetState(controllerId, &state) == ERROR_SUCCESS) {
                    // Debug current state during rebinding
                    static DWORD lastRebindDebug = 0;
                    DWORD currentTime = GetTickCount();
                    if (currentTime - lastRebindDebug > 500) {
                        wchar_t debug[100];
                        swprintf(debug, 100, L"Rebinding %d: Controller %d buttons=0x%04X", rebindingAction, controllerId, state.Gamepad.wButtons);
                        OutputDebugStringW(debug);
                        lastRebindDebug = currentTime;
                    }

                    WORD changed = state.Gamepad.wButtons ^ g_prevJoyState.Gamepad.wButtons;
                    WORD pressed = changed & state.Gamepad.wButtons;

                    if (pressed) {
                        wchar_t debug[100];
                        swprintf(debug, 100, L"Button pressed during rebind: 0x%04X", pressed);
                        OutputDebugStringW(debug);

                        // Find the first pressed button and store its mask
                        WORD buttonMask = 0;
                        for (int bit = 0; bit < 16; bit++) {
                            if (pressed & (1 << bit)) {
                                buttonMask = (1 << bit);
                                break;
                            }
                        }

                        if (buttonMask) {
                            wchar_t debug2[100];
                            swprintf(debug2, 100, L"Storing button mask: 0x%04X for action %d", buttonMask, rebindingAction);
                            OutputDebugStringW(debug2);

                            keyBindings[rebindingAction] = { InputType::Joystick, (int)buttonMask };
                            SetWindowTextW(editBoxes[rebindingAction], bindingToString(keyBindings[rebindingAction]).c_str());
                            rebindingAction = -1;
                            if (g_keyboardHook) {
                                UnhookWindowsHookEx(g_keyboardHook);
                                g_keyboardHook = nullptr;
                            }
                            KillTimer(hwnd, 1);
                        }
                        break;
                    }

                    // Update previous state for first controller
                    if (controllerId == 0) {
                        g_prevJoyState = state;
                    }
                    break;
                }
            }
        }
        else if (wParam == 2) {
            // Continuous joystick polling for actions
            static bool debugOnce = false;

            // Check multiple controllers (0-3)
            for (DWORD controllerId = 0; controllerId < XUSER_MAX_COUNT; controllerId++) {
                XINPUT_STATE state;
                ZeroMemory(&state, sizeof(XINPUT_STATE));

                if (XInputGetState(controllerId, &state) == ERROR_SUCCESS) {
                    if (!debugOnce) {
                        wchar_t debug[100];
                        swprintf(debug, 100, L"Controller %d connected, buttons: 0x%04X", controllerId, state.Gamepad.wButtons);
                        OutputDebugStringW(debug);
                        debugOnce = true;
                    }

                    // Check each action binding
                    for (int i = 0; i < NUM_ACTIONS; ++i) {
                        if (keyBindings[i].type == InputType::Joystick) {
                            WORD buttonMask = (WORD)keyBindings[i].keyOrButton;
                            bool currentlyPressed = (state.Gamepad.wButtons & buttonMask) != 0;

                            // Debug current state
                            static DWORD lastDebugTime = 0;
                            DWORD currentTime = GetTickCount();
                            if (currentTime - lastDebugTime > 1000) { // Debug every second
                                wchar_t debug[150];
                                swprintf(debug, 150, L"Action %d: mask=0x%04X, current=0x%04X, pressed=%d, actionPressed=%d",
                                    i, buttonMask, state.Gamepad.wButtons, currentlyPressed ? 1 : 0, g_actionPressed[i] ? 1 : 0);
                                OutputDebugStringW(debug);
                                lastDebugTime = currentTime;
                            }

                            // Only trigger on button press (not held)
                            if (currentlyPressed && !g_actionPressed[i]) {
                                // Debug output
                                wchar_t debug[100];
                                swprintf(debug, 100, L"Triggered action %d with button mask 0x%04X", i, buttonMask);
                                OutputDebugStringW(debug);

                                handleAction(i, hwnd);
                                g_actionPressed[i] = true;
                                break; // Exit action loop once triggered
                            }
                            else if (!currentlyPressed && g_actionPressed[i]) {
                                g_actionPressed[i] = false;
                            }
                        }
                    }

                    // Store state for first controller for rebinding
                    if (controllerId == 0) {
                        g_prevJoyState = state;
                    }
                    break; // Use first connected controller
                }
            }
        }
        break;
    }

    case WM_DESTROY:
        if (g_keyboardHook) {
            UnhookWindowsHookEx(g_keyboardHook);
            g_keyboardHook = nullptr;
        }
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    processor = new AudioProcessor();
    if (FAILED(processor->Initialize())) {
        MessageBoxW(NULL, L"Failed to initialize audio processor", L"Error", MB_OK);
        return 1;
    }
    std::vector<AudioDevice> devices = processor->EnumerateDevices();
    if (devices.empty()) {
        MessageBoxW(NULL, L"No audio capture devices found", L"Error", MB_OK);
        return 1;
    }
    processor->StartProcessing(devices[0].id);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AudioFXGUI";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"AudioFXGUI", L"Audio FX Key Rebinding & Effects",
        WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_MAXIMIZEBOX,
        100, 100, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete processor;
    return 0;
}