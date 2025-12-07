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
// Resonator.

#include "rings/dsp/resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "rings/resources.h"

namespace rings {

using namespace std;
using namespace stmlib;

// Initialize resonator: Set up all modal filters with default parameters
void Resonator::Init() {
  // Initialize all modal filters (up to 64)
  for (int32_t i = 0; i < kMaxModes; ++i) {
    f_[i].Init();  // Initialize state variable filter
  }

  // Set default parameters: A4 (220 Hz), moderate structure, brightness, damping
  set_frequency(220.0f / kSampleRate);  // A4 normalized frequency
  set_structure(0.25f);                  // Moderate inharmonicity
  set_brightness(0.5f);                  // Medium brightness
  set_damping(0.3f);                     // Medium damping
  set_position(0.999f);                  // Near end excitation
  previous_position_ = 0.0f;             // Initialize previous position
  set_resolution(kMaxModes);             // Maximum resolution (64 modes)
}

// Compute filters: Calculate modal filter frequencies and Q factors
// Returns number of active modes (filters below Nyquist)
int32_t Resonator::ComputeFilters() {
  // Stiffness: Interpolate from lookup table (controls inharmonicity)
  float stiffness = Interpolate(lut_stiffness, structure_, 256.0f);
  float harmonic = frequency_;      // Start with fundamental frequency
  float stretch_factor = 1.0f;      // Frequency stretch factor (starts at 1.0)
  // Q factor: Interpolate from 4-decade lookup table (500x range)
  float q = 500.0f * Interpolate(
      lut_4_decades,
      damping_,
      256.0f);
  
  // Brightness attenuation: Reduce brightness range when structure is very low
  // Prevents clipping when many modes are active
  float brightness_attenuation = 1.0f - structure_;
  brightness_attenuation *= brightness_attenuation;  // Square
  brightness_attenuation *= brightness_attenuation; // Square again (4th power)
  brightness_attenuation *= brightness_attenuation; // Square again (8th power)
  float brightness = brightness_ * (1.0f - 0.2f * brightness_attenuation);
  
  // Q loss: Brightness-dependent Q reduction (higher brightness = lower Q)
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;  // 0.15-1.0 range
  // Q loss damping rate: Structure-dependent (controls how Q changes with mode number)
  float q_loss_damping_rate = structure_ * (2.0f - structure_) * 0.1f;
  
  int32_t num_modes = 0;
  // Compute each modal filter
  for (int32_t i = 0; i < min(kMaxModes, resolution_); ++i) {
    // Calculate partial frequency: harmonic * stretch_factor
    float partial_frequency = harmonic * stretch_factor;
    // Clamp to Nyquist (0.49) to prevent aliasing
    if (partial_frequency >= 0.49f) {
      partial_frequency = 0.49f;
    } else {
      num_modes = i + 1;  // Count active modes
    }
    // Set filter frequency and Q: Q increases with frequency
    f_[i].set_f_q<FREQUENCY_FAST>(
        partial_frequency,
        1.0f + partial_frequency * q);  // Q: 1.0 to 1.0 + 0.49*q
    
    // Update stretch factor: Add stiffness (inharmonicity)
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Negative stiffness: Decay to prevent foldback into negative frequencies
      stiffness *= 0.93f;
    } else {
      // Positive stiffness: Slight decay to add extra partials at high frequencies
      stiffness *= 0.98f;
    }
    // Update Q loss: Gradually increase Q loss for higher modes
    // Prevents highest partials from decaying too fast
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    // Move to next harmonic
    harmonic += frequency_;
    // Apply Q loss: Reduce Q for higher modes
    q *= q_loss;
  }
  
  return num_modes;  // Return number of active modes
}

// Process audio: Main audio processing function for modal resonator
void Resonator::Process(const float* in, float* out, float* aux, size_t size) {
  // Compute filter frequencies and Q factors: Returns number of active modes
  int32_t num_modes = ComputeFilters();
  
  // Interpolate position parameter: Smooth position changes
  ParameterInterpolator position(&previous_position_, position_, size);
  while (size--) {
    // Cosine oscillator: Generates mode amplitudes based on excitation position
    // Position determines which modes are excited (0.0 = center, 1.0 = end)
    CosineOscillator amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(position.Next());
    
    // Scale input: Reduce input level (0.125 = 1/8) to prevent clipping
    float input = *in++ * 0.125f;
    float odd = 0.0f;   // Odd modes output
    float even = 0.0f;  // Even modes output
    amplitudes.Start(); // Initialize cosine oscillator
    
    // Process modes in pairs: Alternate between odd and even outputs
    for (int32_t i = 0; i < num_modes;) {
      // Odd modes: Multiply amplitude by bandpass filter output
      odd += amplitudes.Next() * f_[i++].Process<FILTER_MODE_BAND_PASS>(input);
      // Even modes: Multiply amplitude by bandpass filter output
      even += amplitudes.Next() * f_[i++].Process<FILTER_MODE_BAND_PASS>(input);
    }
    // Output: Separate odd and even partials
    *out++ = odd;
    *aux++ = even;
  }
}

}  // namespace rings
