// Sample format traits for SC1000 audio engine
//
// Provides compile-time format handling via template policy classes.
// Each format type has static methods for reading/writing samples
// and constexpr properties for bytes_per_sample, scale, etc.
//
// Usage:
//   FormatS16::write(ptr, sample);
//   float val = FormatS16::read(ptr);

#pragma once

#include <alsa/asoundlib.h>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace sc {
namespace audio {

//
// TPDF (Triangular PDF) dither generator
// Uses two uniform randoms summed for triangular distribution
// Thread-local state for lock-free operation in audio callback
//
class TpdfDither {
public:
    // Returns dither value in range [-1, +1] LSB (triangular distribution)
    static inline float generate() {
        // Simple fast PRNG (xorshift32)
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        float r1 = static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);

        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        float r2 = static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);

        // Sum of two uniform [-1,1] gives triangular [-2,2], scale to [-1,1]
        return (r1 + r2) * 0.5f;
    }

private:
    static thread_local uint32_t state_;
};

// Initialize with a non-zero seed
inline thread_local uint32_t TpdfDither::state_ = 0x12345678;

//
// Format trait tags for template specialization
//

struct FormatS16 {
    static constexpr snd_pcm_format_t alsa_format = SND_PCM_FORMAT_S16_LE;
    static constexpr int bytes_per_sample = 2;
    static constexpr float scale = 32767.0f;
    static constexpr bool needs_dither = true;

    static inline void write(void* dst, float sample) {
        auto* p = static_cast<int16_t*>(dst);
        // Apply TPDF dither (1 LSB amplitude) before quantization
        float dither = TpdfDither::generate();  // [-1, +1]
        float dithered = sample * scale + dither;
        float clamped = std::fmax(-32768.0f, std::fmin(32767.0f, dithered));
        *p = static_cast<int16_t>(clamped);
    }

    static inline float read(const void* src) {
        const auto* p = static_cast<const int16_t*>(src);
        return static_cast<float>(*p) / scale;
    }
};

struct FormatS24_3LE {
    static constexpr snd_pcm_format_t alsa_format = SND_PCM_FORMAT_S24_3LE;
    static constexpr int bytes_per_sample = 3;
    static constexpr float scale = 8388607.0f;
    static constexpr bool needs_dither = false;  // 24-bit noise floor is -144dB, inaudible

    static inline void write(void* dst, float sample) {
        float clamped = std::fmax(-8388608.0f, std::fmin(8388607.0f, sample * scale));
        int32_t val = static_cast<int32_t>(clamped);
        auto* p = static_cast<uint8_t*>(dst);
        p[0] = static_cast<uint8_t>(val & 0xFF);
        p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        p[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    }

    static inline float read(const void* src) {
        const auto* p = static_cast<const uint8_t*>(src);
        // Sign-extend from 24-bit
        int32_t val = static_cast<int32_t>(p[0])
                    | (static_cast<int32_t>(p[1]) << 8)
                    | (static_cast<int32_t>(p[2]) << 16);
        // Sign extend if negative (bit 23 set)
        if (val & 0x800000) {
            val |= 0xFF000000;
        }
        return static_cast<float>(val) / scale;
    }
};

struct FormatS24_LE {
    static constexpr snd_pcm_format_t alsa_format = SND_PCM_FORMAT_S24_LE;
    static constexpr int bytes_per_sample = 4;  // Stored in low 24 bits of 32-bit word
    static constexpr float scale = 8388607.0f;
    static constexpr bool needs_dither = false;  // 24-bit noise floor is -144dB, inaudible

    static inline void write(void* dst, float sample) {
        float clamped = std::fmax(-8388608.0f, std::fmin(8388607.0f, sample * scale));
        int32_t val = static_cast<int32_t>(clamped);
        // Store in low 24 bits, leave top 8 bits as zero/sign extension
        *static_cast<int32_t*>(dst) = val & 0x00FFFFFF;
    }

    static inline float read(const void* src) {
        int32_t val = *static_cast<const int32_t*>(src);
        // Sign extend from 24-bit
        if (val & 0x800000) {
            val |= 0xFF000000;
        }
        return static_cast<float>(val) / scale;
    }
};

struct FormatS32 {
    static constexpr snd_pcm_format_t alsa_format = SND_PCM_FORMAT_S32_LE;
    static constexpr int bytes_per_sample = 4;
    static constexpr float scale = 2147483647.0f;
    static constexpr bool needs_dither = false;  // 32-bit has plenty of precision

    static inline void write(void* dst, float sample) {
        // Use double for better precision with large scale
        double clamped = std::fmax(-2147483648.0, std::fmin(2147483647.0,
                                   static_cast<double>(sample) * static_cast<double>(scale)));
        *static_cast<int32_t*>(dst) = static_cast<int32_t>(clamped);
    }

    static inline float read(const void* src) {
        return static_cast<float>(*static_cast<const int32_t*>(src)) / scale;
    }
};

struct FormatFloat {
    static constexpr snd_pcm_format_t alsa_format = SND_PCM_FORMAT_FLOAT_LE;
    static constexpr int bytes_per_sample = 4;
    static constexpr float scale = 1.0f;
    static constexpr bool needs_dither = false;

    static inline void write(void* dst, float sample) {
        // Clamp to [-1.0, 1.0] for safety
        *static_cast<float*>(dst) = std::fmax(-1.0f, std::fmin(1.0f, sample));
    }

    static inline float read(const void* src) {
        return *static_cast<const float*>(src);
    }
};

//
// Helper to select format at runtime (returns bytes per sample)
//
inline int get_bytes_per_sample(snd_pcm_format_t format) {
    switch (format) {
        case SND_PCM_FORMAT_S16_LE:   return FormatS16::bytes_per_sample;
        case SND_PCM_FORMAT_S24_3LE:  return FormatS24_3LE::bytes_per_sample;
        case SND_PCM_FORMAT_S24_LE:   return FormatS24_LE::bytes_per_sample;
        case SND_PCM_FORMAT_S32_LE:   return FormatS32::bytes_per_sample;
        case SND_PCM_FORMAT_FLOAT_LE: return FormatFloat::bytes_per_sample;
        default: return 2;  // Default to S16
    }
}

//
// Runtime format reader - reads a sample from any format and returns float [-1, 1]
// Used for capture input where format is determined at runtime
//
inline float read_sample_as_float(const void* src, snd_pcm_format_t format) {
    switch (format) {
        case SND_PCM_FORMAT_S16_LE:   return FormatS16::read(src);
        case SND_PCM_FORMAT_S24_3LE:  return FormatS24_3LE::read(src);
        case SND_PCM_FORMAT_S24_LE:   return FormatS24_LE::read(src);
        case SND_PCM_FORMAT_S32_LE:   return FormatS32::read(src);
        case SND_PCM_FORMAT_FLOAT_LE: return FormatFloat::read(src);
        default: return 0.0f;
    }
}

//
// Read a sample from capture buffer at given frame/channel position
// Returns float [-1, 1]
//
inline float read_capture_sample(const void* buffer, int format, int bytes_per_sample,
                                  unsigned long frame, int channel, int num_channels) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
    ptr += (frame * num_channels + channel) * bytes_per_sample;
    return read_sample_as_float(ptr, static_cast<snd_pcm_format_t>(format));
}

} // namespace audio
} // namespace sc
