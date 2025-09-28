#include "AudioProcessor.h"
#include <iostream>
#include <conio.h>

int main() {
    AudioProcessor processor;
    if (FAILED(processor.Initialize())) {
        std::cout << "Failed to initialize audio processor" << std::endl;
        return 1;
    }
    std::cout << "Enumerating audio capture devices..." << std::endl << std::endl;
    std::vector<AudioDevice> devices = processor.EnumerateDevices();
    if (devices.empty()) {
        std::cout << "No audio capture devices found" << std::endl;
        return 1;
    }
    for (size_t i = 0; i < devices.size(); i++) {
        std::wcout << i + 1 << L". " << devices[i].name << std::endl;
    }
    std::cout << std::endl << "Select device (1-" << devices.size() << "): ";
    int selection;
    std::cin >> selection;
    if (selection < 1 || selection >(int)devices.size()) {
        std::cout << "Invalid selection" << std::endl;
        return 1;
    }
    std::cout << "Starting audio processing... Press 'q' to quit, 't' to toggle tremolo" << std::endl;
    std::cout << "Tremolo controls: '1' decrease rate, '2' increase rate, '3' decrease depth, '4' increase depth" << std::endl;
    std::cout << "Chorus controls: 'c' to toggle, '}'/'{' to decrease/increase rate, '/'/'?' to decrease/increase depth" << std::endl;
    std::cout << "Press 'r' to reset all effects and looper to default." << std::endl;
    processor.StartProcessing(devices[selection - 1].id);
    bool tremoloState = false;
    float currentRate = 5.0f;
    float currentDepth = 0.5f;
    float currentChorusRate = 1.5f;
    float currentChorusDepth = 0.02f;
    float currentMainVolume = 1.0f;
    // Print initial effect values
    std::cout << "\rTremolo rate: " << currentRate << " Hz      " << std::endl;
    std::cout << "\rTremolo depth: " << currentDepth << "      " << std::endl;
    std::cout << "\rChorus rate: " << currentChorusRate << " Hz      " << std::endl;
    std::cout << "\rChorus depth: " << currentChorusDepth << "      " << std::endl;
    std::cout << "\rMain volume: " << currentMainVolume << "      " << std::endl;
    while (true) {
        char cmd = _getch();
        switch (cmd) {
        case 'q':
            processor.Stop();
            std::cout << "Stopping..." << std::endl;
            Sleep(1000);
            return 0;
        case 't':
            tremoloState = !tremoloState;
            processor.SetTremoloEnabled(tremoloState);
            std::cout << "Tremolo " << (tremoloState ? "enabled" : "disabled") << std::endl;
            break;
        case '1':
            currentRate = (currentRate > 1.0f) ? currentRate - 1.0f : 0.5f;
            processor.SetTremoloRate(currentRate);
            std::cout << "\rTremolo rate: " << currentRate << " Hz      " << std::flush;
            break;
        case '2':
            currentRate = (currentRate < 20.0f) ? currentRate + 1.0f : 20.0f;
            processor.SetTremoloRate(currentRate);
            std::cout << "\rTremolo rate: " << currentRate << " Hz      " << std::flush;
            break;
        case '3':
            currentDepth = (currentDepth > 0.1f) ? currentDepth - 0.1f : 0.0f;
            processor.SetTremoloDepth(currentDepth);
            std::cout << "\rTremolo depth: " << currentDepth << "      " << std::flush;
            break;
        case '4':
            currentDepth = (currentDepth < 0.9f) ? currentDepth + 0.1f : 1.0f;
            processor.SetTremoloDepth(currentDepth);
            std::cout << "\rTremolo depth: " << currentDepth << "      " << std::flush;
            break;
        case 'c':
            processor.SetChorusEnabled(!processor.IsChorusEnabled());
            std::cout << "Chorus " << (processor.IsChorusEnabled() ? "enabled" : "disabled") << std::endl;
            break;
        case ']':
            currentChorusRate = (currentChorusRate > 0.1f) ? currentChorusRate - 0.1f : 0.1f;
            processor.SetChorusRate(currentChorusRate);
            std::cout << "\rChorus rate: " << currentChorusRate << " Hz      " << std::flush;
            break;
        case '}':
            currentChorusRate = (currentChorusRate < 5.0f) ? currentChorusRate + 0.1f : 5.0f;
            processor.SetChorusRate(currentChorusRate);
            std::cout << "\rChorus rate: " << currentChorusRate << " Hz      " << std::flush;
            break;
        case '/':
            currentChorusDepth = (currentChorusDepth > 0.005f) ? currentChorusDepth - 0.005f : 0.0f;
            processor.SetChorusDepth(currentChorusDepth);
            std::cout << "\rChorus depth: " << currentChorusDepth << "      " << std::flush;
            break;
        case '?':
            currentChorusDepth = (currentChorusDepth < 0.1f) ? currentChorusDepth + 0.005f : 0.1f;
            processor.SetChorusDepth(currentChorusDepth);
            std::cout << "\rChorus depth: " << currentChorusDepth << "      " << std::flush;
            break;
        case 'v':
            currentMainVolume = (currentMainVolume > 0.05f) ? currentMainVolume - 0.05f : 0.0f;
            processor.SetMainVolume(currentMainVolume);
            std::cout << "\rMain volume: " << currentMainVolume << "      " << std::flush;
            break;
        case 'b':
            currentMainVolume = (currentMainVolume < 2.0f) ? currentMainVolume + 0.05f : 2.0f;
            processor.SetMainVolume(currentMainVolume);
            std::cout << "\rMain volume: " << currentMainVolume << "      " << std::flush;
            break;
        case 'r':
            processor.Reset();
            tremoloState = false;
            currentRate = 5.0f;
            currentDepth = 0.5f;
            currentChorusRate = 1.5f;
            currentChorusDepth = 0.02f;
            currentMainVolume = 1.0f;
            std::cout << "All effects and looper reset to default." << std::endl;
            break;
        }
    }
    return 0;
}