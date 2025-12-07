// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Polyblep oscillator used for string synth synthesis.

#ifndef RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_
#define RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

namespace rings {

using namespace stmlib;

// Oscillator shapes: Different waveform shapes for string synth harmonics
enum OscillatorShape {
  OSCILLATOR_SHAPE_BRIGHT_SQUARE,  // Bright square wave (high harmonics)
  OSCILLATOR_SHAPE_SQUARE,         // Standard square wave
  OSCILLATOR_SHAPE_DARK_SQUARE,    // Dark square wave (low-pass filtered)
  OSCILLATOR_SHAPE_TRIANGLE,       // Triangle wave
};

// StringSynthOscillator: PolyBLEP oscillator for string synth synthesis
// Uses PolyBLEP (Polynomial Band-Limited Step) algorithm to reduce aliasing
class StringSynthOscillator {
 public:
  StringSynthOscillator() { }
  ~StringSynthOscillator() { }

  // Initialize oscillator: Reset all state variables
  inline void Init() {
    phase_ = 0.0f;              // Phase accumulator (0.0-1.0)
    phase_increment_ = 0.01f;    // Phase increment (frequency)
    filter_state_ = 0.0f;        // Filter state (for low-pass filtering)
    high_ = false;               // High state flag (for square wave)

    next_sample_ = 0.0f;         // Next sample (for PolyBLEP)
    next_sample_saw_ = 0.0f;     // Next sawtooth sample (for saw component)

    gain_ = 0.0f;                // Current gain (for interpolation)
    gain_saw_ = 0.0f;            // Current saw gain (for interpolation)
  }
  
  // Render oscillator: Generate waveform with PolyBLEP anti-aliasing
  template<OscillatorShape shape, bool interpolate_pitch>
  inline void Render(
      float target_increment,    // Target phase increment (frequency)
      float target_gain,          // Target gain (for square component)
      float target_gain_saw,      // Target gain (for saw component)
      float* out,                 // Output buffer (accumulates)
      size_t size) {              // Block size
    // High frequency rolloff: Cut harmonics above 12kHz, low-pass above 8kHz
    // Prevents aliasing for very high frequencies
    if (target_increment >= 0.17f) {
      target_gain *= 1.0f - (target_increment - 0.17f) * 12.5f;  // Rolloff
      if (target_increment >= 0.25f) {
        return;  // Skip rendering if too high (above Nyquist)
      }
    }
    
    // Initialize state and interpolators
    float phase = phase_;
    ParameterInterpolator phase_increment(
        &phase_increment_,
        target_increment,
        size);  // Smooth frequency changes
    ParameterInterpolator gain(&gain_, target_gain, size);        // Smooth gain changes
    ParameterInterpolator gain_saw(&gain_saw_, target_gain_saw, size);  // Smooth saw gain

    float next_sample = next_sample_;         // Next sample (for PolyBLEP)
    float next_sample_saw = next_sample_saw_; // Next saw sample
    float filter_state = filter_state_;       // Filter state
    bool high = high_;                        // High state flag

    // Main rendering loop
    while (size--) {
      // Get current samples (from previous iteration)
      float this_sample = next_sample;
      float this_sample_saw = next_sample_saw;
      next_sample = 0.0f;
      next_sample_saw = 0.0f;

      // Update phase: Use interpolated or direct increment
      float increment = interpolate_pitch
          ? phase_increment.Next()  // Smooth pitch changes
          : target_increment;       // Direct frequency
      phase += increment;
    
      float sample = 0.0f;
      const float pw = 0.5f;  // Pulse width (50% duty cycle)

      // PolyBLEP correction: Square wave rising edge (at pulse width)
      if (!high && phase >= pw) {
        float t = (phase - pw) / increment;  // Fractional time
        this_sample += ThisBlepSample(t);    // Add BLEP to current sample
        next_sample += NextBlepSample(t);     // Add BLEP to next sample
        high = true;                          // Set high state
      }
      // PolyBLEP correction: Falling edge (at phase wrap)
      if (phase >= 1.0f) {
        phase -= 1.0f;  // Wrap phase
        float t = phase / increment;  // Fractional time
        float a = ThisBlepSample(t);  // Current BLEP
        float b = NextBlepSample(t);  // Next BLEP
        // Subtract BLEP from square wave
        this_sample -= a;
        next_sample -= b;
        // Subtract BLEP from sawtooth wave
        this_sample_saw -= a;
        next_sample_saw -= b;
        high = false;  // Clear high state
      }
      
      // Generate raw waveforms: Square and sawtooth
      next_sample += phase < pw ? 0.0f : 1.0f;  // Square wave (0 or 1)
      next_sample_saw += phase;                 // Sawtooth wave (0 to 1)
      
      // Shape waveform: Apply filtering based on shape type
      if (shape == OSCILLATOR_SHAPE_TRIANGLE) {
        // Triangle wave: Integrate square wave with low-pass filter
        const float integrator_coefficient = increment * 0.125f;
        this_sample = 64.0f * (this_sample - 0.5f);  // Scale and center
        filter_state += integrator_coefficient * (this_sample - filter_state);  // Integrate
        sample = filter_state;
      } else if (shape == OSCILLATOR_SHAPE_DARK_SQUARE) {
        // Dark square: Heavy low-pass filtering
        const float integrator_coefficient = increment * 2.0f;
        this_sample = 4.0f * (this_sample - 0.5f);  // Scale and center
        filter_state += integrator_coefficient * (this_sample - filter_state);  // Low-pass
        sample = filter_state;
      } else if (shape == OSCILLATOR_SHAPE_BRIGHT_SQUARE) {
        // Bright square: High-pass filtering (removes DC, keeps high frequencies)
        const float integrator_coefficient = increment * 2.0f;
        this_sample = 2.0f * this_sample - 1.0f;  // Scale to -1 to 1
        filter_state += integrator_coefficient * (this_sample - filter_state);  // Low-pass
        sample = (this_sample - filter_state) * 0.5f;  // High-pass (difference)
      } else {
        // Standard square: No filtering
        this_sample = 2.0f * this_sample - 1.0f;  // Scale to -1 to 1
        sample = this_sample;
      }
      // Scale sawtooth: -1 to 1
      this_sample_saw = 2.0f * this_sample_saw - 1.0f;
      
      // Mix outputs: Square component + saw component
      *out++ += sample * gain.Next() + this_sample_saw * gain_saw.Next();
    }
    // Store state for next call
    high_ = high;
    phase_ = phase;
    next_sample_ = next_sample;
    next_sample_saw_ = next_sample_saw;
    filter_state_ = filter_state;
  }

 private:
  // PolyBLEP functions: Generate band-limited step correction samples
  // ThisBlepSample: Current sample BLEP correction (quadratic polynomial)
  static inline float ThisBlepSample(float t) {
    return 0.5f * t * t;  // Quadratic: 0.5 * t^2
  }
  // NextBlepSample: Next sample BLEP correction (inverted quadratic)
  static inline float NextBlepSample(float t) {
    t = 1.0f - t;         // Invert time
    return -0.5f * t * t; // Negative quadratic: -0.5 * (1-t)^2
  }

  // State variables
  bool high_;              // High state flag (for square wave)
  float phase_;            // Phase accumulator (0.0-1.0)
  float phase_increment_;  // Phase increment (frequency)
  float next_sample_;      // Next sample (for PolyBLEP)
  float next_sample_saw_;  // Next sawtooth sample
  float filter_state_;     // Filter state (for low-pass filtering)
  float gain_;             // Current gain (for interpolation)
  float gain_saw_;         // Current saw gain (for interpolation)

  DISALLOW_COPY_AND_ASSIGN(StringSynthOscillator);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_SYNTH_OSCILLATOR_H_
