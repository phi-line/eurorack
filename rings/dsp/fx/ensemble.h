// Copyright 2014 Emilie Gillet.
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
// Ensemble FX.

#ifndef RINGS_DSP_FX_ENSEMBLE_H_
#define RINGS_DSP_FX_ENSEMBLE_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"

#include "rings/dsp/fx/fx_engine.h"
#include "rings/resources.h"

namespace rings {

// Ensemble: Multi-voice ensemble effect with 3 detuned voices
// Creates multiple pitch-shifted copies with different LFO phases
class Ensemble {
 public:
  Ensemble() { }
  ~Ensemble() { }
  
  // Initialize ensemble: Set up FX engine and LFO phases
  void Init(uint16_t* buffer) {
    engine_.Init(buffer);  // Initialize FX engine with shared buffer
    phase_1_ = 0;          // Reset slow LFO phase
    phase_2_ = 0;          // Reset fast LFO phase
  }
  
  // Process ensemble: Apply multi-voice ensemble effect
  void Process(float* left, float* right, size_t size) {
    // Two delay lines: One for left, one for right channel
    typedef E::Reserve<2047, E::Reserve<2047> > Memory;  // 2 delay lines, 2047 samples each
    E::DelayLine<Memory, 0> line_l;  // Left channel delay line
    E::DelayLine<Memory, 1> line_r;  // Right channel delay line
    E::Context c;                     // FX engine context
    
    while (size--) {
      engine_.Start(&c);
      // Dry amount: Reduce dry signal as amount increases (max 50% reduction)
      float dry_amount = 1.0f - amount_ * 0.5f;
    
      // Update LFOs: Two LFOs at different rates (slow and fast)
      phase_1_ += 1.57e-05f;  // Slow LFO: ~0.75 Hz
      if (phase_1_ >= 1.0f) {
        phase_1_ -= 1.0f;  // Wrap phase
      }
      phase_2_ += 1.37e-04f;  // Fast LFO: ~6.6 Hz
      if (phase_2_ >= 1.0f) {
        phase_2_ -= 1.0f;  // Wrap phase
      }
      // Generate 3-phase LFOs: 0°, 120°, 240° (for 3 voices)
      int32_t phi_1 = (phase_1_ * 4096.0f);  // Slow LFO phase (integer)
      float slow_0 = lut_sine[phi_1 & 4095];        // Slow LFO: 0°
      float slow_120 = lut_sine[(phi_1 + 1365) & 4095];   // Slow LFO: 120° (1365 = 4096/3)
      float slow_240 = lut_sine[(phi_1 + 2730) & 4095];   // Slow LFO: 240° (2730 = 4096*2/3)
      int32_t phi_2 = (phase_2_ * 4096.0f);  // Fast LFO phase (integer)
      float fast_0 = lut_sine[phi_2 & 4095];        // Fast LFO: 0°
      float fast_120 = lut_sine[(phi_2 + 1365) & 4095];   // Fast LFO: 120°
      float fast_240 = lut_sine[(phi_2 + 2730) & 4095];   // Fast LFO: 240°
      
      // Modulation amounts: Slow LFO (main) + fast LFO (vibrato)
      float a = depth_ * 1.0f;   // Slow LFO amplitude
      float b = depth_ * 0.1f;   // Fast LFO amplitude (10% of slow)
      
      // Combine slow and fast LFOs: Creates 3 detuned voices
      float mod_1 = slow_0 * a + fast_0 * b;      // Voice 1 modulation
      float mod_2 = slow_120 * a + fast_120 * b;  // Voice 2 modulation
      float mod_3 = slow_240 * a + fast_240 * b;  // Voice 3 modulation
    
      float wet = 0.0f;  // Wet signal output
    
      // Write input to delay lines: Separate left and right channels
      c.Read(*left, 1.0f);   // Read left channel (100%)
      c.Write(line_l, 0.0f); // Write to left delay line
      c.Read(*right, 1.0f);  // Read right channel (100%)
      c.Write(line_r, 0.0f); // Write to right delay line
    
      // Left channel: Mix 3 voices from delay lines
      c.Interpolate(line_l, mod_1 + 1024, 0.33f);  // Voice 1: mod_1 + center delay
      c.Interpolate(line_l, mod_2 + 1024, 0.33f);  // Voice 2: mod_2 + center delay
      c.Interpolate(line_r, mod_3 + 1024, 0.33f);  // Voice 3: mod_3 + center delay (from right line)
      c.Write(wet, 0.0f);                          // Sum voices
      *left = wet * amount_ + *left * dry_amount;  // Mix wet and dry
      
      // Right channel: Mix 3 voices with different routing
      c.Interpolate(line_r, mod_1 + 1024, 0.33f);  // Voice 1: mod_1 + center delay
      c.Interpolate(line_r, mod_2 + 1024, 0.33f);  // Voice 2: mod_2 + center delay
      c.Interpolate(line_l, mod_3 + 1024, 0.33f);  // Voice 3: mod_3 + center delay (from left line)
      c.Write(wet, 0.0f);                          // Sum voices
      *right = wet * amount_ + *right * dry_amount; // Mix wet and dry
      left++;
      right++;
    }
  }
  
  // Set ensemble amount: Wet/dry mix (0.0 = dry, 1.0 = wet)
  inline void set_amount(float amount) {
    amount_ = amount;
  }
  
  // Set ensemble depth: Modulation depth (scaled to samples)
  inline void set_depth(float depth) {
    depth_ = depth * 128.0f;  // Scale: 0.0-1.0 -> 0-128 samples
  }
  
 private:
  typedef FxEngine<4096, FORMAT_16_BIT> E;  // FX engine: 4KB buffer, 16-bit format
  E engine_;
  
  float amount_;  // Wet/dry mix amount
  float depth_;   // Modulation depth (in samples)
  
  float phase_1_;  // Slow LFO phase accumulator
  float phase_2_;  // Fast LFO phase accumulator
  
  DISALLOW_COPY_AND_ASSIGN(Ensemble);
};

}  // namespace rings

#endif  // RINGS_DSP_FX_ENSEMBLE_H_