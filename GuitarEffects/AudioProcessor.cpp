#include "AudioProcessor.h"
#include <cmath>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <comdef.h>
#include <iostream>

// Constants
const float PI = 3.14159265358979323846f;
const float WAH_FREQ_MIN = 200.0f;   // Minimum wah frequency
const float WAH_FREQ_MAX = 3000.0f;  // Maximum wah frequency

// Add COM GUIDs used for device activation (define if not present)
const CLSID CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e} };
const IID IID_IMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6} };
const IID IID_IAudioClient = { 0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2} };
const IID IID_IAudioCaptureClient = { 0xc8adbd64, 0xe71e, 0x48a0, {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17} };
const IID IID_IAudioRenderClient = { 0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2} };

AudioProcessor::AudioProcessor() : deviceEnumerator(NULL), captureDevice(NULL),
renderDevice(NULL), captureClient(NULL),
renderClient(NULL), captureInterface(NULL),
renderInterface(NULL), captureFormat(NULL),
renderFormat(NULL), running(false),
tremoloEnabled(false), tremoloRate(5.0f),
tremoloDepth(0.5f), tremoloPhase(0.0f), sampleRate(44100),
captureBufferFrames(0), renderBufferFrames(0) {
}

AudioProcessor::~AudioProcessor() {
    Cleanup();
}

HRESULT AudioProcessor::Initialize() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return hr;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&deviceEnumerator);
    return hr;
}

std::vector<AudioDevice> AudioProcessor::EnumerateDevices() {
    std::vector<AudioDevice> devices;
    if (!deviceEnumerator) return devices;
    IMMDeviceCollection* captureCollection = NULL;
    HRESULT hr = deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &captureCollection);
    if (SUCCEEDED(hr) && captureCollection) {
        UINT count = 0;
        captureCollection->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice* device = NULL;
            if (SUCCEEDED(captureCollection->Item(i, &device)) && device) {
                AudioDevice audioDevice;
                audioDevice.isCapture = false; // initialize to false
                LPWSTR deviceId = NULL;
                if (SUCCEEDED(device->GetId(&deviceId)) && deviceId) {
                    audioDevice.id = deviceId;
                    CoTaskMemFree(deviceId);
                }
                IPropertyStore* props = NULL;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
                        if (varName.pwszVal) {
                            audioDevice.name = varName.pwszVal;
                        }
                    }
                    PropVariantClear(&varName);
                    props->Release();
                }
                audioDevice.isCapture = true;
                devices.push_back(audioDevice);
                device->Release();
            }
        }
        captureCollection->Release();
    }
    return devices;
}

HRESULT AudioProcessor::SetupAudio(const std::wstring& captureDeviceId) {
    if (!deviceEnumerator) {
        // Defensive: deviceEnumerator must be initialized by Initialize()
        return E_POINTER;
    }
    HRESULT hr;
    hr = deviceEnumerator->GetDevice(captureDeviceId.c_str(), &captureDevice);
    if (FAILED(hr) || !captureDevice) return hr;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &renderDevice);
    if (FAILED(hr) || !renderDevice) return hr;
    hr = captureDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&captureClient);
    if (FAILED(hr) || !captureClient) return hr;
    hr = renderDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&renderClient);
    if (FAILED(hr) || !renderClient) return hr;
    hr = captureClient->GetMixFormat(&captureFormat);
    if (FAILED(hr) || !captureFormat) return hr;
    hr = renderClient->GetMixFormat(&renderFormat); // use renderClient (IAudioClient) instead of IMMDevice
    if (FAILED(hr) || !renderFormat) return hr;
    sampleRate = static_cast<float>(captureFormat->nSamplesPerSec);
    hr = captureClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, captureFormat, NULL);
    if (FAILED(hr)) return hr;
    hr = renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, renderFormat, NULL);
    if (FAILED(hr)) return hr;
    hr = captureClient->GetBufferSize(&captureBufferFrames);
    if (FAILED(hr)) return hr;
    hr = renderClient->GetBufferSize(&renderBufferFrames);
    if (FAILED(hr)) return hr;
    hr = captureClient->GetService(IID_IAudioCaptureClient, (void**)&captureInterface);
    if (FAILED(hr) || !captureInterface) return hr;
    hr = renderClient->GetService(IID_IAudioRenderClient, (void**)&renderInterface);
    if (FAILED(hr) || !renderInterface) return hr;
    return S_OK;
}

void AudioProcessor::ApplyTremolo(float* buffer, UINT32 numFrames) {
    if (!tremoloEnabled || !buffer || !captureFormat) return;
    float rate = tremoloRate;
    float depth = tremoloDepth;
    for (UINT32 i = 0; i < numFrames; i++) {
        float tremolo = 1.0f + depth * sinf(tremoloPhase);
        for (int ch = 0; ch < (int)captureFormat->nChannels; ch++) {
            buffer[i * captureFormat->nChannels + ch] *= tremolo;
        }
        tremoloPhase += 2.0f * PI * rate / this->sampleRate;
        if (tremoloPhase > 2.0f * PI) {
            tremoloPhase -= 2.0f * PI;
        }
    }
}

void AudioProcessor::ApplyChorus(float* buffer, UINT32 numFrames) {
    if (!chorusEnabled || !buffer || !captureFormat) return;
    const int channels = captureFormat->nChannels;
    const float baseDelayMs = 15.0f;
    const float modDepthMs = 10.0f * chorusDepth;
    const float lfoRate = chorusRate;
    const float feedback = chorusFeedback;
    const float width = chorusWidth;
    const float wetMix = 0.5f;
    const float dryMix = 1.0f - wetMix;
    const int maxDelayMs = 40;
    const int maxDelaySamples = static_cast<int>((this->sampleRate * maxDelayMs) / 1000.0f);
    if (chorusDelayBuffer.size() < (size_t)(maxDelaySamples * channels)) {
        chorusDelayBuffer.assign(maxDelaySamples * channels, 0.0f);
        chorusDelayIndex = 0;
    }
    float phase = chorusPhase;
    for (UINT32 i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t bufIdx = i * channels + ch;
            float dry = buffer[bufIdx];
            float lfo = 0.6f * sinf(phase) + 0.4f * sinf(phase * 1.5f);
            float channelPhase = phase + (float)ch * width * PI;
            lfo = 0.6f * sinf(channelPhase) + 0.4f * sinf(channelPhase * 1.5f);
            float delayMs = baseDelayMs + modDepthMs * lfo;
            float delaySamples = (this->sampleRate * delayMs) / 1000.0f;
            float readPos = (float)chorusDelayIndex - delaySamples;
            while (readPos < 0) readPos += (float)chorusDelayBuffer.size() / channels;
            size_t idxA = ((size_t)readPos) * channels + ch;
            size_t idxB = (idxA + channels);
            if (idxB >= chorusDelayBuffer.size()) idxB -= chorusDelayBuffer.size();
            float frac = readPos - floorf(readPos);
            float sampleA = chorusDelayBuffer[idxA % chorusDelayBuffer.size()];
            float sampleB = chorusDelayBuffer[idxB % chorusDelayBuffer.size()];
            float wet = sampleA * (1.0f - frac) + sampleB * frac;
            buffer[bufIdx] = dryMix * dry + wetMix * wet;
            size_t writeIdx = (chorusDelayIndex * channels + ch);
            chorusDelayBuffer[writeIdx % chorusDelayBuffer.size()] = dry + wet * feedback;
        }
        phase += 2.0f * PI * lfoRate / this->sampleRate;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
        chorusDelayIndex++;
        if (chorusDelayIndex * channels >= chorusDelayBuffer.size()) chorusDelayIndex = 0;
    }
    chorusPhase = phase;
}

void AudioProcessor::ApplyOverdrive(float* buffer, UINT32 numFrames) {
    if (!overdriveEnabled || !buffer || !captureFormat) return;

    const int channels = captureFormat->nChannels;
    const float drive = overdriveDrive;        // 1.0f to 10.0f+ (input gain)
    const float threshold = overdriveThreshold; // 0.1f to 0.9f (where overdrive kicks in)
    const float tone = overdriveTone;          // 0.0f to 1.0f (tone control)
    const float wetMix = overdriveMix;         // 0.0f to 1.0f (dry/wet blend)
    const float dryMix = 1.0f - wetMix;

    // Calculate dynamic parameters based on threshold
    // Lower threshold = more aggressive overdrive and better compensation
    const float sensitivity = 1.0f - threshold;  // 0.0 to 0.9
    const float outputGain = 1.0f + (sensitivity * 2.0f); // Compensate volume loss
    const float saturationAmount = 1.5f + (sensitivity * 3.0f); // More saturation at lower thresholds

    // Pre-emphasis filter for bite (boosts mids before overdrive)
    const float preEmphasisGain = 1.0f + (sensitivity * 0.8f);

    // Tone shaping parameters
    const float bassRolloff = 0.3f + (tone * 0.4f); // More bass cut with higher tone
    const float trebleBoost = 1.0f + (tone * 1.5f); // More treble with higher tone

    for (UINT32 i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t bufIdx = i * channels + ch;
            float input = buffer[bufIdx];

            // Stage 1: Input gain and pre-emphasis
            float signal = input * drive * preEmphasisGain;

            // Stage 2: Asymmetric tube-style overdrive
            float overdriven;
            float absSignal = fabsf(signal);

            if (absSignal <= threshold) {
                // Clean region - slight compression for punch
                overdriven = signal * (1.0f + (absSignal / threshold) * 0.3f);
            }
            else {
                // Overdrive region - progressive saturation
                float excess = absSignal - threshold;
                float normalizedExcess = excess / (1.0f - threshold + 0.001f); // Avoid division by zero

                // Asymmetric clipping (different curves for positive/negative)
                if (signal > 0.0f) {
                    // Positive half - harder clipping
                    float saturation = 1.0f - expf(-normalizedExcess * saturationAmount);
                    overdriven = threshold + (saturation * (1.0f - threshold) * 0.85f);
                }
                else {
                    // Negative half - softer clipping for asymmetry
                    float saturation = 1.0f - expf(-normalizedExcess * saturationAmount * 0.8f);
                    overdriven = -(threshold + (saturation * (1.0f - threshold) * 0.75f));
                }

                // Add harmonic content for aggression
                float harmonicContent = signal * absSignal * 0.15f * sensitivity;
                overdriven += harmonicContent;
            }

            // Stage 3: Tone shaping (simulates amp tone stack)
            float toneProcessed = overdriven;

            // Simple bass/treble adjustment
            if (ch < 2) { // Only process first two channels for filter state
                // High-pass filter for bass rolloff
                float bassFiltered = overdriven * bassRolloff +
                    overdriveFilterState[ch] * (1.0f - bassRolloff);
                overdriveFilterState[ch] = bassFiltered;

                // Treble emphasis
                toneProcessed = bassFiltered + (overdriven - bassFiltered) * trebleBoost;
            }

            // Stage 4: Output processing
            // Soft limiting to prevent harsh clipping
            if (fabsf(toneProcessed) > 0.9f) {
                float sign = (toneProcessed > 0.0f) ? 1.0f : -1.0f;
                float compressed = 0.9f + (fabsf(toneProcessed) - 0.9f) * 0.1f;
                toneProcessed = sign * fminf(compressed, 0.98f);
            }

            // Apply output gain compensation
            toneProcessed *= outputGain;

            // Final mix
            buffer[bufIdx] = dryMix * input + wetMix * toneProcessed;
        }
    }
}

void AudioProcessor::ApplyReverb(float* buffer, UINT32 numFrames) {
    if (!reverbEnabled || !buffer || !captureFormat) return;

    const int channels = captureFormat->nChannels;
    if (channels < 2) return; // Reverb requires stereo

    // Initialize reverb filters if needed
    if (!reverbInitialized) {
        // Comb filter delays (in samples at 44.1kHz)
        int combTunings[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        // Allpass filter delays
        int allpassTunings[4] = { 556, 441, 341, 225 };

        // Scale delays for current sample rate
        float scaleFactor = this->sampleRate / 44100.0f;

        for (int i = 0; i < 8; i++) {
            reverbCombL[i].setBuffer((int)(combTunings[i] * scaleFactor));
            reverbCombR[i].setBuffer((int)(combTunings[i] * scaleFactor * 1.1f)); // Slight offset for stereo
        }

        for (int i = 0; i < 4; i++) {
            reverbAllpassL[i].setBuffer((int)(allpassTunings[i] * scaleFactor));
            reverbAllpassR[i].setBuffer((int)(allpassTunings[i] * scaleFactor * 1.1f));
            reverbAllpassL[i].setFeedback(0.5f);
            reverbAllpassR[i].setFeedback(0.5f);
        }

        reverbInitialized = true;
    }

    // Update reverb parameters
    float roomSize = reverbSize * 0.28f + 0.7f;
    float damping = reverbDamping * 0.4f;

    for (int i = 0; i < 8; i++) {
        reverbCombL[i].setFeedback(roomSize);
        reverbCombR[i].setFeedback(roomSize);
        reverbCombL[i].setDamp(damping);
        reverbCombR[i].setDamp(damping);
    }

    const float wetGain = reverbMix * 3.0f;
    const float dryGain = 1.0f - reverbMix;
    const float width = reverbWidth;

    for (UINT32 i = 0; i < numFrames; i++) {
        float inputL = buffer[i * channels];
        float inputR = buffer[i * channels + 1];

        // Mix input to mono for reverb processing
        float input = (inputL + inputR) * 0.015f; // Scale down input

        // Process through comb filters
        float combOutputL = 0.0f, combOutputR = 0.0f;

        for (int c = 0; c < 8; c++) {
            combOutputL += reverbCombL[c].process(input);
            combOutputR += reverbCombR[c].process(input);
        }

        // Process through allpass filters
        float allpassOutputL = combOutputL;
        float allpassOutputR = combOutputR;

        for (int a = 0; a < 4; a++) {
            allpassOutputL = reverbAllpassL[a].process(allpassOutputL);
            allpassOutputR = reverbAllpassR[a].process(allpassOutputR);
        }

        // Apply stereo width
        float reverbL = allpassOutputL * (1.0f + width) * 0.5f + allpassOutputR * (1.0f - width) * 0.5f;
        float reverbR = allpassOutputR * (1.0f + width) * 0.5f + allpassOutputL * (1.0f - width) * 0.5f;

        // Mix dry and wet signals
        buffer[i * channels] = inputL * dryGain + reverbL * wetGain;
        buffer[i * channels + 1] = inputR * dryGain + reverbR * wetGain;

        // Copy to additional channels if present
        for (int ch = 2; ch < channels; ch++) {
            buffer[i * channels + ch] = buffer[i * channels + (ch % 2)];
        }
    }
}

void AudioProcessor::ApplyWarm(float* buffer, UINT32 numFrames) {
    if (!warmEnabled || !buffer || !captureFormat) return;

    const int channels = captureFormat->nChannels;
    const float amount = warmAmount;
    const float tone = warmTone;
    const float saturation = warmSaturation;

    // Make the effect more pronounced
    const float wetMix = amount;  // Use full amount for wet mix
    const float dryMix = 1.0f - wetMix;

    // More aggressive parameters for audible effect
    const float compressThreshold = 0.2f; // Lower threshold for more compression
    const float compressRatio = 0.3f + (amount * 0.4f); // Variable compression
    const float saturationDrive = 1.0f + (saturation * 3.0f); // More drive
    const float harmonicAmount = saturation * 0.5f; // More prominent harmonics

    // Frequency shaping - more pronounced
    const float bassBoost = 1.0f + (1.0f - tone) * 0.8f; // Boost bass when tone is low
    const float trebleRoll = 1.0f - (tone * 0.3f); // Roll off highs when tone is high
    const float midWarmth = 1.0f + amount * 0.4f; // Mid frequency warmth

    for (UINT32 i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < channels && ch < 2; ++ch) {
            size_t bufIdx = i * channels + ch;
            float input = buffer[bufIdx];
            float processed = input;

            // Stage 1: Pre-emphasis and bass boost
            processed *= bassBoost;

            // Stage 2: Soft compression for glue
            float absSignal = fabsf(processed);
            if (absSignal > compressThreshold) {
                float excess = absSignal - compressThreshold;
                float compressed = compressThreshold + excess * compressRatio;
                processed = (processed > 0.0f) ? compressed : -compressed;
            }

            // Stage 3: Tube-style saturation (more aggressive)
            float driven = processed * saturationDrive;
            float saturated;

            if (fabsf(driven) <= 1.0f) {
                // Polynomial saturation for warmth
                float x = driven;
                float x2 = x * x;
                float x3 = x2 * x;
                saturated = x - (x3 * 0.33f) + (x2 * harmonicAmount * 0.1f);
            }
            else {
                // Hard limiting with soft knee
                float sign = (driven > 0.0f) ? 1.0f : -1.0f;
                float magnitude = fabsf(driven);
                saturated = sign * (1.0f - expf(-(magnitude - 1.0f) * 0.5f));
            }

            // Scale back down
            saturated *= 0.7f;

            // Stage 4: Add even harmonics for tube warmth
            if (harmonicAmount > 0.01f) {
                float harmonic2 = saturated * saturated * harmonicAmount * 0.15f;
                float harmonic3 = saturated * saturated * saturated * harmonicAmount * 0.05f;
                saturated += harmonic2 + harmonic3;
            }

            // Stage 5: Tone shaping and mid warmth
            saturated *= midWarmth;

            // Simple high-frequency roll-off for smoothness
            warmLowpassState[ch] += 0.3f * (saturated * trebleRoll - warmLowpassState[ch]);
            float toneProcessed = warmLowpassState[ch];

            // Stage 6: Output gain compensation
            toneProcessed *= (0.8f + amount * 0.4f); // Compensate for level changes

            // Final limiting
            if (fabsf(toneProcessed) > 0.95f) {
                float sign = (toneProcessed > 0.0f) ? 1.0f : -1.0f;
                toneProcessed = sign * 0.95f;
            }

            // Mix with original signal
            buffer[bufIdx] = dryMix * input + wetMix * toneProcessed;
        }

        // Copy to additional channels
        for (int ch = 2; ch < channels; ch++) {
            buffer[i * channels + ch] = buffer[i * channels + (ch % 2)];
        }
    }
}

void AudioProcessor::ApplyBluesDriver(float* buffer, UINT32 numFrames) {
    if (!bluesEnabled || !buffer || !captureFormat) return;
    const int channels = captureFormat->nChannels;

    // Thorny blues parameters - more aggressive and edgy
    const float inputGain = bluesGain * 1.8f; // More input drive for harder clipping
    const float preDistortionBoost = 1.4f; // Pre-emphasis for bite

    // Asymmetric clipping for that "broken" blues sound
    const float softThreshold = 0.3f;
    const float hardThreshold = 0.65f;

    // Tone shaping - scooped mids with enhanced highs for sparkle
    const float bassPresence = 1.2f + (1.0f - bluesTone) * 0.5f;
    const float midScoop = 0.6f + bluesTone * 0.2f; // Slight scoop
    const float trebleBoost = 1.5f + bluesTone * 1.0f; // Lots of high-end bite
    const float presenceFreq = 0.15f; // High-mid presence

    // Harmonic generation for grit
    const float harmonicDrive = 0.3f;

    for (UINT32 i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t idx = i * channels + ch;
            float input = buffer[idx];

            // Stage 1: Pre-emphasis boost for attack
            float signal = input * inputGain * preDistortionBoost;

            // Stage 2: Asymmetric hard clipping (diode-like behavior)
            float clipped;
            float absSignal = fabsf(signal);

            if (absSignal < softThreshold) {
                // Clean region with slight compression
                clipped = signal * (1.0f + absSignal * 0.2f);
            }
            else if (absSignal < hardThreshold) {
                // Soft saturation region
                float excess = (absSignal - softThreshold) / (hardThreshold - softThreshold);
                float curve = excess - (excess * excess * excess * 0.33f);
                float sat = softThreshold + curve * (hardThreshold - softThreshold);
                clipped = (signal > 0.0f) ? sat : -sat;
            }
            else {
                // Hard clipping region - asymmetric!
                float excess = absSignal - hardThreshold;
                if (signal > 0.0f) {
                    // Positive: harder clipping (more compression)
                    float hardSat = hardThreshold + (1.0f - hardThreshold) * (1.0f - expf(-excess * 2.0f));
                    clipped = fminf(hardSat, 0.95f);
                }
                else {
                    // Negative: softer clipping (more dynamics)
                    float softSat = hardThreshold + (1.0f - hardThreshold) * (1.0f - expf(-excess * 1.2f));
                    clipped = -fminf(softSat, 0.90f);
                }
            }

            // Stage 3: Add even and odd harmonics for thorny character
            float x2 = clipped * clipped;
            float x3 = x2 * clipped;
            float harmonics = (x2 * harmonicDrive * 0.15f) + // 2nd harmonic (warmth)
                (x3 * harmonicDrive * 0.25f);   // 3rd harmonic (grit)
            clipped += harmonics;

            // Stage 4: Tone stack (Marshall-inspired with more bite)
            // Use simple filters stored in bluesFilterState[0] = low, [1] = high
            int filterIdx = ch % 2;

            // Low-shelf for bass
            float lowTarget = clipped * bassPresence;
            bluesFilterState[filterIdx] += 0.08f * (lowTarget - bluesFilterState[filterIdx]);
            float bass = bluesFilterState[filterIdx];

            // High-shelf for treble and presence
            float highpass = clipped - bass;
            float treble = highpass * trebleBoost;

            // Mid calculation with scoop
            float mid = (clipped - bass * 0.5f - highpass * 0.5f) * midScoop;

            // Presence boost (upper mids) - the "thorn"
            float presence = highpass * presenceFreq * 2.5f;

            // Mix tone components
            float toneMixed = bass * 0.35f + mid * 0.25f + treble * 0.3f + presence * 0.1f;

            // Stage 5: Final soft limiting to prevent harshness
            float output = toneMixed;
            float absOut = fabsf(output);
            if (absOut > 0.85f) {
                float sign = (output > 0.0f) ? 1.0f : -1.0f;
                float limited = 0.85f + (absOut - 0.85f) * 0.3f;
                output = sign * fminf(limited, 0.98f);
            }

            // Apply output level with slight boost to compensate
            buffer[idx] = output * bluesLevel * 1.1f;
        }
    }
}

// --- Compressor implementation ---
void AudioProcessor::ApplyCompressor(float* buffer, UINT32 numFrames) {
    if (!compEnabled || !buffer || !captureFormat) return;
    const int channels = captureFormat->nChannels;

    // Convert parameters to useful values
    float attackSec = fmaxf(0.1f, compAttackMs) / 1000.0f;
    float releaseSec = fmaxf(10.0f, compSustainMs) / 1000.0f;

    // Advanced envelope coefficients with adaptive behavior
    float attackCoef = expf(-1.0f / (attackSec * this->sampleRate));
    float releaseCoef = expf(-1.0f / (releaseSec * this->sampleRate));

    // Sustainer-specific parameters
    const float threshold = 0.15f; // Lower threshold for more sustain
    const float ratio = 8.0f; // High ratio for strong compression
    const float kneeWidth = 0.1f; // Soft knee for smooth compression
    const float makeupGain = 2.5f; // Boost output to compensate

    // Sustain enhancement - slower release for longer notes
    float sustainFactor = compSustainMs / 1000.0f;
    float adaptiveReleaseCoef = expf(-1.0f / ((releaseSec * (1.0f + sustainFactor * 2.0f)) * this->sampleRate));

    for (UINT32 i = 0; i < numFrames; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            size_t idx = i * channels + ch;
            float x = buffer[idx];
            float absx = fabsf(x);

            // --- Stage 1: Peak detection with attack/release ---
            float env = compEnv[ch];
            if (absx > env) {
                // Attack phase - fast response to peaks
                env = attackCoef * env + (1.0f - attackCoef) * absx;
            }
            else {
                // Release phase - use adaptive release for sustain
                env = adaptiveReleaseCoef * env + (1.0f - adaptiveReleaseCoef) * absx;
            }
            compEnv[ch] = env;

            // --- Stage 2: Compute gain reduction with soft knee ---
            float gain = 1.0f;

            if (env > threshold - kneeWidth && env < threshold + kneeWidth) {
                // Soft knee region - smooth transition
                float kneeInput = env - threshold + kneeWidth;
                float kneeOutput = kneeInput * kneeInput / (4.0f * kneeWidth);
                float dbOver = 20.0f * log10f((threshold + kneeOutput) / threshold + 1e-20f);
                float dbReduce = dbOver - dbOver / ratio;
                gain = powf(10.0f, -dbReduce / 20.0f);
            }
            else if (env >= threshold + kneeWidth) {
                // Above knee - full compression
                float dbOver = 20.0f * log10f(env / threshold + 1e-20f);
                float dbReduce = dbOver - dbOver / ratio;
                gain = powf(10.0f, -dbReduce / 20.0f);
            }
            // else: below knee, gain = 1.0f (no compression)

            // --- Stage 3: Smooth gain to prevent zipper noise ---
            float smooth = compGainSmooth[ch];
            float smoothCoef = 0.001f; // Very smooth for sustain
            smooth = smooth * (1.0f - smoothCoef) + gain * smoothCoef;
            compGainSmooth[ch] = smooth;

            // --- Stage 4: Apply compression and makeup gain ---
            float compressed = x * smooth * makeupGain * compLevel;

            // --- Stage 5: Sustain enhancement - add subtle harmonics ---
            // This helps maintain presence during long sustains
            float harmonic = compressed * fabsf(compressed) * 0.08f * sustainFactor;
            compressed += harmonic;

            // --- Stage 6: Advanced tone control ---
            // Simulate amp-like tone stack
            float low = compLowState[ch];
            low += 0.03f * (compressed - low); // Low shelf
            compLowState[ch] = low;

            float high = compressed - low; // High shelf

            // Tone control: 0 = warm (more lows), 1 = bright (more highs)
            float midBoost = 1.0f + (1.0f - fabsf(compTone - 0.5f) * 2.0f) * 0.3f; // Mid emphasis
            float toneBalance = compTone;

            float output = (low * (1.0f - toneBalance) * 1.2f +  // Bass boost when tone is low
                compressed * midBoost * 0.4f +          // Mids always present
                high * toneBalance * 1.5f);             // Treble boost when tone is high

            // --- Stage 7: Soft limiting to prevent clipping ---
            float absOut = fabsf(output);
            if (absOut > 0.9f) {
                float sign = (output > 0.0f) ? 1.0f : -1.0f;
                float limited = 0.9f + (absOut - 0.9f) * 0.1f;
                output = sign * fminf(limited, 0.98f);
            }

            buffer[idx] = output;
        }
    }
}

// Wah effect processing
void AudioProcessor::processWah(float* leftChannel, float* rightChannel, int numSamples) {
    if (!wahState.enabled || !captureFormat) {
        return;
    }

    const float sr = this->sampleRate;
    const float lfoIncrement = (2.0f * PI * wahState.lfoRate) / sr;

    // Envelope attack/release coefficients
    float attackCoef = expf(-1.0f / (fmaxf(0.001f, wahState.envAttackMs) * 0.001f * sr));
    float releaseCoef = expf(-1.0f / (fmaxf(1.0f, wahState.envReleaseMs) * 0.001f * sr));

    // Keep track of last center frequency to smooth coefficient updates
    static float smoothFreq = wahState.freq;
    const float smoothFactor = 0.08f; // smoothing for freq changes

    for (int i = 0; i < numSamples; ++i) {
        // Calculate instantaneous input level (RMS-ish using abs average)
        float inL = leftChannel[i];
        float inR = rightChannel[i];
        float level = (fabsf(inL) + fabsf(inR)) * 0.5f;

        // Update envelope follower (attack/release)
        if (level > wahState.env) {
            wahState.env = attackCoef * wahState.env + (1.0f - attackCoef) * level;
        } else {
            wahState.env = releaseCoef * wahState.env + (1.0f - releaseCoef) * level;
        }

        // Compute modulation amount from envelope (scale 0..1)
        float envMod = fminf(1.0f, wahState.env * 3.0f); // scale up sensitivity

        // LFO value
        float lfoValue = 0.0f;
        if (wahState.lfoRate > 0.0f && wahState.lfoDepth > 0.0f) {
            // use sine LFO for smoothness
            lfoValue = 0.5f * (1.0f + sinf(wahState.lfoPhase)); // 0..1
            wahState.lfoPhase += lfoIncrement;
            if (wahState.lfoPhase >= 2.0f * PI) wahState.lfoPhase -= 2.0f * PI;
        }

        // Combine envelope and LFO to determine target frequency
        float envInfluence = envMod * wahState.lfoDepth; // envelope scaled by LFO depth param
        float lfoInfluence = lfoValue * wahState.lfoDepth;
        float combined = (envInfluence + lfoInfluence) / fmaxf(0.0001f, (wahState.lfoDepth + wahState.lfoDepth));
        // If lfoDepth is zero, fall back to envelope only
        if (wahState.lfoDepth <= 0.0001f) combined = envMod;

        float targetFreq = WAH_FREQ_MIN + combined * (WAH_FREQ_MAX - WAH_FREQ_MIN);
        // Mix with manual freq setting (manual has some weight)
        targetFreq = (targetFreq * 0.9f) + (wahState.freq * 0.1f);

        // Smooth frequency to avoid zipper noise
        smoothFreq += (targetFreq - smoothFreq) * smoothFactor;

        // Update coefficients if change significant
        static float lastUpdatedFreq = 0.0f;
        if (fabsf(smoothFreq - lastUpdatedFreq) > 1.0f) {
            updateWahCoefficients(smoothFreq, sr);
            lastUpdatedFreq = smoothFreq;
        }

        // Process left channel sample with biquad difference eq (Direct Form 2 Transposed variant used variables z1/z2 as states)
        float inSampleL = inL;
        float outL = wahCoeffs.b0 * inSampleL + wahCoeffs.b1 * wahState.z1L + wahCoeffs.b2 * wahState.z2L - wahCoeffs.a1 * wahState.z1L - wahCoeffs.a2 * wahState.z2L;
        wahState.z2L = wahState.z1L;
        wahState.z1L = outL;

        float inSampleR = inR;
        float outR = wahCoeffs.b0 * inSampleR + wahCoeffs.b1 * wahState.z1R + wahCoeffs.b2 * wahState.z2R - wahCoeffs.a1 * wahState.z1R - wahCoeffs.a2 * wahState.z2R;
        wahState.z2R = wahState.z1R;
        wahState.z1R = outR;

        // Mix dry/wet
        leftChannel[i] = inSampleL * (1.0f - wahState.mix) + outL * wahState.mix;
        rightChannel[i] = inSampleR * (1.0f - wahState.mix) + outR * wahState.mix;
    }
}

void AudioProcessor::updateWahCoefficients(float centerFreq, float sampleRate) {
    // Bandpass filter using RBJ cookbook formula
    float omega = 2.0f * PI * centerFreq / sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * wahState.q);

    float a0 = 1.0f + alpha;

    // Bandpass filter coefficients (constant 0 dB peak gain)
    wahCoeffs.b0 = alpha / a0;
    wahCoeffs.b1 = 0.0f;
    wahCoeffs.b2 = -alpha / a0;
    wahCoeffs.a1 = -2.0f * cosOmega / a0;
    wahCoeffs.a2 = (1.0f - alpha) / a0;
}

void AudioProcessor::resetWahState() {
    wahState.z1L = 0.0f;
    wahState.z2L = 0.0f;
    wahState.z1R = 0.0f;
    wahState.z2R = 0.0f;
    wahState.lfoPhase = 0.0f;
}

// --- AudioProcessor method implementations ---
void AudioProcessor::Reset() {
    tremoloRate = 5.0f;
    tremoloDepth = 0.5f;
    tremoloEnabled = false;
    chorusEnabled = false;
    chorusRate = 1.5f;
    chorusDepth = 0.02f;
    chorusFeedback = 0.3f;
    chorusWidth = 0.5f;
    tremoloPhase = 0.0f;
    chorusPhase = 0.0f;
    mainVolume = 1.0f;

    // Reset blues parameters
    bluesEnabled = false;
    bluesGain = 1.5f;
    bluesTone = 0.5f;
    bluesLevel = 0.8f;
    bluesFilterState[0] = bluesFilterState[1] = 0.0f;

    // Reset reverb parameters
    reverbEnabled = false;
    reverbSize = 0.5f;
    reverbDamping = 0.5f;
    reverbWidth = 1.0f;
    reverbMix = 0.3f;
    reverbInitialized = false; // Force reinitialize on next use
    warmEnabled = false;
    warmAmount = 0.5f;
    warmTone = 0.5f;
    warmSaturation = 0.3f;
    // Clear filter states
    for (int i = 0; i < 2; i++) {
        warmLowpassState[i] = 0.0f;
        warmHighpassState[i] = 0.0f;
        warmSaturatorState[i] = 0.0f;
    }

    // Reset compressor parameters
    compEnabled = false;
    compLevel = 1.0f;
    compTone = 0.5f;
    compAttackMs = 10.0f;
    compSustainMs = 100.0f;
    for (int i = 0; i < 2; i++) {
        compEnv[i] = 0.0f;
        compGainSmooth[i] = 0.0f;
        compLowState[i] = 0.0f;
    }

    // Reset wah parameters
    wahState.enabled = false;
    wahState.freq = 1000.0f;
    wahState.q = 10.0f;
    wahState.lfoRate = 0.5f;
    wahState.lfoDepth = 0.5f;
    wahState.mix = 0.5f;
    resetWahState();
}

void AudioProcessor::AudioLoop() {
    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!captureClient || !renderClient || !captureInterface || !renderInterface ||
        !captureFormat || !renderFormat) {
        std::cerr << "AudioLoop: Required interface is null, exiting thread." << std::endl;
        running = false;
        if (SUCCEEDED(hrCOM)) CoUninitialize();
        return;
    }

    HRESULT hr;
    hr = captureClient->Start();
    if (FAILED(hr)) {
        std::cerr << "AudioLoop: captureClient->Start() failed, exiting thread." << std::endl;
        running = false;
        if (SUCCEEDED(hrCOM)) CoUninitialize();
        return;
    }
    hr = renderClient->Start();
    if (FAILED(hr)) {
        std::cerr << "AudioLoop: renderClient->Start() failed, exiting thread." << std::endl;
        captureClient->Stop();
        running = false;
        if (SUCCEEDED(hrCOM)) CoUninitialize();
        return;
    }
    running = true;
    while (running) {
        if (!captureInterface || !renderInterface) {
            Sleep(1);
            continue;
        }
        UINT32 packetLength = 0;
        hr = captureInterface->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            std::cerr << "AudioLoop: GetNextPacketSize failed, breaking loop." << std::endl;
            break;
        }
        if (packetLength != 0) {
            BYTE* captureData = NULL;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;
            hr = captureInterface->GetBuffer(&captureData, &numFramesAvailable, &flags, NULL, NULL);
            if (FAILED(hr)) {
                std::cerr << "AudioLoop: GetBuffer failed, breaking loop." << std::endl;
                break;
            }
            if (captureData && numFramesAvailable > 0) {
                UINT32 numFramesPadding = 0;
                hr = renderClient->GetCurrentPadding(&numFramesPadding);
                if (FAILED(hr)) {
                    std::cerr << "AudioLoop: GetCurrentPadding failed, breaking loop." << std::endl;
                    captureInterface->ReleaseBuffer(numFramesAvailable);
                    break;
                }
                UINT32 numFramesAvailableForRender = renderBufferFrames - numFramesPadding;
                if (numFramesAvailableForRender >= numFramesAvailable) {
                    BYTE* renderData = NULL;
                    hr = renderInterface->GetBuffer(numFramesAvailable, &renderData);
                    if (FAILED(hr)) {
                        std::cerr << "AudioLoop: renderInterface->GetBuffer failed, breaking loop." << std::endl;
                        captureInterface->ReleaseBuffer(numFramesAvailable);
                        break;
                    }
                    if (renderData) {
                        memcpy(renderData, captureData, numFramesAvailable * captureFormat->nBlockAlign);
                        if (captureFormat && captureFormat->wBitsPerSample == 32 && renderData) {
                            ApplyTremolo((float*)renderData, numFramesAvailable);
                            if (chorusEnabled) {
                                ApplyChorus((float*)renderData, numFramesAvailable);
                            }
                            if (bluesEnabled) {
                                ApplyBluesDriver((float*)renderData, numFramesAvailable);
                            }
                            if (overdriveEnabled) {
                                ApplyOverdrive((float*)renderData, numFramesAvailable);
                            }
                            if (compEnabled) {
                                ApplyCompressor((float*)renderData, numFramesAvailable);
                            }
                            if (reverbEnabled) {
                                ApplyReverb((float*)renderData, numFramesAvailable);
                            }
                            if (warmEnabled) {
                                ApplyWarm((float*)renderData, numFramesAvailable);
                            }
                            // Wah processing (interleaved to per-channel wrapper)
                            if (wahState.enabled && captureFormat && captureFormat->wBitsPerSample == 32) {
                                int channels = captureFormat->nChannels;
                                if (channels >= 2) {
                                    std::vector<float> leftBuf(numFramesAvailable);
                                    std::vector<float> rightBuf(numFramesAvailable);
                                    float* out = (float*)renderData;
                                    for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                                        leftBuf[f] = out[f * channels];
                                        rightBuf[f] = out[f * channels + 1];
                                    }
                                    // processWah updates leftBuf and rightBuf in-place
                                    processWah(leftBuf.data(), rightBuf.data(), (int)numFramesAvailable);
                                    for (UINT32 f = 0; f < numFramesAvailable; ++f) {
                                        out[f * channels] = leftBuf[f];
                                        out[f * channels + 1] = rightBuf[f];
                                        // copy to additional channels if present
                                        for (int ch = 2; ch < channels; ++ch) {
                                            out[f * channels + ch] = out[f * channels + (ch % 2)];
                                        }
                                    }
                                }
                            }
                            // Apply main volume
                            float vol = mainVolume;
                            int totalSamples = numFramesAvailable * captureFormat->nChannels;
                            float* out = (float*)renderData;
                            for (int i = 0; i < totalSamples; ++i) {
                                out[i] *= vol;
                            }
                        }
                        renderInterface->ReleaseBuffer(numFramesAvailable, 0);
                    }
                }
                captureInterface->ReleaseBuffer(numFramesAvailable);
            }
        }
        else {
            Sleep(1);
        }
    }
    std::cerr << "AudioLoop: Exiting main loop." << std::endl;
    if (captureClient) captureClient->Stop();
    if (renderClient) renderClient->Stop();
    if (SUCCEEDED(hrCOM)) CoUninitialize();
}

void AudioProcessor::StartProcessing(const std::wstring& deviceId) {
    if (running) {
        Stop();
        Sleep(100);
    }
    Cleanup();
    // Ensure deviceEnumerator is initialized
    HRESULT hr = S_OK;
    if (!deviceEnumerator) {
        hr = Initialize();
        if (FAILED(hr)) {
            MessageBoxW(NULL, L"Failed to initialize audio system", L"Error", MB_OK);
            return;
        }
    }
    hr = SetupAudio(deviceId);
    if (SUCCEEDED(hr)) {
        std::thread audioThread(&AudioProcessor::AudioLoop, this);
        audioThread.detach();
    }
    else {
        Cleanup();
        MessageBoxW(NULL, L"Failed to setup audio device", L"Error", MB_OK);
    }
}

void AudioProcessor::Stop() {
    running = false;
    Sleep(100); // Give audio processing time to stop cleanly
}

void AudioProcessor::Cleanup() {
    running = false;
    if (captureClient) {
        captureClient->Stop();
    }
    if (renderClient) {
        renderClient->Stop();
    }
    if (captureInterface) {
        captureInterface->Release();
        captureInterface = NULL;
    }
    if (renderInterface) {
        renderInterface->Release();
        renderInterface = NULL;
    }
    if (captureClient) {
        captureClient->Release();
        captureClient = NULL;
    }
    if (renderClient) {
        renderClient->Release();
        renderClient = NULL;
    }
    if (captureFormat) {
        CoTaskMemFree(captureFormat);
        captureFormat = NULL;
    }
    if (renderFormat) {
        CoTaskMemFree(renderFormat);
        renderFormat = NULL;
    }
    if (captureDevice) {
        captureDevice->Release();
        captureDevice = NULL;
    }
    if (renderDevice) {
        renderDevice->Release();
        renderDevice = NULL;
    }
    if (deviceEnumerator) {
        deviceEnumerator->Release();
        deviceEnumerator = NULL;
    }
}

// Clamp helper for C++14
template<typename T>
T clamp(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

float AudioProcessor::GetReverbMix() const {
    return reverbMix;
}

void AudioProcessor::SetWarmEnabled(bool enabled) {
    warmEnabled = enabled;
}

void AudioProcessor::SetWarmAmount(float amount) {
    warmAmount = fmaxf(0.0f, fminf(1.0f, amount));
}

void AudioProcessor::SetWarmTone(float tone) {
    warmTone = fmaxf(0.0f, fminf(1.0f, tone));
}

void AudioProcessor::SetWarmSaturation(float saturation) {
    warmSaturation = fmaxf(0.0f, fminf(1.0f, saturation));
}

bool AudioProcessor::IsWarmEnabled() const {
    return warmEnabled;
}

float AudioProcessor::GetWarmAmount() const {
    return warmAmount;
}

float AudioProcessor::GetWarmTone() const {
    return warmTone;
}

float AudioProcessor::GetWarmSaturation() const {
    return warmSaturation;
}

void AudioProcessor::SetSampleRate(float rate) {
    sampleRate = rate;
}

float AudioProcessor::GetMainVolume() const {
    return mainVolume;
}

float AudioProcessor::GetChorusDepth() const {
    return chorusDepth;
}

float AudioProcessor::GetChorusRate() const {
    return chorusRate;
}

void AudioProcessor::SetBluesEnabled(bool enabled) {
    bluesEnabled = enabled;
}

void AudioProcessor::SetBluesGain(float gain) {
    bluesGain = gain;
}

void AudioProcessor::SetBluesTone(float tone) {
    bluesTone = fmaxf(0.0f, fminf(1.0f, tone));
}

void AudioProcessor::SetBluesLevel(float level) {
    bluesLevel = fmaxf(0.0f, fminf(2.0f, level));
}

bool AudioProcessor::IsBluesEnabled() const {
    return bluesEnabled;
}

float AudioProcessor::GetBluesGain() const {
    return bluesGain;
}

float AudioProcessor::GetBluesTone() const {
    return bluesTone;
}

float AudioProcessor::GetBluesLevel() const {
    return bluesLevel;
}

void AudioProcessor::SetReverbEnabled(bool enabled) { reverbEnabled = enabled; }
void AudioProcessor::SetReverbSize(float size) { reverbSize = fmaxf(0.0f, fminf(1.0f, size)); }
void AudioProcessor::SetReverbDamping(float damping) { reverbDamping = fmaxf(0.0f, fminf(1.0f, damping)); }
void AudioProcessor::SetReverbWidth(float width) { reverbWidth = fmaxf(0.0f, fminf(1.0f, width)); }
void AudioProcessor::SetReverbMix(float mix) { reverbMix = fmaxf(0.0f, fminf(1.0f, mix)); }

void AudioProcessor::SetCompressorEnabled(bool enabled) { compEnabled = enabled; }
void AudioProcessor::SetCompressorLevel(float level) { compLevel = fmaxf(0.0f, fminf(2.0f, level)); }
void AudioProcessor::SetCompressorTone(float tone) { compTone = fmaxf(0.0f, fminf(1.0f, tone)); }
void AudioProcessor::SetCompressorAttack(float ms) { compAttackMs = fmaxf(0.1f, ms); }
void AudioProcessor::SetCompressorSustain(float ms) { compSustainMs = fmaxf(1.0f, ms); }

void AudioProcessor::SetOverdriveEnabled(bool enabled) { overdriveEnabled = enabled; }
void AudioProcessor::SetOverdriveDrive(float drive) { overdriveDrive = drive; }
void AudioProcessor::SetOverdriveThreshold(float threshold) { overdriveThreshold = fmaxf(0.0f, fminf(1.0f, threshold)); }
void AudioProcessor::SetOverdriveTone(float tone) { overdriveTone = fmaxf(0.0f, fminf(1.0f, tone)); }
void AudioProcessor::SetOverdriveMix(float mix) { overdriveMix = fmaxf(0.0f, fminf(1.0f, mix)); }

void AudioProcessor::SetMainVolume(float vol) { mainVolume = vol; }

void AudioProcessor::SetChorusEnabled(bool enabled) { chorusEnabled = enabled; }
void AudioProcessor::SetChorusRate(float rate) { chorusRate = rate; }
void AudioProcessor::SetChorusDepth(float depth) { chorusDepth = depth; }
void AudioProcessor::SetChorusFeedback(float feedback) { chorusFeedback = feedback; }
void AudioProcessor::SetChorusWidth(float width) { chorusWidth = width; }
bool AudioProcessor::IsChorusEnabled() const { return chorusEnabled; }

void AudioProcessor::SetTremoloEnabled(bool enabled) { tremoloEnabled = enabled; }
void AudioProcessor::SetTremoloRate(float rate) { tremoloRate = rate; }
void AudioProcessor::SetTremoloDepth(float depth) { tremoloDepth = depth; }

bool AudioProcessor::IsOverdriveEnabled() const { return overdriveEnabled; }
float AudioProcessor::GetOverdriveDrive() const { return overdriveDrive; }
float AudioProcessor::GetOverdriveThreshold() const { return overdriveThreshold; }
float AudioProcessor::GetOverdriveTone() const { return overdriveTone; }
float AudioProcessor::GetOverdriveMix() const { return overdriveMix; }