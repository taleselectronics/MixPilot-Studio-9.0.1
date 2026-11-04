#include "../Source/Analysis/AudioIntelligenceEngine.h"
#include <stdexcept>
#include <string>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using mixpilot::intelligence::AudioIntelligenceEngine;
using mixpilot::intelligence::compare;
constexpr double pi = 3.14159265358979323846;
constexpr double sr = 48000.0;

static void feed(AudioIntelligenceEngine& engine, double duration,
                 double level = 1.0, bool addedPeak = false,
                 bool invertRight = false) {
    const int count = static_cast<int>(duration * sr);
    for (int i = 0; i < count; ++i) {
        const double t = i / sr;
        const double base = 0.11 * std::sin(2 * pi * 220 * t) +
                            0.06 * std::sin(2 * pi * 1100 * t) +
                            0.015 * std::sin(2 * pi * 4600 * t);
        const double extra = addedPeak ? 0.08 * std::sin(2 * pi * 850 * t) : 0.0;
        const float v = static_cast<float>(level * (base + extra));
        engine.push(v, invertRight ? -v : v);
    }
}

static void check(bool ok, const char* description) {
    if (!ok) throw std::runtime_error(description);
}

int main() {
    {
        AudioIntelligenceEngine silence(sr);
        for (int i = 0; i < 48000; ++i) silence.pushMono(0.0f);
        check(!silence.finish().valid, "silence should be invalid");
    }
    {
        AudioIntelligenceEngine first(sr), louder(sr);
        feed(first, 3.0);
        feed(louder, 3.0, 2.0);
        const auto a = first.finish(), b = louder.finish();
        check(a.valid && b.valid, "tone reports must be valid");
        check(a.activeWindows >= 4, "enough active windows");
        check(std::abs((b.rmsDbFS - a.rmsDbFS) - 6.0206f) < 0.05f, "gain change should be +6.02 dB");
        check(std::abs(a.crestFactorDb - b.crestFactorDb) < 0.05f, "crest factor invariant to gain");
        check(a.stereoCorrelation > 0.999f, "in-phase channels correlate");
        const auto match = compare(a, b);
        check(match.valid, "same signal level match valid");
        check(match.spectralDistanceDb < 0.15f, "same signal spectra match"); // gain independent
    }
    {
        AudioIntelligenceEngine positive(sr), negative(sr);
        feed(positive, 2.0);
        feed(negative, 2.0, 1.0, false, true);
        check(positive.finish().stereoCorrelation > 0.999f, "same-polarity stereo correlation");
        check(negative.finish().stereoCorrelation < -0.999f, "opposite-polarity stereo correlation");
    }
    {
        AudioIntelligenceEngine source(sr), reference(sr);
        feed(source, 4.0, 1.0, true); // added 850 Hz narrow energy
        feed(reference, 4.0);
        auto a = source.finish(), b = reference.finish();
        check(a.valid && b.valid, "source/reference reports valid");
        const auto d = compare(a, b);
        check(d.valid && d.spectralDistanceDb > 0.1f, "added 850Hz raises spectral distance");
        bool foundCut = false;
        for (const auto& x : d.suggestions)
            if (x.centerHz > 650 && x.centerHz < 1100 &&
                x.suggestedGainDb < 0 && x.confidence > 0)
                foundCut = true;
        check(foundCut, "850Hz excess should propose a cut");
    }
    {
        // Multiple analyzers are safe to instantiate: FFT arrays are on heap.
        std::vector<std::unique_ptr<AudioIntelligenceEngine>> engines;
        for (int i = 0; i < 8; ++i)
            engines.push_back(std::make_unique<AudioIntelligenceEngine>(sr));
        for (auto& e : engines) feed(*e, 1.0);
        for (auto& e : engines) check(e->finish().valid, "multiple analyzers should remain valid");
    }
    std::cout << "Audio Intelligence tests passed.\n";
}
