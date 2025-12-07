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
// FM Voice.

#ifndef RINGS_DSP_FM_VOICE_H_
#define RINGS_DSP_FM_VOICE_H_

#include "stmlib/stmlib.h"

#include <algorithm>

#include "stmlib/dsp/filter.h"

#include "rings/dsp/dsp.h"
#include "rings/dsp/follower.h"

#include "rings/resources.h"

namespace rings {

using namespace stmlib;

// FMVoice: Frequency modulation synthesis voice
// Implements FM synthesis with carrier and modulator oscillators
class FMVoice {
 public:
  FMVoice() { }
  ~FMVoice() { }
  
  // Initialize FM voice: Set up oscillators and envelopes
  void Init();
  // Process audio: Main audio processing function
  void Process(
      const float* in,   // Input audio (for external excitation)
      float* out,         // Output buffer (odd harmonics)
      float* aux,         // Output buffer (even harmonics)
      size_t size);       // Block size
  
  // Set carrier frequency: Fundamental frequency of FM voice
  inline void set_frequency(float frequency) {
    carrier_frequency_ = frequency;  // Normalized frequency (0.0-0.5)
  }
  
  // Set FM ratio: Modulator frequency / carrier frequency
  inline void set_ratio(float ratio) {
    ratio_ = ratio;  // Controls harmonic content (structure parameter)
  }

  // Set brightness: Modulation index (amount of FM)
  inline void set_brightness(float brightness) {
    brightness_ = brightness;  // 0.0-1.0: controls modulation depth
  }
  
  // Set damping: Envelope decay time
  inline void set_damping(float damping) {
    damping_ = damping;  // 0.0-1.0: controls envelope decay rate
  }
  
  // Set position: Not used for FM (kept for compatibility)
  inline void set_position(float position) {
    position_ = position;  // Unused parameter
  }
  
  // Set feedback amount: Feedback from output to modulator (position parameter)
  inline void set_feedback_amount(float feedback_amount) {
    feedback_amount_ = feedback_amount;  // 0.0-1.0: controls feedback amount
  }
  
  // Trigger internal envelope: Start amplitude and brightness envelopes
  inline void TriggerInternalEnvelope() {
    amplitude_envelope_ = 1.0f;   // Set amplitude envelope to 1.0
    brightness_envelope_ = 1.0f;  // Set brightness envelope to 1.0
  }
  
  // Sine FM: Calculate sine wave with frequency modulation
  inline float SineFm(uint32_t phase, float fm) const {
    // Add FM offset to phase: fm is in semitones, convert to phase offset
    phase += (static_cast<uint32_t>((fm + 4.0f) * 536870912.0f)) << 3;
    // Extract integral and fractional parts for lookup table interpolation
    uint32_t integral = phase >> 20;
    float fractional = static_cast<float>(phase << 12) / 4294967296.0f;
    // Linear interpolation between lookup table entries
    float a = lut_sine[integral];
    float b = lut_sine[integral + 1];
    return a + (b - a) * fractional;
  }
  
 private:
  // Parameters
  float carrier_frequency_;   // Carrier oscillator frequency (normalized)
  float ratio_;               // FM ratio (modulator/carrier frequency)
  float brightness_;         // Brightness (modulation index)
  float damping_;            // Damping (envelope decay)
  float position_;           // Position (unused, kept for compatibility)
  float feedback_amount_;    // Feedback amount (from output to modulator)
  
  // Previous parameters (for interpolation)
  float previous_carrier_frequency_;
  float previous_modulator_frequency_;
  float previous_brightness_;
  float previous_damping_;
  float previous_feedback_amount_;
  
  // Envelopes and state
  float amplitude_envelope_;   // Amplitude envelope (decays on trigger)
  float brightness_envelope_; // Brightness envelope (decays on trigger)
  float gain_;                 // Output gain
  float fm_amount_;            // FM modulation amount
  uint32_t carrier_phase_;     // Carrier oscillator phase accumulator
  uint32_t modulator_phase_;   // Modulator oscillator phase accumulator
  float previous_sample_;      // Previous sample (for feedback)
  
  Follower follower_;  // Envelope follower (for external excitation)
  
  DISALLOW_COPY_AND_ASSIGN(FMVoice);
};

}  // namespace rings

#endif  // RINGS_DSP_FM_VOICE_H_
