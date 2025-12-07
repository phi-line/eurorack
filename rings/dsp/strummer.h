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
// Strumming logic.

#ifndef RINGS_DSP_STRUMMER_H_
#define RINGS_DSP_STRUMMER_H_

#include "stmlib/stmlib.h"

#include "rings/dsp/onset_detector.h"
#include "rings/dsp/part.h"

namespace rings {

// Strummer: Handles automatic strum detection and note change detection
// Detects note changes on V/OCT input and audio onsets on IN input
class Strummer {
 public:
  Strummer() { }
  ~Strummer() { }
  
  // Initialize strummer with inter-onset interval and sample rate
  void Init(float ioi, float sr) {
    // Initialize onset detector with timing thresholds (8ms, 160ms, 1600ms)
    onset_detector_.Init(
        8.0f / kSampleRate,      // Fast attack threshold
        160.0f / kSampleRate,    // Medium threshold
        1600.0f / kSampleRate,   // Slow threshold
        sr,                       // Sample rate
        ioi);                     // Inter-onset interval
    inhibit_timer_ = static_cast<int32_t>(ioi * sr);  // Convert IOI to samples
    inhibit_counter_ = 0;         // Initialize inhibit counter
    previous_note_ = 69.0f;       // Initialize previous note (A4 = MIDI 69)
  }
  
  // Process audio input and detect strum triggers
  void Process(
      const float* in,                    // Audio input (NULL if not connected)
      size_t size,                        // Block size
      PerformanceState* performance_state) {  // Performance state to update
    
    // Detect audio onsets: Check for transients in input signal
    bool has_onset = in && onset_detector_.Process(in, size);
    // Detect note changes: Check if note changed by more than 0.4 semitones
    bool note_changed = fabs(performance_state->note - previous_note_) > 0.4f;

    int32_t inhibit_timer = inhibit_timer_;
    // Internal strum detection: Automatically detect strums when no external strum input
    if (performance_state->internal_strum) {
      bool has_external_note_cv = !performance_state->internal_note;
      bool has_external_exciter = !performance_state->internal_exciter;
      if (has_external_note_cv) {
        // External note CV: strum on note change (>0.4 semitones)
        performance_state->strum = note_changed;
      } else if (has_external_exciter) {
        // External exciter: strum on audio onset detection
        performance_state->strum = has_onset;
        // Use longer inhibit time for onset detector (prevents double-triggering)
        inhibit_timer *= 4;
      } else {
        // Nothing connected: no auto-strum (module doesn't play itself)
        performance_state->strum = false;
      }
    }

    // Inhibit timer: Prevents rapid retriggering after a strum
    if (inhibit_counter_) {
      --inhibit_counter_;              // Decrement counter
      performance_state->strum = false;  // Suppress strum during inhibit period
    } else {
      if (performance_state->strum) {
        inhibit_counter_ = inhibit_timer;  // Start inhibit timer on strum
      }
    }
    previous_note_ = performance_state->note;  // Store current note for next frame
  }

 private:
  float previous_note_;        // Previous note value for change detection
  int32_t inhibit_counter_;    // Current inhibit counter (counts down to 0)
  int32_t inhibit_timer_;      // Inhibit timer duration (in samples)
  
  OnsetDetector onset_detector_;  // Audio onset detector for transient detection
  
  DISALLOW_COPY_AND_ASSIGN(Strummer);
};

}  // namespace rings

#endif  // RINGS_DSP_STRUMMER_H_
