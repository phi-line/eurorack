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
// Low pass filter for getting stable pitch data.

#ifndef RINGS_DSP_NOTE_FILTER_H_
#define RINGS_DSP_NOTE_FILTER_H_

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/delay_line.h"

namespace rings {

// NoteFilter: Low-pass filter for getting stable pitch data
// Combines median filtering, adaptive lag processing, and delayed stable note output
class NoteFilter {
 public:
  enum {
    N = 4  // Median filter order: 4 samples
  };
  NoteFilter() { }
  ~NoteFilter() { }

  // Initialize note filter: Set up time constants and delay line
  void Init(
      float sample_rate,              // Update rate (typically 2000 Hz: 48kHz/24)
      float time_constant_fast_edge, // Fast edge response time (1ms)
      float time_constant_steady_part, // Steady state lag time (10ms)
      float edge_recovery_time,       // Edge recovery time (50ms)
      float edge_avoidance_delay) {   // Edge avoidance delay (4ms)
    // Calculate filter coefficients from time constants
    fast_coefficient_ = 1.0f / (time_constant_fast_edge * sample_rate);
    slow_coefficient_ = 1.0f / (time_constant_steady_part * sample_rate);
    lag_coefficient_ = 1.0f / (edge_recovery_time * sample_rate);
  
    // Initialize delayed stable note delay line
    delayed_stable_note_.Init();
    delayed_stable_note_.set_delay(
        std::min(size_t(15), size_t(edge_avoidance_delay * sample_rate)));  // Max 15 samples
  
    // Initialize note values to A4 (MIDI 69)
    stable_note_ = note_ = 69.0f;
    coefficient_ = fast_coefficient_;        // Start with fast coefficient
    stable_coefficient_ = slow_coefficient_; // Start with slow coefficient
    std::fill(&previous_values_[0], &previous_values_[N], note_);  // Fill median filter buffer
  }

  // Process note: Apply median filtering and adaptive lag processing
  inline float Process(float note, bool strum) {
    // Sharp change detection: If note changes by >0.4 semitones or strum triggered
    if (fabs(note - note_) > 0.4f || strum) {
      // Instant update: Follow sharp changes immediately
      stable_note_ = note_ = note;
      coefficient_ = fast_coefficient_;        // Use fast coefficient
      stable_coefficient_ = slow_coefficient_; // Use slow coefficient
      std::fill(&previous_values_[0], &previous_values_[N], note);  // Reset median filter
    } else {
      // Median filtering: Remove outliers from raw note values
      float sorted_values[N];
      // Shift buffer: Move oldest value out, add new value
      std::rotate(
          &previous_values_[0],
          &previous_values_[1],
          &previous_values_[N]);
      previous_values_[N - 1] = note;
      // Sort values for median calculation
      std::copy(&previous_values_[0], &previous_values_[N], &sorted_values[0]);
      std::sort(&sorted_values[0], &sorted_values[N]);
      // Calculate median: Average of two middle values (for even N=4)
      float median = 0.5f * (sorted_values[(N - 1) / 2] + sorted_values[N / 2]);
    
      // Adaptive lag processor: Smoothly follow median with adaptive time constant
      note_ += coefficient_ * (median - note_);                    // Filtered note
      stable_note_ += stable_coefficient_ * (note_ - stable_note_); // Stable note (slower)

      // Adapt coefficients: Gradually transition from fast to slow
      coefficient_ += lag_coefficient_ * (slow_coefficient_ - coefficient_);
      stable_coefficient_ += lag_coefficient_ * \
          (lag_coefficient_ - stable_coefficient_);
    
      // Write stable note to delay line (for edge avoidance)
      delayed_stable_note_.Write(stable_note_);
    }
    return note_;  // Return filtered note
  }

  // Get filtered note: Current filtered note value (fast response)
  inline float note() const {
    return note_;
  }

  // Get stable note: Delayed stable note (prevents edge leakage to previous voice)
  inline float stable_note() const {
    return delayed_stable_note_.Read();  // Read from delay line
  }

 private:
  float previous_values_[N];                    // Median filter buffer (4 samples)
  float note_;                                  // Filtered note (fast response)
  float stable_note_;                           // Stable note (slow response)
  stmlib::DelayLine<float, 16> delayed_stable_note_;  // Delayed stable note (for edge avoidance)

  float coefficient_;           // Current filter coefficient (adaptive)
  float stable_coefficient_;     // Current stable filter coefficient (adaptive)

  float fast_coefficient_;       // Fast coefficient (for sharp edges)
  float slow_coefficient_;       // Slow coefficient (for steady state)
  float lag_coefficient_;        // Lag coefficient (for adaptation rate)
};

}  // namespace rings

#endif  // RINGS_DSP_NOTE_FILTER_H_
