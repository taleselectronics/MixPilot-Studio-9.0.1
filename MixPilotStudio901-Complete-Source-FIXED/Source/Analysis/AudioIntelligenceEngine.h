#pragma once
// MixPilot Audio Intelligence 9.0.1
// Offline/captured-audio analysis ONLY. Never call push/finish on the audio thread.
// C++17, JUCE-independent.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mixpilot::intelligence {

inline constexpr std::size_t kTonalBands = 24;

struct ResonanceCandidate {
    float frequencyHz = 0.0f;
    float prominenceDb = 0.0f;
    float persistence = 0.0f; // fraction of active FFT windows
    float confidence = 0.0f;  // heuristic, not proof of a defect
};

struct AudioReport {
    bool valid = false;
    double sampleRate = 0.0;
    double analyzedSeconds = 0.0;
    std::uint64_t sampleCount = 0;
    unsigned activeWindows = 0;
    float rmsDbFS = -120.0f;
    float peakDbFS = -120.0f;
    float crestFactorDb = 0.0f;
    float stereoCorrelation = 0.0f; // 0 for mono/undefined
    float stereoBalanceDb = 0.0f;   // L relative to R, dB
    float sibilanceProxyDb = 0.0f;  // 4-10k versus 1-4k, NOT an S detector
    std::array<float, kTonalBands> tonalDb{};
    std::vector<ResonanceCandidate> resonanceCandidates;
};

struct TonalCorrection {
    float centerHz = 0.0f;
    float sourceMinusReferenceDb = 0.0f;
    float suggestedGainDb = 0.0f; // bounded, advisory only
    float confidence = 0.0f;
};

struct ReferenceComparison {
    bool valid = false;
    float spectralDistanceDb = 0.0f; // mean absolute normalized band delta
    float crestDifferenceDb = 0.0f;
    std::vector<TonalCorrection> suggestions;
};

class AudioIntelligenceEngine final {
public:
    explicit AudioIntelligenceEngine(double sampleRate);
    ~AudioIntelligenceEngine();
    AudioIntelligenceEngine(const AudioIntelligenceEngine&) = delete;
    AudioIntelligenceEngine& operator=(const AudioIntelligenceEngine&) = delete;
    AudioIntelligenceEngine(AudioIntelligenceEngine&&) noexcept;
    AudioIntelligenceEngine& operator=(AudioIntelligenceEngine&&) noexcept;

    // Feed original dry audio (or a reference file), not the processed plugin output.
    void push(float left, float right) noexcept;
    void pushMono(float sample) noexcept { push(sample, sample); }
    AudioReport finish() const; // Call off-thread, after capture ends.
    void reset() noexcept;

private:
    struct WorkingMemory;
    std::unique_ptr<WorkingMemory> work_;
    void analyzeWindow() noexcept;
};

// Positive source-reference difference means excessive RELATIVE band energy.
// Suggestions are hypotheses; human approval/processed-audio checks required.
ReferenceComparison compare(const AudioReport& source, const AudioReport& reference);

} // namespace mixpilot::intelligence
