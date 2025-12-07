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
// Envelope / centroid follower for FM voice.

#ifndef RINGS_DSP_FOLLOWER_H_
#define RINGS_DSP_FOLLOWER_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace rings {

using namespace stmlib;

// Follower: Envelope and centroid follower for FM voice
// Extracts amplitude envelope and spectral centroid from input signal
class Follower {
 public:  
  Follower() { }
  ~Follower() { }
  
  // Initialize follower: Set up filter bank and time constants
  void Init(float low, float low_mid, float mid_high) {
    // Initialize filter bank: Split signal into 3 frequency bands
    low_mid_filter_.Init();
    mid_high_filter_.Init();
    
    // Set filter frequencies: Low-mid and mid-high splits
    low_mid_filter_.set_f_q<FREQUENCY_DIRTY>(low_mid, 0.5f);
    mid_high_filter_.set_f_q<FREQUENCY_DIRTY>(mid_high, 0.5f);
    
    // Set attack/decay times for each band: Geometric means for smooth transitions
    attack_[0] = low_mid;                      // Low band attack
    decay_[0] = Sqrt(low_mid * low);           // Low band decay

    attack_[1] = Sqrt(low_mid * mid_high);     // Mid band attack
    decay_[1] = low_mid;                       // Mid band decay

    attack_[2] = Sqrt(mid_high * 0.5f);        // High band attack
    decay_[2] = Sqrt(mid_high * low_mid);      // High band decay

    // Initialize detectors: Reset all band detectors
    std::fill(&detector_[0], &detector_[3], 0.0f);
    
    centroid_ = 0.0f;  // Initialize spectral centroid
  }

  // Process sample: Extract envelope and centroid from input
  void Process(
      float sample,        // Input sample
      float* envelope,     // Output: Amplitude envelope
      float* centroid) {   // Output: Spectral centroid (0.0-1.0)
    float bands[3] = { 0.0f, 0.0f, 0.0f };
    
    // Filter bank: Split signal into 3 frequency bands
    bands[2] = mid_high_filter_.Process<FILTER_MODE_HIGH_PASS>(sample);  // High band
    bands[1] = low_mid_filter_.Process<FILTER_MODE_HIGH_PASS>(
        mid_high_filter_.lp());  // Mid band (from low-mid filter high-pass)
    bands[0] = low_mid_filter_.lp();  // Low band (from low-mid filter low-pass)
    
    // Compute weighted sum and total: For centroid calculation
    float weighted = 0.0f;  // Weighted sum (frequency * energy)
    float total = 0.0f;      // Total energy
    float frequency = 0.0f;   // Frequency weight (0.0, 0.5, 1.0)
    for (int32_t i = 0; i < 3; ++i) {
      // Update detector: Track envelope with attack/decay
      SLOPE(detector_[i], fabs(bands[i]), attack_[i], decay_[i]);
      // Accumulate weighted sum and total
      weighted += detector_[i] * frequency;  // Weight by frequency
      total += detector_[i];                 // Sum energy
      frequency += 0.5f;                     // Increment frequency weight
    }
    
    // Update centroid: Asymmetric tracking (fast attack, slow decay)
    float error = weighted / (total + 0.001f) - centroid_;  // Error from current centroid
    float coefficient = error > 0.0f ? 0.05f : 0.001f;       // Fast attack, slow decay
    centroid_ += error * coefficient;                        // Update centroid
    
    // Output: Envelope and centroid
    *envelope = total;      // Total energy across all bands
    *centroid = centroid_;  // Spectral centroid (0.0 = low, 1.0 = high)
  }
  
 private:
  NaiveSvf low_mid_filter_;   // Low-mid frequency split filter
  NaiveSvf mid_high_filter_;  // Mid-high frequency split filter
  
  float attack_[3];    // Attack time constants for each band
  float decay_[3];     // Decay time constants for each band
  float detector_[3];  // Envelope detectors for each band
  
  float centroid_;    // Spectral centroid (weighted average frequency)
  
  DISALLOW_COPY_AND_ASSIGN(Follower);
};

}  // namespace rings

#endif  // RINGS_DSP_FOLLOWER_H_
