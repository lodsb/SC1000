// Interpolation policy traits for SC1000 audio engine
//
// Thin wrappers around existing optimized interpolation code.
// Used as template parameters to select interpolation at compile time.
//
// Usage:
//   auto result = CubicInterpolation::interpolate(tr1, pos1, len1, pitch1,
//                                                  tr2, pos2, len2, pitch2);

#pragma once

#include "../dsp/sinc_interpolate_opt.h"
#include "../dsp/cubic_interpolate_opt.h"

namespace sc {
namespace audio {

//
// Unified result type for dual-deck interpolation
//
struct DualDeckSamples {
    float l1, r1;  // Beat deck (left, right)
    float l2, r2;  // Scratch deck (left, right)
};

//
// Cubic interpolation policy (4-tap Catmull-Rom)
// Fast, no anti-aliasing
//
struct CubicInterpolation {
    static constexpr const char* name = "Cubic";

    static inline DualDeckSamples interpolate(
        struct track* tr1, double sample_pos1, int tr_len1, float /* pitch1 */,
        struct track* tr2, double sample_pos2, int tr_len2, float /* pitch2 */)
    {
        // Cubic doesn't use pitch for bandwidth selection
        auto result = dsp::cubic_interpolate_dual_deck_opt(
            tr1, sample_pos1, tr_len1,
            tr2, sample_pos2, tr_len2);

        return {result.l1, result.r1, result.l2, result.r2};
    }
};

//
// Sinc interpolation policy (16-tap windowed sinc)
// Higher quality with proper anti-aliasing
//
struct SincInterpolation {
    static constexpr const char* name = "Sinc";

    static inline DualDeckSamples interpolate(
        struct track* tr1, double sample_pos1, int tr_len1, float pitch1,
        struct track* tr2, double sample_pos2, int tr_len2, float pitch2)
    {
        // Sinc uses absolute pitch for bandwidth selection
        auto result = dsp::sinc_interpolate_dual_deck_opt(
            tr1, sample_pos1, tr_len1, std::fabs(pitch1),
            tr2, sample_pos2, tr_len2, std::fabs(pitch2));

        return {result.l1, result.r1, result.l2, result.r2};
    }
};

//
// Interpolation mode enum for runtime selection
//
enum class InterpolationMode {
    Cubic = 0,
    Sinc = 1
};

inline const char* interpolation_mode_name(InterpolationMode mode) {
    switch (mode) {
        case InterpolationMode::Cubic: return CubicInterpolation::name;
        case InterpolationMode::Sinc:  return SincInterpolation::name;
        default: return "Unknown";
    }
}

} // namespace audio
} // namespace sc
