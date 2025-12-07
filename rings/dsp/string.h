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
// Comb filter / KS string.

#ifndef RINGS_DSP_STRING_H_
#define RINGS_DSP_STRING_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/delay_line.h"
#include "stmlib/dsp/filter.h"

#include "rings/dsp/dsp.h"

namespace rings {

const size_t kDelayLineSize = 2048;  // Delay line size: 2048 samples (42.7ms at 48kHz)

// DampingFilter: FIR (Finite Impulse Response) damping filter for string synthesis
// Implements a 3-tap filter with brightness control and smooth parameter interpolation
class DampingFilter {
 public:
  DampingFilter() { }
  ~DampingFilter() { }
  
  // Initialize filter: Reset all state to zero
  void Init() {
    x_ = 0.0f;                    // Current sample
    x__ = 0.0f;                   // Previous sample (two samples ago)
    brightness_ = 0.0f;           // Current brightness parameter
    brightness_increment_ = 0.0f;  // Brightness interpolation increment
    damping_ = 0.0f;              // Current damping coefficient
    damping_increment_ = 0.0f;    // Damping interpolation increment
  }
   
  // Configure filter: Set damping and brightness with optional interpolation
  inline void Configure(float damping, float brightness, size_t size) {
    if (!size) {
      // Instant update: Set parameters immediately (no interpolation)
      damping_ = damping;
      brightness_ = brightness;
      damping_increment_ = 0.0f;
      brightness_increment_ = 0.0f;
    } else {
      // Smooth interpolation: Calculate increment per sample
      float step = 1.0f / static_cast<float>(size);
      damping_increment_ = (damping - damping_) * step;      // Damping interpolation
      brightness_increment_ = (brightness - brightness_) * step;  // Brightness interpolation
    }
  }
   
  // Process sample: Apply 3-tap FIR filter with brightness-dependent coefficients
  inline float Process(float x) {
    // Filter coefficients: h0 (center tap), h1 (side taps)
    // Brightness controls balance between center and side taps
    float h0 = (1.0f + brightness_) * 0.5f;   // Center tap coefficient (0.5-1.0)
    float h1 = (1.0f - brightness_) * 0.25f; // Side tap coefficient (0.0-0.25)
    // 3-tap filter: h0*x_ + h1*(x + x__) with damping applied
    float y = damping_ * (h0 * x_ + h1 * (x + x__));
    // Shift delay line: x__ <- x_, x_ <- x
    x__ = x_;
    x_ = x;
    // Update parameters: Apply interpolation increments
    brightness_ += brightness_increment_;
    damping_ += damping_increment_;
    return y;
  }
 private:
  float x_;
  float x__;
  float brightness_;
  float brightness_increment_;
  float damping_;
  float damping_increment_;
  
  DISALLOW_COPY_AND_ASSIGN(DampingFilter);
};

// Delay line types: Main string delay line and stiffness delay line (for dispersion)
typedef stmlib::DelayLine<float, kDelayLineSize> StringDelayLine;        // Main delay: 2048 samples
typedef stmlib::DelayLine<float, kDelayLineSize / 2> StiffnessDelayLine;  // Stiffness delay: 1024 samples

// String: Karplus-Strong string synthesis with dispersion and non-linearities
// Implements comb filter with multimode filter and non-linear processing in feedback loop
class String {
 public:
  String() { }
  ~String() { }
  
  // Initialize string: Set up delay lines and filters
  void Init(bool enable_dispersion);  // Enable dispersion for non-linear string models
  // Process audio: Main audio processing function
  void Process(const float* in, float* out, float* aux, size_t size);
  
  // Set frequency: Instant update (no smoothing)
  inline void set_frequency(float frequency) {
    frequency_ = frequency;  // Normalized frequency (0.0-0.5)
  }

  // Set frequency with smoothing: Smooth frequency changes (for glide)
  inline void set_frequency(float frequency, float coefficient) {
    // One-pole smoothing: coefficient controls smoothing amount (0.0=instant, 1.0=no change)
    frequency_ += coefficient * (frequency - frequency_);
  }

  // Set dispersion: Frequency-dependent wave velocity (stiffness)
  inline void set_dispersion(float dispersion) {
    dispersion_ = dispersion;  // Negative = stiff string, positive = flexible string
  }
  
  // Set brightness: Controls harmonic content and filter cutoff
  inline void set_brightness(float brightness) {
    brightness_ = brightness;  // 0.0-1.0: affects damping filter and noise filter
  }
  
  // Set damping: Controls decay time (RT60)
  inline void set_damping(float damping) {
    damping_ = damping;  // 0.0-1.0: maps to decay time (100ms to 10s)
  }
  
  // Set position: Pluck position along string
  inline void set_position(float position) {
    position_ = position;  // 0.0-1.0: where the string is excited
  }
  
  // Get mutable delay line: Allows external access to delay line (for debugging)
  inline StringDelayLine* mutable_string() { return &string_; }
  
 private:
  // Internal processing: Template-based implementation (dispersion enabled/disabled at compile time)
  template<bool enable_dispersion>
  void ProcessInternal(const float* in, float* out, float* aux, size_t size);
   
  // Parameters
  float frequency_;      // Fundamental frequency (normalized, 0.0-0.5)
  float dispersion_;     // Dispersion amount (negative=stiff, positive=flexible)
  float brightness_;     // Brightness parameter (0.0-1.0)
  float damping_;        // Damping parameter (0.0-1.0, maps to RT60)
  float position_;       // Pluck position (0.0-1.0)
  
  // Internal state
  float delay_;                          // Current delay length (in samples)
  float clamped_position_;               // Clamped position (prevents extreme values)
  float previous_dispersion_;            // Previous dispersion (for interpolation)
  float previous_damping_compensation_;  // Previous damping compensation (for interpolation)
  
  bool enable_dispersion_;   // Dispersion enabled flag
  bool enable_iir_damping_;  // IIR damping enabled flag
  float dispersion_noise_;   // Dispersion noise (for non-linear string models)
  
  // Upsampler: Linear interpolation upsampler for very low pitches (<11.7Hz)
  // Used when delay line is too short for the fundamental frequency
  float src_phase_;        // Upsampler phase accumulator
  float out_sample_[2];    // Output sample buffer (for upsampling)
  float aux_sample_[2];    // Auxiliary sample buffer (for upsampling)
  
  float curved_bridge_;    // Curved bridge effect (non-linear boundary condition)
  
  // Delay lines: Main string delay and stiffness delay (for dispersion)
  StringDelayLine string_;      // Main comb filter delay line (2048 samples)
  StiffnessDelayLine stretch_;  // Stiffness delay line for dispersion (1024 samples)
  
  // Filters: FIR damping filter, IIR damping filter, DC blocker
  DampingFilter fir_damping_filter_;    // FIR damping filter (3-tap with brightness)
  stmlib::Svf iir_damping_filter_;      // IIR damping filter (low-pass for high frequencies)
  stmlib::DCBlocker dc_blocker_;        // DC blocker (removes DC offset)
  
  DISALLOW_COPY_AND_ASSIGN(String);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_H_
