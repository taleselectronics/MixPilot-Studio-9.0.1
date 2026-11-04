#pragma once
// Pure signal-profile diagnostic: compares relative spectral-band energy only.
// Lower distance does not necessarily mean better sound or a faithful match.
#include <array>
#include <cmath>
#include <algorithm>
namespace mixpilot::planning {
inline float spectralDistanceDb(const std::array<float,18>& a,
                                const std::array<float,18>& b) noexcept {
    float total=0.0f;
    for(int i=0;i<18;++i)total+=std::abs(a[(size_t)i]-b[(size_t)i]);
    return total/18.0f;
}
} // namespace mixpilot::planning
