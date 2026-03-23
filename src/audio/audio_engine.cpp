#include "audio/audio_engine.h"
#include <cstring>
#include <iostream>
#include <cctype>
#include <algorithm>
#include <chrono>

#ifdef __EMSCRIPTEN__
#include <SDL.h>
#else
#ifdef _WIN32
#include <pa_win_wasapi.h>
#endif
#endif

namespace GuitarAmp {

AudioEngine::AudioEngine() {
    process_buffer_.resize(MAX_BUFFER_SIZE, 0.0f);
}

AudioEngine::~AudioEngine() {
    shutdown();
}

#ifndef __EMSCRIPTEN__

bool AudioEngine::is_usb_device_name(const std::string& name) {
    // Convert to lowercase for matching
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Common USB guitar cable / interface identifiers
    static const char* usb_keywords[] = {
        "usb", "guitar", "guitar link", "irig", "scarlett",
        "behringer", "focusrite", "presonus", "steinberg",
        "audio interface", "line 6", "rocksmith", "umc",
        "um2", "uphoria", "podcast", "xenyx"
    };

    for (const auto* keyword : usb_keywords) {
        if (lower.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static int get_host_api_priority(PaHostApiTypeId type) {
    // Platform-aware priority: pick the lowest-latency host API per OS
#if defined(__linux__)
    switch (type) {
        case paJACK:          return 100;  // JACK is king on Linux
        case paALSA:          return 70;
        default:              return 10;
    }
#elif defined(_WIN32)
    switch (type) {
        case paASIO:          return 100;  // ASIO still best if available
        case paWASAPI:        return 90;   // WASAPI Exclusive is excellent
        case paDirectSound:   return 40;
        case paMME:           return 10;
        default:              return 20;
    }
#elif defined(__APPLE__)
    switch (type) {
        case paCoreAudio:     return 100;
        default:              return 30;
    }
#else
    (void)type;
    return 30;
#endif
}

static bool is_projector_or_hdmi(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower.find("epson") != std::string::npos
        || lower.find("projector") != std::string::npos
        || lower.find("hdmi") != std::string::npos
        || lower.find("displayport") != std::string::npos;
}

void AudioEngine::auto_detect_devices() {
    int device_count = Pa_GetDeviceCount();
    int num_apis = Pa_GetHostApiCount();

    // Phase 1: Print all devices
    std::cout << "\nDetected audio devices:" << std::endl;
    for (int i = 0; i < device_count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        bool is_usb = is_usb_device_name(info->name);
        const PaHostApiInfo* api = Pa_GetHostApiInfo(info->hostApi);
        const char* api_name = api ? api->name : "Unknown";
        if (info->maxInputChannels > 0) {
            std::cout << "  [IN]  " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
        if (info->maxOutputChannels > 0) {
            std::cout << "  [OUT] " << info->name
                      << " (" << api_name << ")"
                      << (is_usb ? " [USB]" : "") << std::endl;
        }
    }

    // Phase 2: For each host API (ranked by priority), find the best
    // USB input + non-USB/non-projector output PAIR on the SAME API.
    // PortAudio requires input and output to be on the same host API.

    struct ApiCandidate {
        int api_index;
        int priority;
        int usb_input;       // device index, -1 if none
        int best_output;     // device index, -1 if none
        std::string api_name;
    };

    std::vector<ApiCandidate> candidates;
    for (int a = 0; a < num_apis; ++a) {
        const PaHostApiInfo* api = Pa_GetHostApiInfo(a);
        if (!api) continue;

        ApiCandidate c;
        c.api_index = a;
        c.priority = get_host_api_priority(api->type);
        c.usb_input = -1;
        c.best_output = -1;
        c.api_name = api->name;

        // Scan devices belonging to this API
        for (int d = 0; d < api->deviceCount; ++d) {
            int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(a, d);
            const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
            if (!info) continue;

            bool is_usb = is_usb_device_name(info->name);

            // Best USB input on this API
            if (is_usb && info->maxInputChannels > 0 && c.usb_input < 0) {
                c.usb_input = dev_idx;
            }

            // Best non-USB, non-projector output on this API
            if (!is_usb && !is_projector_or_hdmi(info->name)
                && info->maxOutputChannels > 0 && c.best_output < 0) {
                c.best_output = dev_idx;
            }
        }

        // If no good output found, accept any non-USB output (including projector)
        if (c.best_output < 0) {
            for (int d = 0; d < api->deviceCount; ++d) {
                int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(a, d);
                const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
                if (!info) continue;
                if (!is_usb_device_name(info->name) && info->maxOutputChannels > 0) {
                    c.best_output = dev_idx;
                    break;
                }
            }
        }

        candidates.push_back(c);
    }

    // Sort by priority (highest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const ApiCandidate& a, const ApiCandidate& b) {
                  return a.priority > b.priority;
              });

    // Phase 3: Pick the best API that has BOTH a USB input AND an output
    bool found_pair = false;
    for (auto& c : candidates) {
        if (c.usb_input >= 0 && c.best_output >= 0) {
            input_device_ = c.usb_input;
            output_device_ = c.best_output;

            const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device_);
            const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device_);

            std::cout << "\n>> Auto-selected (same API: " << c.api_name << "):" << std::endl;
            std::cout << "   INPUT:  " << in_info->name << " [USB Guitar Cable]" << std::endl;
            std::cout << "   OUTPUT: " << out_info->name << " [Speakers]" << std::endl;

            if (in_info->defaultSampleRate > 0) {
                sample_rate_ = static_cast<int>(in_info->defaultSampleRate);
                std::cout << "   Rate:   " << sample_rate_ << " Hz (device native)" << std::endl;
            }

            found_pair = true;
            break;
        }
    }

    // Fallback: no USB input found — pick the best API with any input + output
    if (!found_pair) {
        for (auto& c : candidates) {
            if (c.best_output >= 0) {
                // Find any input on this API
                const PaHostApiInfo* api = Pa_GetHostApiInfo(c.api_index);
                for (int d = 0; d < api->deviceCount; ++d) {
                    int dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(c.api_index, d);
                    const PaDeviceInfo* info = Pa_GetDeviceInfo(dev_idx);
                    if (info && info->maxInputChannels > 0) {
                        input_device_ = dev_idx;
                        output_device_ = c.best_output;

                        std::cout << "\n>> No USB guitar cable detected." << std::endl;
                        std::cout << "   Using " << c.api_name << " defaults:" << std::endl;
                        std::cout << "   INPUT:  " << info->name << std::endl;
                        std::cout << "   OUTPUT: " << Pa_GetDeviceInfo(output_device_)->name << std::endl;

                        found_pair = true;
                        break;
                    }
                }
                if (found_pair) break;
            }
        }
    }

    // Last resort: system defaults
    if (!found_pair) {
        input_device_ = Pa_GetDefaultInputDevice();
        output_device_ = Pa_GetDefaultOutputDevice();
        std::cout << "\n>> Using system default input/output devices." << std::endl;
    }
}

#endif // !__EMSCRIPTEN__ (PortAudio helper functions)

// =============================================================================
// Platform-specific: initialize / shutdown / start / stop / device management
// =============================================================================

#ifndef __EMSCRIPTEN__
// --- Native PortAudio implementation ---

bool AudioEngine::initialize() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init failed: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }
    initialized_ = true;

    // Auto-detect USB guitar cable for input, laptop speakers for output
    auto_detect_devices();

    return true;
}

void AudioEngine::shutdown() {
    stop();
    if (initialized_) {
        Pa_Terminate();
        initialized_ = false;
    }
}

bool AudioEngine::start() {
    if (!initialized_ || running_) return false;

    const PaDeviceInfo* in_dev = Pa_GetDeviceInfo(input_device_);
    const PaDeviceInfo* out_dev = Pa_GetDeviceInfo(output_device_);
    (void)in_dev; (void)out_dev;  // used only in platform-specific blocks below

    // Compute desired latency from the user's buffer size setting.
    // Do NOT use defaultLowInputLatency — on macOS built-in mic it can be >200ms.
    double desired_latency = static_cast<double>(buffer_size_) / sample_rate_;

    PaStreamParameters input_params;
    input_params.device = input_device_;
    input_params.channelCount = 1;
    input_params.sampleFormat = paFloat32;
    input_params.suggestedLatency = desired_latency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters output_params;
    output_params.device = output_device_;
    output_params.channelCount = 1;
    output_params.sampleFormat = paFloat32;
    output_params.suggestedLatency = desired_latency;
    output_params.hostApiSpecificStreamInfo = nullptr;

#ifdef _WIN32
    // WASAPI Exclusive Mode: bypass the Windows audio mixer for minimal latency
    PaWasapiStreamInfo wasapi_in_info = {};
    PaWasapiStreamInfo wasapi_out_info = {};
    if (in_dev) {
        const PaHostApiInfo* api = Pa_GetHostApiInfo(in_dev->hostApi);
        if (api && api->type == paWASAPI) {
            wasapi_in_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_in_info.hostApiType = paWASAPI;
            wasapi_in_info.version = 1;
            wasapi_in_info.flags = paWinWasapiExclusive;
            input_params.hostApiSpecificStreamInfo = &wasapi_in_info;

            wasapi_out_info.size = sizeof(PaWasapiStreamInfo);
            wasapi_out_info.hostApiType = paWASAPI;
            wasapi_out_info.version = 1;
            wasapi_out_info.flags = paWinWasapiExclusive;
            output_params.hostApiSpecificStreamInfo = &wasapi_out_info;

            std::cout << "  Using WASAPI Exclusive Mode" << std::endl;
        }
    }
#endif

    // Use the user's configured buffer size for minimum latency
    unsigned long frames = static_cast<unsigned long>(buffer_size_);

    PaError err = Pa_OpenStream(
        &stream_,
        &input_params,
        &output_params,
        sample_rate_,
        frames,
        paClipOff | paDitherOff,
        audio_callback,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
        // Fall back: try with explicit buffer size
        std::cerr << "Retrying with buffer size " << buffer_size_ << "..." << std::endl;
        err = Pa_OpenStream(
            &stream_,
            &input_params,
            &output_params,
            sample_rate_,
            buffer_size_,
            paClipOff | paDitherOff,
            audio_callback,
            this
        );
        if (err != paNoError) {
            std::cerr << "Failed to open stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream_);
        stream_ = nullptr;
        return false;
    }

    running_ = true;
    const PaStreamInfo* si = Pa_GetStreamInfo(stream_);
    const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device_);
    const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device_);
    std::cout << "Audio stream started:" << std::endl;
    std::cout << "  Input:   " << (in_info ? in_info->name : "Unknown") << std::endl;
    std::cout << "  Output:  " << (out_info ? out_info->name : "Unknown") << std::endl;
    std::cout << "  Rate:    " << (si ? si->sampleRate : sample_rate_) << " Hz" << std::endl;
    if (si) {
        std::cout << "  Latency: in=" << (si->inputLatency * 1000.0) << " ms"
                  << "  out=" << (si->outputLatency * 1000.0) << " ms" << std::endl;
    }
    return true;
}

void AudioEngine::stop() {
    if (stream_) {
        if (running_) {
            Pa_StopStream(stream_);
            running_ = false;
        }
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
}

bool AudioEngine::restart() {
    stop();
    bool ok = start();
    if (!ok) {
        last_error_ = "Failed to restart audio stream. Check device settings.";
        std::cerr << "[Amplitron] " << last_error_ << std::endl;
    } else {
        last_error_.clear();
    }
    return ok;
}

bool AudioEngine::devices_share_host_api(int input_dev, int output_dev) const {
    const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_dev);
    const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_dev);
    if (!in_info || !out_info) return false;
    return in_info->hostApi == out_info->hostApi;
}

std::string AudioEngine::get_input_device_name() const {
    if (input_device_ >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(input_device_);
        if (info) return info->name;
    }
    return "None";
}

std::string AudioEngine::get_output_device_name() const {
    if (output_device_ >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(output_device_);
        if (info) return info->name;
    }
    return "None";
}

std::vector<AudioDeviceInfo> AudioEngine::get_input_devices() const {
    std::vector<AudioDeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            devices.push_back({
                i, info->name,
                info->maxInputChannels, info->maxOutputChannels,
                info->defaultSampleRate,
                is_usb_device_name(info->name)
            });
        }
    }
    return devices;
}

std::vector<AudioDeviceInfo> AudioEngine::get_output_devices() const {
    std::vector<AudioDeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            devices.push_back({
                i, info->name,
                info->maxInputChannels, info->maxOutputChannels,
                info->defaultSampleRate,
                is_usb_device_name(info->name)
            });
        }
    }
    return devices;
}

bool AudioEngine::set_input_device(int device_index) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    if (!info || info->maxInputChannels < 1) {
        last_error_ = "Invalid input device.";
        return false;
    }

    // Warn about host API mismatch (PortAudio requires same API)
    if (!devices_share_host_api(device_index, output_device_)) {
        const PaDeviceInfo* out_info = Pa_GetDeviceInfo(output_device_);
        const PaHostApiInfo* in_api = Pa_GetHostApiInfo(info->hostApi);
        const PaHostApiInfo* out_api = out_info ? Pa_GetHostApiInfo(out_info->hostApi) : nullptr;
        std::cerr << "[Amplitron] Warning: Input (" << (in_api ? in_api->name : "?") 
                  << ") and output (" << (out_api ? out_api->name : "?") 
                  << ") are on different host APIs. Stream may fail." << std::endl;
    }

    int prev_device = input_device_;
    bool was_running = running_;
    if (was_running) stop();
    input_device_ = device_index;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed to start with new input device. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            input_device_ = prev_device;
            start();  // restore previous
            return false;
        }
        last_error_.clear();
    }
    return true;
}

bool AudioEngine::set_output_device(int device_index) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device_index);
    if (!info || info->maxOutputChannels < 1) {
        last_error_ = "Invalid output device.";
        return false;
    }

    if (!devices_share_host_api(input_device_, device_index)) {
        const PaDeviceInfo* in_info = Pa_GetDeviceInfo(input_device_);
        const PaHostApiInfo* in_api = in_info ? Pa_GetHostApiInfo(in_info->hostApi) : nullptr;
        const PaHostApiInfo* out_api = Pa_GetHostApiInfo(info->hostApi);
        std::cerr << "[Amplitron] Warning: Input (" << (in_api ? in_api->name : "?") 
                  << ") and output (" << (out_api ? out_api->name : "?") 
                  << ") are on different host APIs. Stream may fail." << std::endl;
    }

    int prev_device = output_device_;
    bool was_running = running_;
    if (was_running) stop();
    output_device_ = device_index;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed to start with new output device. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            output_device_ = prev_device;
            start();
            return false;
        }
        last_error_.clear();
    }
    return true;
}

#else // __EMSCRIPTEN__
// --- Web / Emscripten SDL_Audio implementation ---

void AudioEngine::sdl_audio_callback(void* userdata, Uint8* stream, int len) {
    auto* engine = static_cast<AudioEngine*>(userdata);
    auto* out = reinterpret_cast<float*>(stream);
    int frame_count = len / static_cast<int>(sizeof(float));

    // Web demo: process effects on silence (no mic input by default).
    // The DSP chain still runs. A real input would require getUserMedia.
    std::memset(out, 0, static_cast<size_t>(len));
    engine->process_audio(out, out, frame_count);
}

bool AudioEngine::initialize() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    initialized_ = true;
    std::cout << "[Web] Audio subsystem initialized." << std::endl;
    return true;
}

void AudioEngine::shutdown() {
    stop();
    if (initialized_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_ = false;
    }
}

bool AudioEngine::start() {
    if (!initialized_ || running_) return false;

    // Web Audio createScriptProcessor requires buffer size 256–16384
    int web_buffer = buffer_size_ < 256 ? 256 : buffer_size_;

    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = sample_rate_;
    want.format = AUDIO_F32;
    want.channels = 1;
    want.samples = static_cast<Uint16>(web_buffer);
    want.callback = sdl_audio_callback;
    want.userdata = this;

    sdl_audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (sdl_audio_device_ == 0) {
        std::cerr << "[Web] Failed to open audio: " << SDL_GetError() << std::endl;
        return false;
    }

    sample_rate_ = have.freq;
    buffer_size_ = have.samples;

    SDL_PauseAudioDevice(sdl_audio_device_, 0);  // start playback
    running_ = true;

    std::cout << "[Web] Audio stream started:" << std::endl;
    std::cout << "  Rate:   " << sample_rate_ << " Hz" << std::endl;
    std::cout << "  Buffer: " << buffer_size_ << " samples" << std::endl;
    return true;
}

void AudioEngine::stop() {
    if (sdl_audio_device_ != 0) {
        if (running_) {
            SDL_PauseAudioDevice(sdl_audio_device_, 1);
            running_ = false;
        }
        SDL_CloseAudioDevice(sdl_audio_device_);
        sdl_audio_device_ = 0;
    }
}

bool AudioEngine::restart() {
    stop();
    bool ok = start();
    if (!ok) {
        last_error_ = "Failed to restart audio.";
    } else {
        last_error_.clear();
    }
    return ok;
}

// Web stubs for device management (browser handles devices)
std::string AudioEngine::get_input_device_name() const { return "Browser Microphone"; }
std::string AudioEngine::get_output_device_name() const { return "Browser Audio Output"; }
std::vector<AudioDeviceInfo> AudioEngine::get_input_devices() const {
    return {{0, "Browser Microphone", 1, 0, 48000.0, false}};
}
std::vector<AudioDeviceInfo> AudioEngine::get_output_devices() const {
    return {{0, "Browser Audio Output", 0, 1, 48000.0, false}};
}
bool AudioEngine::set_input_device(int) { return true; }
bool AudioEngine::set_output_device(int) { return true; }

#endif // __EMSCRIPTEN__

void AudioEngine::add_effect(std::shared_ptr<Effect> effect) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effect->set_sample_rate(sample_rate_);
    effects_.push_back(std::move(effect));
}

void AudioEngine::remove_effect(int index) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    if (index >= 0 && index < static_cast<int>(effects_.size())) {
        effects_.erase(effects_.begin() + index);
    }
}

void AudioEngine::move_effect(int from, int to) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    int n = static_cast<int>(effects_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    auto effect = effects_[from];
    effects_.erase(effects_.begin() + from);
    effects_.insert(effects_.begin() + to, effect);
}

void AudioEngine::set_buffer_size(int size) {
    size = std::max(MIN_BUFFER_SIZE, std::min(MAX_BUFFER_SIZE, size));
    int prev_size = buffer_size_;
    bool was_running = running_;
    if (was_running) stop();
    buffer_size_ = size;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with buffer size " + std::to_string(size) + ". Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            buffer_size_ = prev_size;
            start();
        } else {
            last_error_.clear();
        }
    }
}

void AudioEngine::set_sample_rate(int rate) {
    int prev_rate = sample_rate_;
    bool was_running = running_;
    if (was_running) stop();
    sample_rate_ = rate;
    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        for (auto& fx : effects_) {
            fx->set_sample_rate(rate);
            fx->reset();
        }
    }
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with sample rate " + std::to_string(rate) + " Hz. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            sample_rate_ = prev_rate;
            std::lock_guard<std::mutex> lock(effect_mutex_);
            for (auto& fx : effects_) {
                fx->set_sample_rate(prev_rate);
                fx->reset();
            }
            start();
        } else {
            last_error_.clear();
        }
    }
}

#ifndef __EMSCRIPTEN__
int AudioEngine::audio_callback(const void* input, void* output,
                                 unsigned long frame_count,
                                 const PaStreamCallbackTimeInfo* /*time_info*/,
                                 PaStreamCallbackFlags /*status_flags*/,
                                 void* user_data) {
    auto* engine = static_cast<AudioEngine*>(user_data);
    const auto* in = static_cast<const float*>(input);
    auto* out = static_cast<float*>(output);

    if (!in || !out) {
        if (out) std::memset(out, 0, frame_count * sizeof(float));
        return paContinue;
    }

    engine->process_audio(in, out, static_cast<int>(frame_count));
    return paContinue;
}
#endif

void AudioEngine::process_audio(const float* input, float* output, int frame_count) {
    auto t_start = std::chrono::steady_clock::now();

    // Safety: ensure process buffer is large enough
    if (frame_count > static_cast<int>(process_buffer_.size())) {
        process_buffer_.resize(frame_count, 0.0f);
    }

    // Drain lock-free command queue (GUI -> Audio)
    drain_commands();

    // Copy input to processing buffer with gain
    float in_gain = input_gain_.load(std::memory_order_relaxed);
    float peak_in = 0.0f;
    for (int i = 0; i < frame_count; ++i) {
        process_buffer_[i] = input[i] * in_gain;
        float abs_val = std::fabs(process_buffer_[i]);
        if (abs_val > peak_in) peak_in = abs_val;
    }
    input_level_.store(peak_in);

    // Process through effect chain
    // Structural changes (add/remove/move) still use try_lock for safety;
    // parameter changes are fully lock-free via the SPSC queue above.
    if (effect_mutex_.try_lock()) {
        for (auto& fx : effects_) {
            if (fx->is_enabled()) {
                fx->process(process_buffer_.data(), frame_count);
            }
        }
        effect_mutex_.unlock();
    }

    // Copy to output with gain
    float out_gain = output_gain_.load(std::memory_order_relaxed);
    float peak_out = 0.0f;
    for (int i = 0; i < frame_count; ++i) {
        output[i] = process_buffer_[i] * out_gain;
        output[i] = clamp(output[i], -1.0f, 1.0f);
        float abs_val = std::fabs(output[i]);
        if (abs_val > peak_out) peak_out = abs_val;
    }
    output_level_.store(peak_out);

    // Record the processed output
    if (recorder_.is_recording()) {
        recorder_.write_samples(output, frame_count);
    }

    // CPU load measurement
    auto t_end = std::chrono::steady_clock::now();
    float duration_us = std::chrono::duration<float, std::micro>(t_end - t_start).count();
    callback_duration_us_.store(duration_us, std::memory_order_relaxed);
    float budget_us = (static_cast<float>(frame_count) / sample_rate_) * 1e6f;
    cpu_load_.store(duration_us / budget_us, std::memory_order_relaxed);
}

// --- Lock-free command drain (called from audio thread) ---

void AudioEngine::drain_commands() {
    AudioCommand cmd;
    while (command_queue_.try_pop(cmd)) {
        switch (cmd.type) {
            case AudioCommand::SetEffectParam:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    auto& params = effects_[cmd.effect_index]->params();
                    if (cmd.param_index >= 0 &&
                        cmd.param_index < static_cast<int>(params.size())) {
                        params[cmd.param_index].value = cmd.value;
                    }
                }
                break;
            case AudioCommand::SetEffectEnabled:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_enabled(cmd.value > 0.5f);
                }
                break;
            case AudioCommand::SetEffectMix:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_mix(cmd.value);
                }
                break;
            case AudioCommand::SetInputGain:
                input_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            case AudioCommand::SetOutputGain:
                output_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

// --- GUI-thread push methods ---

void AudioEngine::set_input_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetInputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    input_gain_.store(gain, std::memory_order_relaxed);  // immediate read-back for GUI
}

void AudioEngine::set_output_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetOutputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    output_gain_.store(gain, std::memory_order_relaxed);
}

void AudioEngine::push_param_change(int effect_index, int param_index, float value) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectParam;
    cmd.effect_index = effect_index;
    cmd.param_index = param_index;
    cmd.value = value;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_enabled(int effect_index, float enabled) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectEnabled;
    cmd.effect_index = effect_index;
    cmd.value = enabled;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_mix(int effect_index, float mix) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectMix;
    cmd.effect_index = effect_index;
    cmd.value = mix;
    command_queue_.try_push(cmd);
}

// --- Buffer auto-tuning ---

int AudioEngine::get_suggested_buffer_size() const {
    float load = cpu_load_.load(std::memory_order_relaxed);
    int current = buffer_size_;

    // If consistently overloaded (>80%), suggest next larger buffer
    if (load > 0.80f) {
        if (current < MAX_BUFFER_SIZE) {
            return std::min(current * 2, MAX_BUFFER_SIZE);
        }
    }
    // If consistently underloaded (<30%), suggest next smaller buffer
    if (load < 0.30f) {
        if (current > MIN_BUFFER_SIZE) {
            return std::max(current / 2, MIN_BUFFER_SIZE);
        }
    }
    return current;  // no change
}

} // namespace GuitarAmp
