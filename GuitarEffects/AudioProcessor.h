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
    bool isCapture = false;
};

// Reverb filter structures
struct ReverbComb {
    std::vector<float> buffer;
    int bufferSize;
    int bufferIndex;
    float feedback;
    float filterStore;
    float damp1, damp2;

    ReverbComb() : bufferSize(0), bufferIndex(0), feedback(0), filterStore(0), damp1(0), damp2(0) {}

    void setBuffer(int size) {
        bufferSize = size;
        buffer.assign(size, 0.0f);
        bufferIndex = 0;
    }

    void setDamp(float val) {
        damp1 = val;
        damp2 = 1.0f - val;
    }

    void setFeedback(float val) {
        feedback = val;
    }

    float process(float input) {
        float output = buffer[bufferIndex];
        filterStore = (output * damp2) + (filterStore * damp1);
        buffer[bufferIndex] = input + (filterStore * feedback);
        if (++bufferIndex >= bufferSize) bufferIndex = 0;
        return output;
    }
};

struct ReverbAllpass {
    std::vector<float> buffer;
    int bufferSize;
    int bufferIndex;
    float feedback;

    ReverbAllpass() : bufferSize(0), bufferIndex(0), feedback(0) {}

    void setBuffer(int size) {
        bufferSize = size;
        buffer.assign(size, 0.0f);
        bufferIndex = 0;
    }

    void setFeedback(float val) {
        feedback = val;
    }

    float process(float input) {
        float bufout = buffer[bufferIndex];
        float output = -input + bufout;
        buffer[bufferIndex] = input + (bufout * feedback);
        if (++bufferIndex >= bufferSize) bufferIndex = 0;
        return output;
    }
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

    // Chorus parameters
    bool chorusEnabled = false;
    float chorusRate = 1.5f;
    float chorusDepth = 0.02f;
    float chorusFeedback = 0.3f;
    float chorusWidth = 0.5f;
    float chorusPhase = 0.0f;
    std::vector<float> chorusDelayBuffer;
    size_t chorusDelayIndex = 0;

    std::atomic<float> mainVolume;

    // Overdrive effect parameters
    bool overdriveEnabled = false;
    float overdriveDrive = 3.0f;
    float overdriveThreshold = 0.3f;
    float overdriveTone = 0.5f;
    float overdriveMix = 0.8f;
    float overdriveFilterState[2] = { 0.0f, 0.0f };

    // Blues driver effect parameters
    bool bluesEnabled = false;
    float bluesGain = 1.5f;   // input gain
    float bluesTone = 0.5f;   // 0..1 tone control
    float bluesLevel = 0.8f;  // output level (0..1)
    float bluesFilterState[2] = { 0.0f, 0.0f };

    // Compressor / Sustainer parameters
    bool compEnabled = false;
    float compLevel = 1.0f;   // makeup gain (0..2)
    float compTone = 0.5f;    // tone post-eq (0..1)
    float compAttackMs = 10.0f; // attack in ms
    float compSustainMs = 300.0f; // release/sustain in ms
    float compEnv[2] = { 0.0f, 0.0f };
    float compGainSmooth[2] = { 1.0f, 1.0f };
    float compLowState[2] = { 0.0f, 0.0f };

    // Reverb parameters
    bool reverbEnabled = false;
    float reverbSize = 0.5f;
    float reverbDamping = 0.5f;
    float reverbWidth = 1.0f;
    float reverbMix = 0.3f;
    bool reverbInitialized = false;

    // Reverb filter arrays
    ReverbComb reverbCombL[8], reverbCombR[8];
    ReverbAllpass reverbAllpassL[4], reverbAllpassR[4];

    // Warm effect parameters
    bool warmEnabled = false;
    float warmAmount = 0.5f;
    float warmTone = 0.5f;
    float warmSaturation = 0.3f;

    // Warm effect filter states
    float warmLowpassState[2] = { 0.0f, 0.0f };
    float warmHighpassState[2] = { 0.0f, 0.0f };
    float warmSaturatorState[2] = { 0.0f, 0.0f };

    // Wah effect state variables
    struct WahState {
        float freq;           // Center frequency (Hz)
        float q;              // Resonance/Q factor
        float mix;            // Dry/wet mix (0-1)
        float lfoRate;        // LFO rate for auto-wah (Hz)
        float lfoDepth;       // LFO depth (0-1)
        bool enabled;

        // Filter state variables (biquad filter)
        float z1L, z2L;       // Left channel state
        float z1R, z2R;       // Right channel state

        // LFO state
        float lfoPhase;

        // Envelope follower for dynamic wah (reacts to bends/slides)
        float env;            // current envelope level (0..1)
        float envAttackMs;    // attack time in ms
        float envReleaseMs;   // release time in ms

        WahState() : freq(800.0f), q(10.0f), mix(1.0f), 
                     lfoRate(0.0f), lfoDepth(0.0f), enabled(false),
                     z1L(0.0f), z2L(0.0f), z1R(0.0f), z2R(0.0f),
                     lfoPhase(0.0f), env(0.0f), envAttackMs(5.0f), envReleaseMs(80.0f) {}
    } wahState;

    // Wah effect methods
    void processWah(float* leftChannel, float* rightChannel, int numSamples);
    void updateWahCoefficients(float centerFreq, float sampleRate);
    void resetWahState();

    // Biquad filter coefficients for wah
    struct BiquadCoeffs {
        float b0, b1, b2;  // Numerator coefficients
        float a1, a2;      // Denominator coefficients (a0 is normalized to 1)

        BiquadCoeffs() : b0(1.0f), b1(0.0f), b2(0.0f), a1(0.0f), a2(0.0f) {}
    } wahCoeffs;

public:
    AudioProcessor();
    ~AudioProcessor();
    HRESULT Initialize();
    std::vector<AudioDevice> EnumerateDevices();
    HRESULT SetupAudio(const std::wstring& captureDeviceId);
    void ApplyTremolo(float* buffer, UINT32 numFrames);
    void ApplyChorus(float* buffer, UINT32 numFrames);
    void ApplyOverdrive(float* buffer, UINT32 numFrames);
    void ApplyReverb(float* buffer, UINT32 numFrames);
    void ApplyWarm(float* buffer, UINT32 numFrames);
    void ApplyBluesDriver(float* buffer, UINT32 numFrames);
    void ApplyCompressor(float* buffer, UINT32 numFrames);
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
    void SetOverdriveEnabled(bool enabled);
    void SetOverdriveDrive(float drive);
    void SetOverdriveThreshold(float threshold);
    void SetOverdriveTone(float tone);
    void SetOverdriveMix(float mix);

    // Blues driver methods
    void SetBluesEnabled(bool enabled);
    void SetBluesGain(float gain);
    void SetBluesTone(float tone);
    void SetBluesLevel(float level);
    bool IsBluesEnabled() const;
    float GetBluesGain() const;
    float GetBluesTone() const;
    float GetBluesLevel() const;

    // Compressor methods
    void SetCompressorEnabled(bool enabled);
    void SetCompressorLevel(float level);
    void SetCompressorTone(float tone);
    void SetCompressorAttack(float ms);
    void SetCompressorSustain(float ms);
    bool IsCompressorEnabled() const;
    float GetCompressorLevel() const;
    float GetCompressorTone() const;
    float GetCompressorAttack() const;
    float GetCompressorSustain() const;

    // Reverb methods
    void SetReverbEnabled(bool enabled);
    void SetReverbSize(float size);
    void SetReverbDamping(float damping);
    void SetReverbWidth(float width);
    void SetReverbMix(float mix);

    // Warm effect methods
    void SetWarmEnabled(bool enabled);
    void SetWarmAmount(float amount);
    void SetWarmTone(float tone);
    void SetWarmSaturation(float saturation);

    // Wah effect methods
    void setWahEnabled(bool enabled) { wahState.enabled = enabled; }
    bool getWahEnabled() const { return wahState.enabled; }

    void setWahFrequency(float freq) { wahState.freq = freq; }
    float getWahFrequency() const { return wahState.freq; }

    void setWahQ(float q) { wahState.q = q; }
    float getWahQ() const { return wahState.q; }

    void setWahMix(float mix) { wahState.mix = clamp(mix, 0.0f, 1.0f); }
    float getWahMix() const { return wahState.mix; }

    void setWahLFORate(float rate) { wahState.lfoRate = rate; }
    float getWahLFORate() const { return wahState.lfoRate; }

    void setWahLFODepth(float depth) { wahState.lfoDepth = clamp(depth, 0.0f, 1.0f); }
    float getWahLFODepth() const { return wahState.lfoDepth; }

    float GetMainVolume() const;
    bool IsChorusEnabled() const;
    float GetChorusRate() const;
    float GetChorusDepth() const;
    float GetChorusFeedback() const;
    float GetChorusWidth() const;
    bool IsRunning() const;
    bool IsOverdriveEnabled() const;
    float GetOverdriveDrive() const;
    float GetOverdriveThreshold() const;
    float GetOverdriveTone() const;
    float GetOverdriveMix() const;

    // Reverb getters
    bool IsReverbEnabled() const;
    float GetReverbSize() const;
    float GetReverbDamping() const;
    float GetReverbWidth() const;
    float GetReverbMix() const;

    // Warm effect getters
    bool IsWarmEnabled() const;
    float GetWarmAmount() const;
    float GetWarmTone() const;
    float GetWarmSaturation() const;

    void Reset();
private:
    void Cleanup();

    // Helper function to clamp values
    template<typename T>
    T clamp(T value, T min, T max) {
        return (value < min) ? min : (value > max) ? max : value;
    }
};