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

#ifndef RINGS_DSP_RESONATOR_H_
#define RINGS_DSP_RESONATOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "rings/dsp/dsp.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/delay_line.h"

namespace rings {

const int32_t kMaxModes = 64;  // Maximum number of modal filters per resonator

// Modal resonator: Uses a bank of SVF (State Variable Filter) bandpass filters
// Each mode represents a harmonic or inharmonic partial of the resonator
class Resonator {
 public:
  Resonator() { }
  ~Resonator() { }
  
  void Init();  // Initialize resonator with default state
  void Process(
      const float* in,   // Input excitation signal
      float* out,         // Output: odd partials/harmonics
      float* aux,         // Output: even partials/harmonics
      size_t size);       // Block size (typically 24 samples)
  
  inline void set_frequency(float frequency) {
    frequency_ = frequency;  // Set fundamental frequency (normalized, 0.0-0.5)
  }
  
  inline void set_structure(float structure) {
    structure_ = structure;  // Set inharmonicity (0.0-0.9995) - controls mode frequency distribution
  }
  
  inline void set_brightness(float brightness) {
    brightness_ = brightness;  // Set spectrum brightness (0.0-1.0) - controls mode amplitude distribution
  }
  
  inline void set_damping(float damping) {
    damping_ = damping;  // Set decay time (0.0-0.9995) - controls mode decay rates
  }
  
  inline void set_position(float position) {
    position_ = position;  // Set excitation point (0.0-0.9995) - controls which modes are excited
  }
  
  inline void set_resolution(int32_t resolution) {
    resolution -= resolution & 1; // Must be even! (ensures even number of modes)
    resolution_ = std::min(resolution, kMaxModes);  // Clamp to maximum modes
  }

 private:
  int32_t ComputeFilters();  // Recompute filter frequencies and gains when parameters change
  float frequency_;           // Fundamental frequency (normalized)
  float structure_;           // Inharmonicity parameter
  float brightness_;          // Spectrum brightness
  float position_;            // Excitation position
  float previous_position_;   // Previous position for smooth transitions
  float damping_;             // Decay time parameter
  
  int32_t resolution_;        // Number of active modes (must be even, max 64)
  
  stmlib::Svf f_[kMaxModes];  // Array of state variable filters (one per mode)
  
  DISALLOW_COPY_AND_ASSIGN(Resonator);
};

}  // namespace rings

#endif  // RINGS_DSP_RESONATOR_H_
