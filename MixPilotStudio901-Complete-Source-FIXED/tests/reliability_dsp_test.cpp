#include "../Source/ReliabilityDSP.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

// Use runtime checks rather than assert(): MSVC Release sets NDEBUG, which
// would otherwise remove every test condition from the GitHub Actions binary.
static void check(bool ok, const char* description) {
    if (!ok) {
        std::cerr << "FAILED: " << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    using namespace mixpilot;
    // No implicit -1 dBFS limiting when the limiter module is absent.
    const float loud[] = {1.5f, -1.25f};
    check(linkedSamplePeakGuard(loud, 2, false, 0.89125f) == 1.0f,
          "disabled limiter should produce unity gain");
    check(std::abs(linkedSamplePeakGuard(loud, 2, true, 0.9f) - 0.6f) < 1.e-6f,
          "enabled limiter guard should control a peak");
    const float quiet[] = {0.1f, -0.3f};
    check(linkedSamplePeakGuard(quiet, 2, true, 0.9f) == 1.0f,
          "signal below ceiling should be unchanged");
    const float gain = linkedSamplePeakGuard(loud, 2, true, 0.9f);
    check(std::abs(loud[0] * gain - 0.9f) < 1.e-6f,
          "linked guard controls positive channel");
    check(std::abs(loud[1] * gain + 0.75f) < 1.e-6f,
          "linked guard applies identical gain to negative channel");

    LinearParameterSmoother s;
    s.reset(1.0f);
    s.setTarget(0.0f, 100);
    for (int i = 0; i < 99; ++i) {
        const auto v = s.next();
        check(v >= -0.001f && v <= 1.001f, "smoothing stays bounded");
        s.setTarget(0.0f, 100); // same target must not restart the ramp
    }
    check(std::abs(s.next()) < 1.e-5f, "smoothing reaches the target");
    s.setTarget(0.75f, 25);
    for (int i = 0; i < 25; ++i) s.next();
    check(std::abs(s.current() - 0.75f) < 1.e-5f, "smoothing handles a new target");
    s.setTarget(0.25f, 0);
    check(s.current() == 0.25f, "zero-duration smoothing updates immediately");

    check(delayTailSeconds(0.25, 0.0) == 0.25, "zero feedback delay tail");
    check(delayTailSeconds(0.25, 0.6) > delayTailSeconds(0.25, 0.2),
          "more feedback implies a longer estimated tail");
    check(reverbTailSeconds(2.0, 0.025) > 2.0, "reverb includes pre-delay");
    std::cout << "MixPilot reliability DSP tests passed\n";
}
