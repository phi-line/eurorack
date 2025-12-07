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
// Onset detector.

#ifndef RINGS_DSP_ONSET_DETECTOR_H_
#define RINGS_DSP_ONSET_DETECTOR_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/filter.h"

namespace rings {

using namespace std;
using namespace stmlib;

// ZScorer: Z-score normalizer for outlier detection
// Tracks mean and variance to detect statistical outliers
class ZScorer {
 public:
  ZScorer() { }
  ~ZScorer() { }
  
  // Initialize Z-scorer: Set smoothing coefficient
  void Init(float cutoff) {
    coefficient_ = cutoff;  // Smoothing coefficient (low-pass filter)
    mean_ = 0.0f;           // Initialize mean to zero
    variance_ = 0.00f;      // Initialize variance to zero
  }
  
  // Normalize sample: Return z-score (centered sample / standard deviation)
  inline float Normalize(float sample) {
    return Update(sample) / Sqrt(variance_);  // Z-score normalization
  }
  
  // Test sample: Check if sample exceeds threshold (relative)
  inline bool Test(float sample, float threshold) {
    float value = Update(sample);
    return value > Sqrt(variance_) * threshold;  // Compare to threshold * std_dev
  }

  // Test sample: Check if sample exceeds both relative and absolute thresholds
  inline bool Test(float sample, float threshold, float absolute_threshold) {
    float value = Update(sample);
    // Must exceed both relative threshold (z-score) and absolute threshold
    return value > Sqrt(variance_) * threshold && value > absolute_threshold;
  }

 private:
  // Update statistics: Update mean and variance, return centered value
  inline float Update(float sample) {
    float centered = sample - mean_;  // Center sample around mean
    // Update mean: Exponential moving average
    mean_ += coefficient_ * centered;
    // Update variance: Exponential moving average of squared deviation
    variance_ += coefficient_ * (centered * centered - variance_);
    return centered;  // Return centered value
  }
  
  float coefficient_;  // Smoothing coefficient (low-pass filter cutoff)
  float mean_;         // Running mean (exponential moving average)
  float variance_;     // Running variance (exponential moving average)
  
  DISALLOW_COPY_AND_ASSIGN(ZScorer);
};

// Compressor: Automatic gain control compressor
// Normalizes signal level for consistent onset detection
class Compressor {
 public:  
  Compressor() { }
  ~Compressor() { }
  
  // Initialize compressor: Set attack, decay, and maximum gain
  void Init(float attack, float decay, float max_gain) {
    attack_ = attack;        // Attack time constant (fast)
    decay_ = decay;          // Decay time constant (slow)
    level_ = 0.0f;           // Current level estimate
    skew_ = 1.0f / max_gain; // Skew factor (inverse of max gain)
  }
  
  // Process audio: Apply compression with level-dependent gain
  void Process(const float* in, float* out, size_t size) {
    float level = level_;
    while (size--) {
      // Update level: Track envelope with attack/decay
      SLOPE(level, fabs(*in), attack_, decay_);
      // Apply gain: Divide by (skew + level) for compression
      *out++ = *in++ / (skew_ + level);
    }
    level_ = level;  // Store updated level
  }
 
 private:
  float attack_;  // Attack time constant (fast response)
  float decay_;   // Decay time constant (slow response)
  float level_;   // Current level estimate (envelope follower)
  float skew_;    // Skew factor (prevents division by zero)
  
  DISALLOW_COPY_AND_ASSIGN(Compressor);
};

// OnsetDetector: Detects audio onsets (transients) for automatic strumming
// Uses multi-band energy analysis with outlier detection
class OnsetDetector {
 public:  
  OnsetDetector() { }
  ~OnsetDetector() { }
  
  // Initialize onset detector: Set up filter bank and thresholds
  void Init(
      float low,              // Low frequency threshold (8ms)
      float low_mid,          // Low-mid frequency threshold (160ms)
      float mid_high,        // Mid-high frequency threshold (1600ms)
      float decimated_sr,    // Decimated sample rate
      float ioi_time) {      // Inter-onset interval time (10ms)
    float ioi_f = 1.0f / (ioi_time * decimated_sr);
    // Initialize compressor: Fast attack (10x IOI), slow decay (0.05x IOI), max gain 40
    compressor_.Init(ioi_f * 10.0f, ioi_f * 0.05f, 40.0f);
    
    // Initialize filter bank: Split signal into 3 bands
    low_mid_filter_.Init();
    mid_high_filter_.Init();
    low_mid_filter_.set_f_q<FREQUENCY_DIRTY>(low_mid, 0.5f);    // Low-mid split
    mid_high_filter_.set_f_q<FREQUENCY_DIRTY>(mid_high, 0.5f);  // Mid-high split

    // Set attack/decay times for each band (all use same values)
    attack_[0] = low_mid;      // Attack: 160ms
    decay_[0] = low * 0.25f;   // Decay: 2ms

    attack_[1] = low_mid;
    decay_[1] = low * 0.25f;

    attack_[2] = low_mid;
    decay_[2] = low * 0.25f;

    // Initialize energy and envelope arrays
    fill(&envelope_[0], &envelope_[3], 0.0f);
    fill(&energy_[0], &energy_[3], 0.0f);
    
    // Initialize Z-scorer for outlier detection
    z_df_.Init(ioi_f * 0.05f);
    
    // Initialize inhibit timer: Prevent double-triggering
    inhibit_time_ = static_cast<int32_t>(ioi_time * decimated_sr);
    inhibit_decay_ = 1.0f / (ioi_time * decimated_sr);
    
    inhibit_threshold_ = 0.0f;  // Dynamic threshold
    inhibit_counter_ = 0;        // Inhibit counter
    onset_df_ = 0.0f;            // Onset detection function (smoothed)
  }
  
  // Process audio: Detect onsets using multi-band energy analysis
  bool Process(const float* samples, size_t size) {
    // Automatic gain control: Normalize signal level
    compressor_.Process(samples, bands_[0], size);
    
    // Filter bank: Split signal into 3 frequency bands
    // bands_[0] = low, bands_[1] = mid, bands_[2] = high
    mid_high_filter_.Split(bands_[0], bands_[1], bands_[2], size);  // Split high from mid+low
    low_mid_filter_.Split(bands_[1], bands_[0], bands_[1], size);    // Split low from mid

    // Compute energy and onset detection function (derivative) in each band
    float onset_df = 0.0f;      // Onset detection function (sum of derivatives)
    float total_energy = 0.0f;  // Total energy across all bands
    for (int32_t i = 0; i < 3; ++i) {
      float* s = bands_[i];
      float energy = 0.0f;
      float envelope = envelope_[i];
      size_t increment = 4 >> i;  // Low: 4, Mid: 2, High: 1 (downsampling)
      // Compute energy envelope: Track squared amplitude with attack/decay
      for (size_t j = 0; j < size; j += increment) {
        SLOPE(envelope, s[j] * s[j], attack_[i], decay_[i]);  // Envelope follower
        energy += envelope;
      }
      energy = Sqrt(energy) * float(increment);  // Scale by increment
      envelope_[i] = envelope;

      // Onset detection: Derivative of energy (change in energy)
      float derivative = energy - energy_[i];
      // Sum derivative and absolute derivative (detects both increases and decreases)
      onset_df += derivative + fabs(derivative);
      energy_[i] = energy;
      total_energy += energy;
    }
    
    // Smooth onset detection function: Low-pass filter
    onset_df_ += 0.05f * (onset_df - onset_df_);
    // Outlier detection: Check if onset_df is a statistical outlier
    bool outlier_in_df = z_df_.Test(onset_df_, 1.0f, 0.01f);  // Z-score > 1.0, absolute > 0.01
    // Energy threshold: Must exceed dynamic threshold
    bool exceeds_energy_threshold = total_energy >= inhibit_threshold_;
    // Inhibit check: Must not be in inhibit period
    bool not_inhibited = !inhibit_counter_;
    // Onset detected: All conditions must be true
    bool has_onset = outlier_in_df && exceeds_energy_threshold && not_inhibited;
    
    // Update inhibit threshold and counter
    if (has_onset) {
      inhibit_threshold_ = total_energy * 1.5f;  // Set threshold to 1.5x current energy
      inhibit_counter_ = inhibit_time_;            // Start inhibit period
    } else {
      inhibit_threshold_ -= inhibit_decay_ * inhibit_threshold_;  // Decay threshold
      if (inhibit_counter_) {
        --inhibit_counter_;  // Decrement inhibit counter
      }
    }
    return has_onset;  // Return onset detection result
  }
  
 private:
  Compressor compressor_;        // Automatic gain control compressor
  NaiveSvf low_mid_filter_;     // Low-mid frequency split filter
  NaiveSvf mid_high_filter_;    // Mid-high frequency split filter
  
  float attack_[3];    // Attack time constants for each band
  float decay_[3];     // Decay time constants for each band
  float energy_[3];    // Energy levels for each band (previous frame)
  float envelope_[3];  // Envelope followers for each band
  float onset_df_;     // Onset detection function (smoothed)
  
  float bands_[3][32];  // Filter bank output buffers (3 bands, 32 samples each)
  
  ZScorer z_df_;  // Z-scorer for outlier detection in onset detection function
  
  float inhibit_threshold_;   // Dynamic energy threshold (prevents false triggers)
  float inhibit_decay_;       // Threshold decay rate
  int32_t inhibit_time_;      // Inhibit period duration (in samples)
  int32_t inhibit_counter_;   // Current inhibit counter (counts down to 0)
  
  DISALLOW_COPY_AND_ASSIGN(OnsetDetector);
};

}  // namespace rings

#endif  // RINGS_DSP_ONSET_DETECTOR_H_
