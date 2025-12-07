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
// Chorus.

#ifndef RINGS_DSP_FX_CHORUS_H_
#define RINGS_DSP_FX_CHORUS_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"

#include "rings/dsp/fx/fx_engine.h"
#include "rings/resources.h"

namespace rings {

// Chorus: Stereo chorus effect with dual LFOs
// Uses modulated delay lines to create pitch-shifted copies of the signal
class Chorus {
 public:
  Chorus() { }
  ~Chorus() { }
  
  // Initialize chorus: Set up FX engine and LFO phases
  void Init(uint16_t* buffer) {
    engine_.Init(buffer);  // Initialize FX engine with shared buffer
    phase_1_ = 0;          // Reset LFO 1 phase
    phase_2_ = 0;          // Reset LFO 2 phase
  }
  
  // Process chorus: Apply stereo chorus effect
  void Process(float* left, float* right, size_t size) {
    typedef E::Reserve<2047> Memory;  // Delay line: 2047 samples (42.6ms)
    E::DelayLine<Memory, 0> line;     // Single delay line (shared by both channels)
    E::Context c;                     // FX engine context
    
    while (size--) {
      engine_.Start(&c);
      // Dry amount: Reduce dry signal as amount increases (max 50% reduction)
      float dry_amount = 1.0f - amount_ * 0.5f;
    
      // Update LFOs: Two independent LFOs at different rates
      phase_1_ += 4.17e-06f;  // LFO 1: ~0.2 Hz (slow)
      if (phase_1_ >= 1.0f) {
        phase_1_ -= 1.0f;  // Wrap phase
      }
      phase_2_ += 5.417e-06f;  // LFO 2: ~0.26 Hz (slightly faster)
      if (phase_2_ >= 1.0f) {
        phase_2_ -= 1.0f;  // Wrap phase
      }
      // Generate LFO waveforms: Sine and cosine for quadrature
      float sin_1 = stmlib::Interpolate(lut_sine, phase_1_, 4096.0f);        // LFO 1 sine
      float cos_1 = stmlib::Interpolate(lut_sine, phase_1_ + 0.25f, 4096.0f); // LFO 1 cosine (90°)
      float sin_2 = stmlib::Interpolate(lut_sine, phase_2_, 4096.0f);        // LFO 2 sine
      float cos_2 = stmlib::Interpolate(lut_sine, phase_2_ + 0.25f, 4096.0f); // LFO 2 cosine (90°)
    
      float wet;  // Wet signal output
    
      // Write input to delay line: Sum left and right channels
      c.Read(*left, 0.5f);   // Read left channel (50%)
      c.Read(*right, 0.5f); // Read right channel (50%)
      c.Write(line, 0.0f);   // Write sum to delay line
    
      // Left channel: Read from delay line with dual LFO modulation
      c.Interpolate(line, sin_1 * depth_ + 1200, 0.5f);  // First tap: LFO 1 modulation
      c.Interpolate(line, sin_2 * depth_ + 800, 0.5f);   // Second tap: LFO 2 modulation
      c.Write(wet, 0.0f);                                  // Sum taps
      *left = wet * amount_ + *left * dry_amount;          // Mix wet and dry
      
      // Right channel: Read from delay line with different LFO phases
      c.Interpolate(line, cos_1 * depth_ + 800 + cos_2 * 0, 0.5f);  // First tap: LFO 1 cosine
      c.Interpolate(line, cos_2 * depth_ + 1200, 0.5f);              // Second tap: LFO 2 cosine
      c.Write(wet, 0.0f);                                             // Sum taps
      *right = wet * amount_ + *right * dry_amount;                  // Mix wet and dry
      left++;
      right++;
    }
  }
  
  // Set chorus amount: Wet/dry mix (0.0 = dry, 1.0 = wet)
  inline void set_amount(float amount) {
    amount_ = amount;
  }
  
  // Set chorus depth: Modulation depth (scaled to samples)
  inline void set_depth(float depth) {
    depth_ = depth * 384.0f;  // Scale: 0.0-1.0 -> 0-384 samples
  }
  
 private:
  typedef FxEngine<2048, FORMAT_16_BIT> E;  // FX engine: 2KB buffer, 16-bit format
  E engine_;
  
  float amount_;  // Wet/dry mix amount
  float depth_;   // Modulation depth (in samples)
  
  float phase_1_;  // LFO 1 phase accumulator
  float phase_2_;  // LFO 2 phase accumulator
  
  DISALLOW_COPY_AND_ASSIGN(Chorus);
};

}  // namespace rings

#endif  // RINGS_DSP_FX_CHORUS_H_