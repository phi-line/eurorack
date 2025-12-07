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
// AD envelope for the string synth.

#ifndef ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_
#define ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_

#include "stmlib/stmlib.h"

namespace rings {

// Envelope shapes: Curve shapes for envelope segments
enum EnvelopeShape {
  ENVELOPE_SHAPE_LINEAR,   // Linear interpolation
  ENVELOPE_SHAPE_QUARTIC   // Quartic curve (smooth S-curve)
};

// Envelope flags: Control envelope behavior
enum EnvelopeFlags {
  ENVELOPE_FLAG_RISING_EDGE = 1,   // Trigger attack (rising edge)
  ENVELOPE_FLAG_FALLING_EDGE = 2,  // Trigger release (falling edge)
  ENVELOPE_FLAG_GATE = 4           // Gate signal (sustain)
};

// StringSynthEnvelope: AD/AR envelope for string synth voices
// Multi-segment envelope with configurable shapes and sustain point
class StringSynthEnvelope {
 public:
  StringSynthEnvelope() { }
  ~StringSynthEnvelope() { }
  
  // Initialize envelope: Set default AD envelope
  void Init() {
    set_ad(0.1f, 0.001f);  // Default: 0.1 attack, 0.001 decay
    segment_ = num_segments_;  // Set to end (inactive)
    phase_ = 0.0f;             // Reset phase
    start_value_ = 0.0f;        // Reset start value
    value_ = 0.0f;              // Reset output value
  }

  // Process envelope: Update envelope value based on flags
  inline float Process(uint8_t flags) {
    // Rising edge: Start attack segment
    if (flags & ENVELOPE_FLAG_RISING_EDGE) {
      start_value_ = segment_ == num_segments_ ? level_[0] : value_;  // Start from current or zero
      segment_ = 0;      // Go to attack segment
      phase_ = 0.0f;     // Reset phase
    } else if (flags & ENVELOPE_FLAG_FALLING_EDGE && sustain_point_) {
      // Falling edge: Start release segment (if sustain point exists)
      start_value_ = value_;        // Start from current value
      segment_ = sustain_point_;     // Go to sustain/release segment
      phase_ = 0.0f;                 // Reset phase
    } else if (phase_ >= 1.0f) {
      // Segment complete: Move to next segment
      start_value_ = level_[segment_ + 1];  // Start from end of current segment
      ++segment_;                            // Move to next segment
      phase_ = 0.0f;                         // Reset phase
    }
  
    // Check envelope state
    bool done = segment_ == num_segments_;  // Envelope complete
    bool sustained = sustain_point_ && segment_ == sustain_point_ &&
        flags & ENVELOPE_FLAG_GATE;  // Sustaining (gate held)
  
    // Update phase: Only if not sustained and not done
    float phase_increment = 0.0f;
    if (!sustained && !done) {
      phase_increment = rate_[segment_];  // Get rate for current segment
    }
    
    // Apply shape: Linear or quartic curve
    float t = phase_;
    if (shape_[segment_] == ENVELOPE_SHAPE_QUARTIC) {
      // Quartic curve: Smooth S-curve (1 - (1-t)^4)
      t = 1.0f - t;
      t *= t;      // Square
      t *= t;      // Square again (4th power)
      t = 1.0f - t;  // Invert
    }
    
    // Update phase and compute output value
    phase_ += phase_increment;
    value_ = start_value_ + (level_[segment_ + 1] - start_value_) * t;  // Linear interpolation
    return value_;
  }

  // Set AD envelope: Attack-Decay envelope (no sustain)
  inline void set_ad(float attack, float decay) {
    num_segments_ = 2;      // Two segments: attack and decay
    sustain_point_ = 0;     // No sustain point

    level_[0] = 0.0f;       // Start level
    level_[1] = 1.0f;       // Peak level
    level_[2] = 0.0f;       // End level

    rate_[0] = attack;      // Attack rate
    rate_[1] = decay;      // Decay rate
    
    shape_[0] = ENVELOPE_SHAPE_LINEAR;   // Linear attack
    shape_[1] = ENVELOPE_SHAPE_QUARTIC;  // Quartic decay (smooth)
  }

  // Set AR envelope: Attack-Release envelope (with sustain)
  inline void set_ar(float attack, float decay) {
    num_segments_ = 2;      // Two segments: attack and release
    sustain_point_ = 1;     // Sustain at segment 1

    level_[0] = 0.0f;       // Start level
    level_[1] = 1.0f;       // Sustain level
    level_[2] = 0.0f;       // End level

    rate_[0] = attack;      // Attack rate
    rate_[1] = decay;       // Release rate
    
    shape_[0] = ENVELOPE_SHAPE_LINEAR;  // Linear attack
    shape_[1] = ENVELOPE_SHAPE_LINEAR;   // Linear release
  }
  
 private:
  float level_[4];           // Segment end levels (max 4 segments)
  float rate_[4];            // Segment rates (phase increment per sample)
  EnvelopeShape shape_[4];    // Segment shapes (linear or quartic)
  
  int16_t segment_;          // Current segment index
  float start_value_;         // Start value of current segment
  float value_;              // Current envelope output value
  float phase_;              // Current phase within segment (0.0-1.0)
  
  uint16_t num_segments_;    // Total number of segments
  uint16_t sustain_point_;   // Sustain segment index (0 = no sustain)

  DISALLOW_COPY_AND_ASSIGN(StringSynthEnvelope);
};

}  // namespace rings

#endif  // ELEMENTS_DSP_STRING_SYNTH_ENVELOPE_H_
