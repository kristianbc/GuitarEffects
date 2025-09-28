#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

struct AudioDevice {
    std::wstring id;
    std::wstring name;
    bool isCapture;
};

class AudioProcessor {
private:
    IMMDeviceEnumerator* deviceEnumerator;
    IMMDevice* captureDevice;
    IMMDevice* renderDevice;
    IAudioClient* captureClient;
    IAudioClient* renderClient;
    IAudioCaptureClient* captureInterface;
    IAudioRenderClient* renderInterface;
    WAVEFORMATEX* captureFormat;
    WAVEFORMATEX* renderFormat;
    UINT32 captureBufferFrames;
    UINT32 renderBufferFrames;
    std::atomic<float> tremoloRate;
    std::atomic<float> tremoloDepth;
    std::atomic<bool> tremoloEnabled;
    std::atomic<bool> running;
    float tremoloPhase;
    float sampleRate = 44100.0f;
    bool chorusEnabled = false;
    float chorusRate = 1.5f;
    float chorusDepth = 0.02f;
    float chorusFeedback = 0.3f;
    float chorusWidth = 0.5f;
    float chorusPhase = 0.0f;
    std::vector<float> chorusDelayBuffer;
    size_t chorusDelayIndex = 0;
    std::atomic<float> mainVolume;
public:
    AudioProcessor();
    ~AudioProcessor();
    HRESULT Initialize();
    std::vector<AudioDevice> EnumerateDevices();
    HRESULT SetupAudio(const std::wstring& captureDeviceId);
    void ApplyTremolo(float* buffer, UINT32 numFrames);
    void ApplyChorus(float* buffer, UINT32 numFrames);
    void SetSampleRate(float rate);
    void AudioLoop();
    void StartProcessing(const std::wstring& deviceId);
    void Stop();
    void SetTremoloEnabled(bool enabled);
    void SetTremoloRate(float rate);
    void SetTremoloDepth(float depth);
    void SetChorusEnabled(bool enabled);
    void SetChorusRate(float rate);
    void SetChorusDepth(float depth);
    void SetChorusFeedback(float feedback);
    void SetChorusWidth(float width);
    void SetMainVolume(float vol);
    float GetMainVolume() const;
    bool IsChorusEnabled() const;
    float GetChorusRate() const;
    float GetChorusDepth() const;
    float GetChorusFeedback() const;
    float GetChorusWidth() const;
    bool IsRunning() const;
    void Reset();
private:
    void Cleanup();
};
