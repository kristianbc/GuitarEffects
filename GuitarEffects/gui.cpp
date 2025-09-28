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
    "Tremolo Toggle", "Chorus Toggle",
    "Reset All"
};
const int NUM_ACTIONS = sizeof(actions) / sizeof(actions[0]);

// Default key bindings (VK_*)
int defaultKeys[NUM_ACTIONS] = {
    'T', 'C', 'R'
};

// Key binding state
enum class InputType { Keyboard, Joystick };
struct ActionBinding {
    InputType type;
    int keyOrButton; // VK_* for keyboard, button index for joystick
};
std::map<int, ActionBinding> keyBindings; // action idx -> binding
std::map<int, HWND> editBoxes;  // action idx -> edit box HWND
int rebindingAction = -1;

AudioProcessor* processor = nullptr;
bool tremoloState = false;
bool chorusState = false;

// Effect parameter state
float currentRate = 5.0f;
float currentDepth = 0.5f;
float currentDistortionLevel = 0.5f;
float currentDistortionTone = 0.5f;
float currentDistortionDrive = 1.0f;
float currentChorusRate = 1.5f;
float currentChorusDepth = 0.02f;
float currentChorusFeedback = 0.3f;
float currentChorusWidth = 0.5f;
float currentMainVolume = 1.0f;

// Slider IDs
enum {
    SLIDER_TREMOLO_RATE = 3000,
    SLIDER_TREMOLO_DEPTH,
    SLIDER_CHORUS_RATE,
    SLIDER_CHORUS_DEPTH,
    SLIDER_CHORUS_FEEDBACK,
    SLIDER_CHORUS_WIDTH,
    SLIDER_MAIN_VOLUME
};

HWND createSlider(HWND parent, int id, int x, int y, int min, int max, int pos) {
    HWND h = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        x, y, 120, 24, parent, (HMENU)id, NULL, NULL);
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
        processor->SetChorusEnabled(!processor->IsChorusEnabled());
        break;
    case 2: // Reset All
        processor->Reset();
        tremoloState = false;
        chorusState = false;
        currentRate = 5.0f;
        currentDepth = 0.5f;
        currentChorusRate = 1.5f;
        currentChorusDepth = 0.02f;
        currentChorusFeedback = 0.3f;
        currentChorusWidth = 0.5f;
        currentMainVolume = 1.0f;
        // Update all sliders to reflect reset values if hwnd is provided
        if (hwnd) {
            SendMessageW(GetDlgItem(hwnd, SLIDER_TREMOLO_RATE), TBM_SETPOS, TRUE, (int)(currentRate * 10));
            SendMessageW(GetDlgItem(hwnd, SLIDER_TREMOLO_DEPTH), TBM_SETPOS, TRUE, (int)(currentDepth * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_RATE), TBM_SETPOS, TRUE, (int)(currentChorusRate * 10));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_DEPTH), TBM_SETPOS, TRUE, (int)(currentChorusDepth * 1000));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_FEEDBACK), TBM_SETPOS, TRUE, (int)(currentChorusFeedback * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_CHORUS_WIDTH), TBM_SETPOS, TRUE, (int)(currentChorusWidth * 100));
            SendMessageW(GetDlgItem(hwnd, SLIDER_MAIN_VOLUME), TBM_SETPOS, TRUE, (int)(currentMainVolume * 100));
        }
        break;
    }
}

// Use wide strings for WinAPI Unicode functions
#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

// Helper to display binding as string
std::wstring bindingToString(const ActionBinding& binding) {
    if (binding.type == InputType::Keyboard) {
        wchar_t wbuf[2] = { (wchar_t)binding.keyOrButton, 0 };
        return wbuf;
    }
    else {
        wchar_t wbuf[16];
        swprintf(wbuf, 16, L"JoyBtn%d", binding.keyOrButton);
        return wbuf;
    }
}

int windowWidth = 600;
int windowHeight = 400;

// Store all slider and label HWNDs in arrays for easy management
const int NUM_SLIDERS = 7;
HWND sliderLabels[NUM_SLIDERS] = {nullptr};
HWND sliders[NUM_SLIDERS] = {nullptr};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int leftPanelMinWidth = 320;
    static int rightPanelMinWidth = 250;
    static int sliderLabelWidth = 130;
    static int sliderYStart = 10;
    static int sliderYStep = 30;
    static int minGap = 20;
    switch (msg) {
    case WM_CREATE: {
        InitCommonControls();

        // Keybind UI (left side)
        for (int i = 0; i < NUM_ACTIONS; ++i) {
            // Convert action name to wide string
            int len = MultiByteToWideChar(CP_UTF8, 0, actions[i], -1, NULL, 0);
            std::wstring waction(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, actions[i], -1, &waction[0], len);
            CreateWindowW(L"STATIC", waction.c_str(), WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                10, 10 + i * 30, 180, 24, hwnd, NULL, NULL, NULL);
            HWND edit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_READONLY | WS_CLIPSIBLINGS,
                200, 10 + i * 30, 40, 24, hwnd, (HMENU)(1000 + i), NULL, NULL);
            editBoxes[i] = edit;
            keyBindings[i] = { InputType::Keyboard, defaultKeys[i] };
            SetWindowTextW(edit, bindingToString(keyBindings[i]).c_str());
            CreateWindowW(L"BUTTON", L"Rebind", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
                250, 10 + i * 30, 60, 24, hwnd, (HMENU)(2000 + i), NULL, NULL);
        }

        // Sliders (right side)
        int sliderY = sliderYStart;
        sliderLabels[0] = CreateWindowW(L"STATIC", L"Tremolo Rate", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[0] = createSlider(hwnd, SLIDER_TREMOLO_RATE, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 1, 20, (int)currentRate);
        sliderY += sliderYStep;
        sliderLabels[1] = CreateWindowW(L"STATIC", L"Tremolo Depth", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[1] = createSlider(hwnd, SLIDER_TREMOLO_DEPTH, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 0, 100, (int)(currentDepth * 100));
        sliderY += sliderYStep;
        sliderLabels[2] = CreateWindowW(L"STATIC", L"Chorus Rate", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[2] = createSlider(hwnd, SLIDER_CHORUS_RATE, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 1, 50, (int)(currentChorusRate * 10));
        sliderY += sliderYStep;
        sliderLabels[3] = CreateWindowW(L"STATIC", L"Chorus Depth", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[3] = createSlider(hwnd, SLIDER_CHORUS_DEPTH, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 0, 100, (int)(currentChorusDepth * 1000));
        sliderY += sliderYStep;
        sliderLabels[4] = CreateWindowW(L"STATIC", L"Chorus Feedback", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[4] = createSlider(hwnd, SLIDER_CHORUS_FEEDBACK, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 0, 100, (int)(processor->GetChorusFeedback() * 100));
        sliderY += sliderYStep;
        sliderLabels[5] = CreateWindowW(L"STATIC", L"Chorus Width", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[5] = createSlider(hwnd, SLIDER_CHORUS_WIDTH, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 0, 100, (int)(processor->GetChorusWidth() * 100));
        sliderY += sliderYStep;
        sliderLabels[6] = CreateWindowW(L"STATIC", L"Main Volume", WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS, leftPanelMinWidth + minGap, sliderY, sliderLabelWidth, 20, hwnd, NULL, NULL, NULL);
        sliders[6] = createSlider(hwnd, SLIDER_MAIN_VOLUME, leftPanelMinWidth + minGap + sliderLabelWidth, sliderY, 0, 200, (int)(currentMainVolume * 100));
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
        int sliderLabelWidth = 130;
        int sliderWidth = rightPanelWidth - sliderLabelWidth;
        int sliderY = sliderYStart;
        // If window is too small, stack right panel below left panel
        bool stackPanels = (width < leftPanelMinWidth + rightPanelMinWidth + minGap + 40);
        int sliderPanelX = stackPanels ? 10 : leftPanelWidth + gap;
        int sliderPanelY = stackPanels ? (NUM_ACTIONS * 30 + 30) : sliderYStart;
        for (int i = 0; i < NUM_SLIDERS; ++i) {
            if (sliderLabels[i]) MoveWindow(sliderLabels[i], sliderPanelX, sliderPanelY + i * sliderYStep, sliderLabelWidth, 20, TRUE);
            if (sliders[i]) MoveWindow(sliders[i], sliderPanelX + sliderLabelWidth, sliderPanelY + i * sliderYStep, sliderWidth, 24, TRUE);
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
        }
        SetFocus(hwnd); // Ensure main window regains focus after slider
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= 2000 && id < 2000 + NUM_ACTIONS) {
            rebindingAction = id - 2000;
            SetWindowTextW(editBoxes[rebindingAction], L"...");
            SetTimer(hwnd, 1, 50, NULL); // Start polling for joystick
        }
        SetFocus(hwnd); // Ensure main window regains focus after button
        break;
    }
    case WM_KEYDOWN: {
        if (rebindingAction >= 0) {
            keyBindings[rebindingAction] = { InputType::Keyboard, (int)wParam };
            SetWindowTextW(editBoxes[rebindingAction], bindingToString(keyBindings[rebindingAction]).c_str());
            rebindingAction = -1;
            KillTimer(hwnd, 1);
        }
        else {
            for (int i = 0; i < NUM_ACTIONS; ++i) {
                if (keyBindings[i].type == InputType::Keyboard && keyBindings[i].keyOrButton == (int)wParam) {
                    handleAction(i, hwnd);
                }
            }
        }
        break;
    }
    case WM_TIMER: {
        if (rebindingAction >= 0) {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(XINPUT_STATE));
            if (XInputGetState(0, &state) == ERROR_SUCCESS) {
                for (int btn = 0; btn < 14; ++btn) {
                    if (state.Gamepad.wButtons & (1 << btn)) {
                        keyBindings[rebindingAction] = { InputType::Joystick, btn };
                        SetWindowTextW(editBoxes[rebindingAction], bindingToString(keyBindings[rebindingAction]).c_str());
                        rebindingAction = -1;
                        KillTimer(hwnd, 1);
                        break;
                    }
                }
            }
        }
        break;
    }
    case WM_DESTROY:
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
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"AudioFXGUI", L"Audio FX Key Rebinding & Effects", WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_MAXIMIZEBOX,
        100, 100, windowWidth, windowHeight, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        // Poll joystick for actions
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(XINPUT_STATE));
        if (XInputGetState(0, &state) == ERROR_SUCCESS) {
            for (int i = 0; i < NUM_ACTIONS; ++i) {
                if (keyBindings[i].type == InputType::Joystick) {
                    int btn = keyBindings[i].keyOrButton;
                    if (state.Gamepad.wButtons & (1 << btn)) {
                        handleAction(i, hwnd);
                    }
                }
            }
        }
    }
    delete processor;
    return 0;
}