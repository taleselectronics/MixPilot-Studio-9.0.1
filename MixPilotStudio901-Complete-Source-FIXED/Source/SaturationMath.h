#pragma once
#include <cmath>
#include <algorithm>
namespace mixpilot {
// Drive range: 0-26 dB, with a controlled, predictable loudness tradeoff.
// Drive does NOT double as a dry/wet blend; the module mix owns that task.
inline float saturationBypass(float dry,float wet,float amount) noexcept {
    return amount>0.0001f?wet:dry;
}
inline float saturationDrive(float amount) noexcept {
    const float a=std::clamp(amount,0.0f,1.0f);
    return std::pow(10.0f,(26.0f*a)/20.0f);
}
inline float saturationMakeup(float drive) noexcept {
    return std::pow(std::max(drive,1.0f),-0.55f);
}
inline float tubeCurve(float input,float bias,float drive,float makeup) noexcept {
    const float b=std::clamp(bias,-1.0f,1.0f)*0.20f;
    return (std::tanh((input+b)*drive)-std::tanh(b*drive))*makeup;
}
inline float tapeCurve(float input,float memory,float bias,float drive,float makeup) noexcept {
    const float slew=input-memory;
    const float boostedDrive=drive*(1.0f+0.35f*std::abs(bias));
    return std::tanh((input-0.18f*slew)*boostedDrive)*makeup
        -(0.04f+0.07f*std::abs(bias))*slew;
}
inline float transformerCurve(float input,float low,float bias,float drive,float makeup) noexcept {
    const float b=std::clamp(bias,-1.0f,1.0f)*0.12f;
    return (std::tanh((input+0.5f*low+b)*drive)-std::tanh(b*drive))*makeup-0.5f*low;
}
}
