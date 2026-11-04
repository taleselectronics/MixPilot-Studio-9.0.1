#pragma once
// Small standard-C++ DSP utilities, intentionally independent of JUCE so they
// can be compiled and tested on Linux as well as Windows/MSVC.
#include <algorithm>
#include <cmath>

namespace mixpilot {

// Used for parameter changes, not for audio analysis. Caller advances once
// per audio sample (not once per channel) to keep the stereo image stable.
class LinearParameterSmoother {
public:
    void reset(float initial) noexcept {
        current_ = std::isfinite(initial) ? initial : 0.0f;
        target_ = current_;
        increment_ = 0.0f;
        remaining_ = 0;
    }
    void setTarget(float target, int rampSamples) noexcept {
        if (!std::isfinite(target) || target == target_) return;
        target_ = target;
        remaining_ = std::max(0, rampSamples);
        if (remaining_ == 0) {
            current_ = target_;
            increment_ = 0.0f;
        } else {
            increment_ = (target_ - current_) / static_cast<float>(remaining_);
        }
    }
    float next() noexcept {
        if (remaining_ > 0) {
            current_ += increment_;
            if (--remaining_ == 0) current_ = target_;
        }
        return current_;
    }
    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }
private:
    float current_ = 0.0f, target_ = 0.0f, increment_ = 0.0f;
    int remaining_ = 0;
};

// IMPORTANT: absent an explicitly enabled limiter, there is no protective
// gain change. Preserves samples > 0 dBFS in the floating-point plugin path.
inline float linkedSamplePeakGuard(const float* samples, int channels,
                                   bool limiterActive, float ceiling) noexcept {
    if (!limiterActive || channels <= 0) return 1.0f;
    float peak = 0.0f;
    for (int c = 0; c < channels; ++c)
        peak = std::max(peak, std::abs(samples[c]));
    if (!(std::isfinite(peak)) || !(std::isfinite(ceiling)) || ceiling <= 0.0f)
        return 0.0f; // nonfinite signal must not propagate
    return peak > ceiling ? ceiling / peak : 1.0f;
}

// Estimate remaining delay echoes to about -60 dB amplitude (not a strict
// upper bound). Feedback changes and wet mix affect the audible duration.
inline double delayTailSeconds(double delaySeconds, double feedback) noexcept {
    if (!std::isfinite(delaySeconds) || delaySeconds <= 0.0) return 0.0;
    if (!std::isfinite(feedback) || feedback <= 0.001) return delaySeconds;
    feedback = std::clamp(feedback, 0.001, 0.95);
    return delaySeconds * (1.0 + std::log(0.001) / std::log(feedback));
}

inline double reverbTailSeconds(double decaySeconds, double predelaySeconds) noexcept {
    if (!std::isfinite(decaySeconds) || !std::isfinite(predelaySeconds)) return 0.0;
    return std::max(0.0, predelaySeconds) + std::max(0.0, decaySeconds) + 0.5;
}

} // namespace mixpilot
