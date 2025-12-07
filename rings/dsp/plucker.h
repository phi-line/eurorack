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
// Noise burst generator for Karplus-Strong synthesis.

#ifndef RINGS_DSP_PLUCKER_H_
#define RINGS_DSP_PLUCKER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/filter.h"
#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/random.h"

namespace rings {

// Plucker: Noise burst generator for Karplus-Strong synthesis
// Generates filtered noise burst with comb filter for internal exciter
class Plucker {
 public:
  Plucker() { }
  ~Plucker() { }
  
  // Initialize plucker: Reset all state
  void Init() {
    svf_.Init();                    // Initialize state variable filter
    comb_filter_.Init();            // Initialize comb filter delay line
    remaining_samples_ = 0;         // Reset sample counter
    comb_filter_period_ = 0.0f;    // Reset comb filter period
  }
  
  // Trigger plucker: Set up noise burst parameters
  void Trigger(float frequency, float cutoff, float position) {
    // Position ratio: Maps position (0.0-1.0) to comb delay ratio (0.05-0.95)
    float ratio = position * 0.9f + 0.05f;
    // Comb filter period: Based on frequency and position
    float comb_period = 1.0f / frequency * ratio;
    // Remaining samples: Duration of noise burst (in samples)
    remaining_samples_ = static_cast<size_t>(comb_period);
    // Reduce comb period if too large (delay line limit: 255 samples)
    while (comb_period >= 255.0f) {
      comb_period *= 0.5f;  // Halve until within range
    }
    comb_filter_period_ = comb_period;
    // Comb filter gain: Position-dependent (higher position = lower gain)
    comb_filter_gain_ = (1.0f - position) * 0.8f;
    // Set low-pass filter cutoff: Clamp to Nyquist (0.499)
    svf_.set_f_q<FREQUENCY_DIRTY>(std::min(cutoff, 0.499f), 1.0f);
  }
  
  // Process plucker: Generate noise burst with comb filter and low-pass filtering
  void Process(float* out, size_t size) {
    const float comb_gain = comb_filter_gain_;
    const float comb_delay = comb_filter_period_;
    for (size_t i = 0; i < size; ++i) {
      float in = 0.0f;
      // Generate white noise during burst period
      if (remaining_samples_) {
        in = 2.0f * Random::GetFloat() - 1.0f;  // White noise: -1.0 to 1.0
        --remaining_samples_;
      }
      // Mix noise with comb filter feedback
      out[i] = in + comb_gain * comb_filter_.Read(comb_delay);
      comb_filter_.Write(out[i]);  // Write to delay line
    }
    // Low-pass filter: Smooth the noise burst
    svf_.Process<FILTER_MODE_LOW_PASS>(out, out, size);
  }

 private:
  stmlib::Svf svf_;                          // State variable filter (low-pass for noise burst)
  stmlib::DelayLine<float, 256> comb_filter_;  // Comb filter delay line (256 samples max)
  size_t remaining_samples_;                  // Remaining samples in noise burst
  float comb_filter_period_;                  // Comb filter delay (in samples)
  float comb_filter_gain_;                    // Comb filter feedback gain
  
  DISALLOW_COPY_AND_ASSIGN(Plucker);
};

}  // namespace rings

#endif  // RINGS_DSP_PLUCKER_H_
