#include "crossfader.h"

void Crossfader::set_calibration(int adc_min, int adc_max)
{
    adc_min_ = adc_min;
    adc_max_ = adc_max;
}

void Crossfader::update(int adc_value)
{
    raw_adc_ = adc_value;

    int adc_range = adc_max_ - adc_min_;
    if (adc_range > 0) {
        // Clamp to calibration range
        int clamped = adc_value;
        if (clamped < adc_min_) clamped = adc_min_;
        if (clamped > adc_max_) clamped = adc_max_;

        // Normalize and invert (ADC is inverted: low = scratch side)
        double normalized = static_cast<double>(clamped - adc_min_) / adc_range;
        position_ = 1.0 - normalized;
    } else {
        // Fallback if bad calibration
        position_ = 0.5;
    }
}
