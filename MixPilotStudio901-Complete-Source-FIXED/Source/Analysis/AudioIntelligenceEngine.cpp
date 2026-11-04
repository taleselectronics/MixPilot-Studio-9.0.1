#include "AudioIntelligenceEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <utility>

namespace mixpilot::intelligence {
namespace {
constexpr int kFFT = 8192;
constexpr int kHop = kFFT / 2;
constexpr double kPi = 3.14159265358979323846;
constexpr double kLowHz = 50.0;
constexpr double kHighHz = 16000.0;
constexpr double kSilencePower = 1.0e-7; // about -70 dBFS RMS^2

template <typename T> T clamp(T x, T lo, T hi) noexcept {
    return std::max(lo, std::min(x, hi));
}
float toDb(double amplitude) noexcept {
    return static_cast<float>(20.0 * std::log10(std::max(1.0e-6, amplitude)));
}
float powerDb(double power) noexcept {
    return static_cast<float>(10.0 * std::log10(std::max(1.0e-15, power)));
}
double bandCenter(std::size_t index) noexcept {
    return kLowHz * std::pow(kHighHz / kLowHz,
                    (static_cast<double>(index) + 0.5) / kTonalBands);
}
}

struct AudioIntelligenceEngine::WorkingMemory {
    explicit WorkingMemory(double sr) : sampleRate(sr) {
        for (int i = 0; i < kFFT; ++i) {
            window[static_cast<std::size_t>(i)] = static_cast<float>(
                0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFFT - 1)));
        }
        for (int j = 0; j < kFFT / 2; ++j) {
            auto hz = j * sampleRate / kFFT;
            if (hz >= kLowHz && hz < std::min(kHighHz, sampleRate * 0.49)) {
                const double p = std::log(hz / kLowHz) /
                                 std::log(kHighHz / kLowHz);
                bandIndex[static_cast<std::size_t>(j)] =
                    clamp(static_cast<int>(p * kTonalBands),
                          0, static_cast<int>(kTonalBands) - 1);
            } else {
                bandIndex[static_cast<std::size_t>(j)] = -1;
            }
        }
    }
    double sampleRate;
    std::array<float, kFFT> samples{};
    std::array<float, kFFT> window{};
    std::array<std::complex<float>, kFFT> spectrum{};
    std::array<int, kFFT / 2> bandIndex{};
    std::array<double, kFFT / 2> binPower{};
    std::array<unsigned, kFFT / 2> peakPersistence{};
    std::array<double, kTonalBands> bandPower{};
    std::array<unsigned, kTonalBands> bandBins{};
    int buffered = 0;
    std::uint64_t samplesSeen = 0;
    unsigned activeWindows = 0;
    double sumL2 = 0.0, sumR2 = 0.0, sumLR = 0.0;
    double sumMono2 = 0.0, peak = 0.0;
};

AudioIntelligenceEngine::AudioIntelligenceEngine(double sr)
    : work_(std::make_unique<WorkingMemory>(
          std::isfinite(sr) && sr >= 16000.0 ? sr : 48000.0)) {}
AudioIntelligenceEngine::~AudioIntelligenceEngine() = default;
AudioIntelligenceEngine::AudioIntelligenceEngine(AudioIntelligenceEngine&&) noexcept = default;
AudioIntelligenceEngine& AudioIntelligenceEngine::operator=(AudioIntelligenceEngine&&) noexcept = default;

void AudioIntelligenceEngine::reset() noexcept {
    if (!work_) return;
    // Reset in-place; avoids heap allocations in the reset path.
    std::fill(work_->samples.begin(), work_->samples.end(), 0.0f);
    std::fill(work_->spectrum.begin(), work_->spectrum.end(), std::complex<float>{});
    work_->binPower.fill(0.0);
    work_->peakPersistence.fill(0);
    work_->bandPower.fill(0.0);
    work_->bandBins.fill(0);
    work_->buffered = 0;
    work_->samplesSeen = 0;
    work_->activeWindows = 0;
    work_->sumL2 = work_->sumR2 = work_->sumLR = 0.0;
    work_->sumMono2 = work_->peak = 0.0;
}

void AudioIntelligenceEngine::push(float l, float r) noexcept {
    auto& w = *work_;
    if (!std::isfinite(l)) l = 0.0f;
    if (!std::isfinite(r)) r = 0.0f;
    const double mono = 0.5 * (static_cast<double>(l) + r);
    w.sumL2 += static_cast<double>(l) * l;
    w.sumR2 += static_cast<double>(r) * r;
    w.sumLR += static_cast<double>(l) * r;
    w.sumMono2 += 0.5 * (static_cast<double>(l) * l +
                          static_cast<double>(r) * r);
    w.peak = std::max(w.peak, std::max(std::abs(static_cast<double>(l)),
                                       std::abs(static_cast<double>(r))));
    ++w.samplesSeen;
    w.samples[static_cast<std::size_t>(w.buffered++)] = static_cast<float>(mono);
    if (w.buffered == kFFT) {
        analyzeWindow();
        std::copy(w.samples.begin() + kHop, w.samples.end(), w.samples.begin());
        w.buffered = kHop;
    }
}

void AudioIntelligenceEngine::analyzeWindow() noexcept {
    auto& w = *work_;
    double rms2 = 0.0;
    for (int i = 0; i < kFFT; ++i)
        rms2 += static_cast<double>(w.samples[static_cast<std::size_t>(i)]) *
                w.samples[static_cast<std::size_t>(i)];
    if (rms2 / kFFT < kSilencePower) return;

    // In-place radix-2 FFT: large scratch arrays are allocated on the heap.
    for (unsigned i = 0; i < kFFT; ++i) {
        unsigned n = i, rev = 0;
        for (int bit = 0; bit < 13; ++bit) {
            rev = (rev << 1u) | (n & 1u);
            n >>= 1u;
        }
        w.spectrum[rev] = {w.samples[i] * w.window[i], 0.0f};
    }
    for (int length = 2; length <= kFFT; length *= 2) {
        const int half = length / 2;
        for (int start = 0; start < kFFT; start += length) {
            for (int j = 0; j < half; ++j) {
                const float angle = static_cast<float>(-2.0 * kPi * j / length);
                const std::complex<float> factor(std::cos(angle), std::sin(angle));
                const auto u = w.spectrum[static_cast<std::size_t>(start + j)];
                const auto v = factor * w.spectrum[static_cast<std::size_t>(start + j + half)];
                w.spectrum[static_cast<std::size_t>(start + j)] = u + v;
                w.spectrum[static_cast<std::size_t>(start + j + half)] = u - v;
            }
        }
    }

    std::array<double, kFFT / 2> frame{}; // 32 KB temporary; Windows-safe.
    for (int j = 1; j < kFFT / 2; ++j) {
        const auto index = static_cast<std::size_t>(j);
        const double p = std::norm(w.spectrum[index]);
        frame[index] = p;
        w.binPower[index] += p;
        const int band = w.bandIndex[index];
        if (band >= 0) {
            w.bandPower[static_cast<std::size_t>(band)] += p;
            ++w.bandBins[static_cast<std::size_t>(band)];
        }
    }

    // Persistent narrow-band PEAK CANDIDATES. Voice harmonics can also trigger;
    // do not automatically notch based on this output.
    const int limit = std::min(kFFT / 2 - 26,
         static_cast<int>(10000.0 * kFFT / w.sampleRate));
    for (int j = std::max(16, static_cast<int>(90.0 * kFFT / w.sampleRate));
         j < limit; ++j) {
        const auto i = static_cast<std::size_t>(j);
        if (frame[i] <= frame[i - 1] || frame[i] <= frame[i + 1]) continue;
        const double flank = (frame[i - 14] + frame[i - 11] +
                               frame[i + 11] + frame[i + 14]) * 0.25;
        if (frame[i] > std::max(1.e-13, flank) * 6.31) // >8 dB prominence
            ++w.peakPersistence[i];
    }
    ++w.activeWindows;
}

AudioReport AudioIntelligenceEngine::finish() const {
    AudioReport report;
    const auto& w = *work_;
    report.sampleRate = w.sampleRate;
    report.sampleCount = w.samplesSeen;
    report.analyzedSeconds = static_cast<double>(w.samplesSeen) / w.sampleRate;
    report.activeWindows = w.activeWindows;
    if (w.samplesSeen == 0) return report;
    const double n = static_cast<double>(w.samplesSeen);
    report.rmsDbFS = toDb(std::sqrt(w.sumMono2 / n));
    report.peakDbFS = toDb(w.peak);
    report.crestFactorDb = report.peakDbFS - report.rmsDbFS;
    report.stereoBalanceDb = clamp(powerDb(w.sumL2 / std::max(1.e-20, w.sumR2)),
                                   -40.0f, 40.0f);
    if (w.sumL2 > 1.e-10 && w.sumR2 > 1.e-10)
        report.stereoCorrelation = static_cast<float>(clamp(
            w.sumLR / std::sqrt(w.sumL2 * w.sumR2), -1.0, 1.0));
    if (w.activeWindows < 4) return report;

    std::array<double, kTonalBands> average{};
    for (std::size_t b = 0; b < kTonalBands; ++b) {
        if (w.bandBins[b] == 0) return report;
        average[b] = std::max(1.e-15, w.bandPower[b] / w.bandBins[b]);
    }
    // Normalize each profile to its overall spectral level; this avoids
    // treating plain recording-volume differences as a tonal problem.
    double sumLog = 0.0;
    for (const double p : average) sumLog += powerDb(p);
    const double centerDb = sumLog / kTonalBands;
    for (std::size_t b = 0; b < kTonalBands; ++b)
        report.tonalDb[b] = clamp(static_cast<float>(powerDb(average[b]) - centerDb),
                                  -48.0f, 30.0f);

    double sib = 0.0, presence = 0.0;
    int sibBands = 0, presenceBands = 0;
    for (std::size_t b = 0; b < kTonalBands; ++b) {
        const double f = bandCenter(b);
        if (f >= 4000 && f <= 10000) { sib += average[b]; ++sibBands; }
        if (f >= 1000 && f < 4000) { presence += average[b]; ++presenceBands; }
    }
    if (sibBands && presenceBands)
        report.sibilanceProxyDb = clamp(powerDb((sib / sibBands) /
             std::max(1.e-15, presence / presenceBands)), -48.0f, 30.0f);

    struct RankedPeak { ResonanceCandidate item; float score; };
    std::vector<RankedPeak> peaks;
    const int maxBin = std::min(kFFT / 2 - 26,
        static_cast<int>(10000.0 * kFFT / w.sampleRate));
    for (int j = std::max(16, static_cast<int>(90.0 * kFFT / w.sampleRate));
         j < maxBin; ++j) {
        const auto idx = static_cast<std::size_t>(j);
        const float persistence = static_cast<float>(w.peakPersistence[idx]) /
                                  static_cast<float>(w.activeWindows);
        if (persistence < 0.20f) continue;
        const double p = w.binPower[idx] / w.activeWindows;
        const double flank = (w.binPower[idx - 14] + w.binPower[idx - 11] +
                              w.binPower[idx + 11] + w.binPower[idx + 14]) /
                              (4.0 * w.activeWindows);
        const float prominence = powerDb(p / std::max(1.e-15, flank));
        if (prominence < 5.5f) continue;
        ResonanceCandidate c;
        c.frequencyHz = static_cast<float>(j * w.sampleRate / kFFT);
        c.prominenceDb = clamp(prominence, 0.0f, 24.0f);
        c.persistence = persistence;
        c.confidence = clamp(0.6f * persistence +
                          0.4f * (prominence - 5.5f) / 15.0f, 0.0f, 0.85f);
        peaks.push_back({c, c.confidence * c.prominenceDb});
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    for (const auto& p : peaks) {
        bool close = false;
        for (const auto& previous : report.resonanceCandidates)
            if (std::abs(std::log2(p.item.frequencyHz / previous.frequencyHz)) < 0.12)
                close = true;
        if (!close) report.resonanceCandidates.push_back(p.item);
        if (report.resonanceCandidates.size() == 5) break;
    }
    report.valid = true;
    return report;
}

ReferenceComparison compare(const AudioReport& source, const AudioReport& reference) {
    ReferenceComparison output;
    if (!source.valid || !reference.valid) return output;
    if (source.activeWindows < 4 || reference.activeWindows < 4) return output;
    output.valid = true;
    output.crestDifferenceDb = source.crestFactorDb - reference.crestFactorDb;
    float distance = 0.0f;
    const float evidence = clamp(static_cast<float>(
         std::min(source.activeWindows, reference.activeWindows) / 30.0),
         0.2f, 1.0f);
    for (std::size_t b = 0; b < kTonalBands; ++b) {
        const float delta = source.tonalDb[b] - reference.tonalDb[b];
        distance += std::abs(delta);
        if (std::abs(delta) < 2.5f) continue;
        // Strong deviations may reflect different singers or microphones.
        // Suggestions remain small and are not auto-applied.
        const float gain = clamp(-0.45f * delta, -3.0f, 3.0f);
        TonalCorrection correction;
        correction.centerHz = static_cast<float>(bandCenter(b));
        correction.sourceMinusReferenceDb = delta;
        correction.suggestedGainDb = gain;
        correction.confidence = clamp(evidence *
           (std::abs(delta) / (std::abs(delta) + 5.0f)), 0.0f, 0.80f);
        output.suggestions.push_back(correction);
    }
    output.spectralDistanceDb = distance / kTonalBands;
    std::sort(output.suggestions.begin(), output.suggestions.end(),
       [](const auto& a, const auto& b) {
          return std::abs(a.sourceMinusReferenceDb) >
                 std::abs(b.sourceMinusReferenceDb);
       });
    if (output.suggestions.size() > 6) output.suggestions.resize(6);
    return output;
}

} // namespace mixpilot::intelligence
