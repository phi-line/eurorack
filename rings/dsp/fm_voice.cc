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

#include "rings/dsp/fm_voice.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"

#include "rings/resources.h"

namespace rings {

using namespace stmlib;

// Initialize FM voice: Set up oscillators, envelopes, and follower
void FMVoice::Init() {
  // Set default parameters: A4 (220 Hz), moderate ratio, brightness, damping
  set_frequency(220.0f / kSampleRate);  // A4 normalized frequency
  set_ratio(0.5f);                       // FM ratio 0.5 (subharmonic)
  set_brightness(0.5f);                  // Medium brightness (modulation index)
  set_damping(0.5f);                     // Medium damping
  set_position(0.5f);                    // Position (unused)
  set_feedback_amount(0.0f);             // No feedback
  
  // Initialize previous parameters (for interpolation)
  previous_carrier_frequency_ = carrier_frequency_;
  previous_modulator_frequency_ = carrier_frequency_;
  previous_brightness_ = brightness_;
  previous_damping_ = damping_;
  previous_feedback_amount_ = feedback_amount_;
  
  // Initialize envelopes: Start at zero
  amplitude_envelope_ = 0.0f;
  brightness_envelope_ = 0.0f;
  
  // Initialize oscillators: Reset phase accumulators
  carrier_phase_ = 0;
  modulator_phase_ = 0;
  gain_ = 0.0f;      // Output gain
  fm_amount_ = 0.0f;  // FM modulation amount
  
  // Initialize envelope follower: Multi-band follower for external excitation
  follower_.Init(
      8.0f / kSampleRate,    // Low threshold (8ms)
      160.0f / kSampleRate,  // Low-mid threshold (160ms)
      1600.0f / kSampleRate); // Mid-high threshold (1600ms)
}

// Process audio: Main FM synthesis processing function
void FMVoice::Process(const float* in, float* out, float* aux, size_t size) {
  // Envelope amount: Interpolate between oscillator and FMLPGed behavior
  // When damping < 0.9: Full envelope (oscillator mode)
  // When damping >= 0.9: Reduced envelope (FMLPGed mode)
  float envelope_amount = damping_ < 0.9f ? 1.0f : (1.0f - damping_) * 10.0f;
  
  // Amplitude envelope: RT60 decay calculation
  float amplitude_rt60 = 0.1f * SemitonesToRatio(damping_ * 96.0f) * kSampleRate;
  float amplitude_decay = 1.0f - powf(0.001f, 1.0f / amplitude_rt60);  // Decay coefficient

  // Brightness envelope: RT60 decay calculation (slightly faster)
  float brightness_rt60 = 0.1f * SemitonesToRatio(damping_ * 84.0f) * kSampleRate;
  float brightness_decay = 1.0f - powf(0.001f, 1.0f / brightness_rt60);
  
  // FM ratio: Quantize ratio from lookup table
  float ratio = Interpolate(lut_fm_frequency_quantizer, ratio_, 128.0f);
  // Modulator frequency: Carrier frequency * ratio
  float modulator_frequency = carrier_frequency_ * SemitonesToRatio(ratio);
  
  // Clamp modulator frequency: Prevent aliasing
  if (modulator_frequency > 0.5f) {
    modulator_frequency = 0.5f;  // Clamp to Nyquist
  }
  
  // Feedback: Convert feedback amount to bipolar (-1.0 to 1.0)
  float feedback = (feedback_amount_ - 0.5f) * 2.0f;
  
  // Parameter interpolation: Smooth parameter changes
  ParameterInterpolator carrier_increment(
      &previous_carrier_frequency_, carrier_frequency_, size);
  ParameterInterpolator modulator_increment(
      &previous_modulator_frequency_, modulator_frequency, size);
  ParameterInterpolator brightness(
      &previous_brightness_, brightness_, size);
  ParameterInterpolator feedback_amount(
      &previous_feedback_amount_, feedback, size);

  // Initialize phase accumulators and state
  uint32_t carrier_phase = carrier_phase_;
  uint32_t modulator_phase = modulator_phase_;
  float previous_sample = previous_sample_;
  
  // Main processing loop
  while (size--) {
    // Envelope follower: Extract amplitude and brightness from input
    float amplitude_envelope, brightness_envelope;
    follower_.Process(
        *in++,
        &amplitude_envelope,   // Amplitude envelope (from follower)
        &brightness_envelope); // Brightness envelope (from follower)
    
    // Brightness envelope: Modulate by amplitude envelope (creates dynamics)
    brightness_envelope *= 2.0f * amplitude_envelope * (2.0f - amplitude_envelope);
    
    // Update envelopes: Smooth with attack/decay
    SLOPE(amplitude_envelope_, amplitude_envelope, 0.05f, amplitude_decay);   // Fast attack
    SLOPE(brightness_envelope_, brightness_envelope, 0.01f, brightness_decay); // Very fast attack
    
    // Compute FM amount: Brightness-dependent modulation index
    float brightness_value = brightness.Next();
    brightness_value *= brightness_value;  // Square for smoother response
    // FM amount range: Minimum and maximum based on brightness
    float fm_amount_min = brightness_value < 0.5f
        ? 0.0f
        : brightness_value * 2.0f - 1.0f;  // 0.0 to 1.0
    float fm_amount_max = brightness_value < 0.5f
        ? 2.0f * brightness_value            // 0.0 to 1.0
        : 1.0f;
    // FM envelope: Blend between min and max based on brightness envelope
    float fm_envelope = 0.5f + envelope_amount * (brightness_envelope_ - 0.5f);
    float fm_amount = (fm_amount_min + fm_amount_max * fm_envelope) * 2.0f;
    // Smooth FM amount: Slew limiter for smooth changes
    SLEW(fm_amount_, fm_amount, 0.005f + fm_amount_max * 0.015f);

    // FM synthesis: Phase modulation with feedback
    // Phase feedback: Negative feedback modulates modulator phase
    float phase_feedback = feedback < 0.0f ? 0.5f * feedback * feedback : 0.0f;
    // Modulator phase: Increment with phase feedback from previous sample
    modulator_phase += static_cast<uint32_t>(4294967296.0f * \
      modulator_increment.Next() * (1.0f + previous_sample * phase_feedback));
    // Carrier phase: Simple increment
    carrier_phase += static_cast<uint32_t>(4294967296.0f * \
        carrier_increment.Next());

    // Modulator feedback: Positive feedback modulates modulator amplitude
    float feedback = feedback_amount.Next();
    float modulator_fb = feedback > 0.0f ? 0.25f * feedback * feedback : 0.0f;
    // Generate modulator: Sine wave with feedback modulation
    float modulator = SineFm(modulator_phase, modulator_fb * previous_sample);
    // Generate carrier: Sine wave with FM modulation
    float carrier = SineFm(carrier_phase, fm_amount_ * modulator);
    // Smooth previous sample: Low-pass filter for feedback
    ONE_POLE(previous_sample, carrier, 0.1f);

    // Compute amplitude envelope: Apply envelope to gain
    float gain = 1.0f + envelope_amount * (amplitude_envelope_ - 1.0f);
    // Smooth gain: Slew limiter (faster for higher FM amounts)
    ONE_POLE(gain_, gain, 0.005f + 0.045f * fm_amount_);
    
    // Output: Mix carrier and modulator
    *out++ = (carrier + 0.5f * modulator) * gain_;  // Main output (carrier + modulator)
    *aux++ = 0.5f * modulator * gain_;                // Aux output (modulator only)
  }
  // Store phase accumulators and state
  carrier_phase_ = carrier_phase;
  modulator_phase_ = modulator_phase;
  previous_sample_ = previous_sample;
}

}  // namespace rings
