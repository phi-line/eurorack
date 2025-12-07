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
// Comb filter / KS string.

#include "rings/dsp/string.h"

#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/random.h"

#include "rings/resources.h"

namespace rings {
  
using namespace std;
using namespace stmlib;

// Initialize string: Set up delay lines, filters, and default parameters
void String::Init(bool enable_dispersion) {
  enable_dispersion_ = enable_dispersion;  // Enable dispersion for non-linear string models
  
  // Initialize delay lines and filters
  string_.Init();                    // Main comb filter delay line
  stretch_.Init();                   // Stiffness delay line (for dispersion)
  fir_damping_filter_.Init();        // FIR damping filter
  iir_damping_filter_.Init();        // IIR damping filter
  
  // Set default parameters: A4 (220 Hz), moderate dispersion, brightness, damping
  set_frequency(220.0f / kSampleRate);  // A4 normalized frequency
  set_dispersion(0.25f);                // Moderate dispersion
  set_brightness(0.5f);                 // Medium brightness
  set_damping(0.3f);                    // Medium damping
  set_position(0.8f);                   // Near end pluck position
  
  // Initialize state variables
  delay_ = 1.0f / frequency_;              // Calculate delay from frequency
  clamped_position_ = 0.0f;                  // Clamped position (for interpolation)
  previous_dispersion_ = 0.0f;              // Previous dispersion (for interpolation)
  dispersion_noise_ = 0.0f;                 // Dispersion noise (for non-linear models)
  curved_bridge_ = 0.0f;                   // Curved bridge effect
  previous_damping_compensation_ = 0.0f;    // Previous damping compensation
  
  // Initialize upsampler buffers
  out_sample_[0] = out_sample_[1] = 0.0f;  // Output sample buffer
  aux_sample_[0] = aux_sample_[1] = 0.0f;  // Auxiliary sample buffer
  
  // Initialize DC blocker: High-pass filter with 20Hz cutoff
  dc_blocker_.Init(1.0f - 20.0f / kSampleRate);
}

// ProcessInternal: Template-based string processing (dispersion enabled/disabled at compile time)
template<bool enable_dispersion>
void String::ProcessInternal(
    const float* in,   // Input excitation signal
    float* out,         // Output buffer (odd harmonics)
    float* aux,         // Auxiliary output buffer (even harmonics)
    size_t size) {      // Block size
  // Calculate delay: Convert frequency to delay length (in samples)
  float delay = 1.0f / frequency_;
  // Constrain delay: Minimum 4 samples, maximum delay line size - 4
  CONSTRAIN(delay, 4.0f, kDelayLineSize - 4.0f);
  
  // Upsampler: For very low frequencies (<11.7Hz), delay line is too short
  // Play at lowest possible note and upsample with linear interpolator
  float src_ratio = delay * frequency_;  // Upsampling ratio
  if (src_ratio >= 0.9999f) {
    // Above 11.7 Hz: Disable upsampler (normal operation)
    src_phase_ = 1.0f;
    src_ratio = 1.0f;
  }

  // Clamp position: Prevent extreme values (0.01 to 0.99 range)
  float clamped_position = 0.5f - 0.98f * fabs(position_ - 0.5f);
  
  // Parameter interpolation: Smooth parameter changes
  ParameterInterpolator delay_modulation(
      &delay_, delay, size);  // Delay interpolation
  ParameterInterpolator position_modulation(
      &clamped_position_, clamped_position, size);  // Position interpolation
  ParameterInterpolator dispersion_modulation(
      &previous_dispersion_, dispersion_, size);  // Dispersion interpolation
  
  // Damping calculation: Convert damping parameter to RT60 and filter coefficients
  float lf_damping = damping_ * (2.0f - damping_);  // Non-linear damping curve
  // RT60: Decay time to -60dB (0.07s base, scaled by damping)
  float rt60 = 0.07f * SemitonesToRatio(lf_damping * 96.0f) * kSampleRate;
  // Damping coefficient: Convert RT60 to filter coefficient
  float rt60_base_2_12 = max(-120.0f * delay / src_ratio / rt60, -127.0f);
  float damping_coefficient = SemitonesToRatio(rt60_base_2_12);
  
  // Brightness: Squared for smoother response
  float brightness = brightness_ * brightness_;
  // Noise filter: Brightness-dependent cutoff for dispersion noise
  float noise_filter = SemitonesToRatio((brightness_ - 1.0f) * 48.0f);
  // Damping cutoff: Frequency-dependent damping filter cutoff
  float damping_cutoff = min(
      24.0f + damping_ * damping_ * 48.0f + brightness_ * brightness_ * 24.0f,
      84.0f);
  float damping_f = min(frequency_ * SemitonesToRatio(damping_cutoff), 0.499f);
  
  // Infinite decay mode: Crossfade to infinite decay when damping >= 0.95
  if (damping_ >= 0.95f) {
    float to_infinite = 20.0f * (damping_ - 0.95f);  // Crossfade amount (0.0-1.0)
    damping_coefficient += to_infinite * (1.0f - damping_coefficient);
    brightness += to_infinite * (1.0f - brightness);
    damping_f += to_infinite * (0.4999f - damping_f);
    damping_cutoff += to_infinite * (128.0f - damping_cutoff);
  }
  
  // Configure filters: Set damping filter and IIR filter parameters
  fir_damping_filter_.Configure(damping_coefficient, brightness, size);
  iir_damping_filter_.set_f_q<FREQUENCY_ACCURATE>(damping_f, 0.5f);
  // Damping compensation: Compensate for IIR filter delay
  ParameterInterpolator damping_compensation_modulation(
      &previous_damping_compensation_,
      1.0f - Interpolate(lut_svf_shift, damping_cutoff, 1.0f),
      size);
  
  // Main processing loop: Process each sample
  while (size--) {
    // Upsampler phase: Increment phase accumulator
    src_phase_ += src_ratio;
    if (src_phase_ > 1.0f) {
      src_phase_ -= 1.0f;  // Reset phase, process new sample
      
      // Get interpolated parameters
      float delay = delay_modulation.Next();                    // Main delay
      float comb_delay = delay * position_modulation.Next();    // Comb delay (position-dependent)
    
#ifndef MIC_W
      // Damping compensation: Compensate for IIR filter delay
      delay *= damping_compensation_modulation.Next();
#endif  // MIC_W
      delay -= 1.0f;  // FIR delay: Subtract 1 sample (comb filter)
    
      float s = 0.0f;  // Sample from delay line

      // Dispersion processing: Non-linear string model with dispersion
      if (enable_dispersion) {
        // Dispersion noise: Filtered white noise for frequency modulation
        float noise = 2.0f * Random::GetFloat() - 1.0f;  // White noise: -1.0 to 1.0
        noise *= 1.0f / (0.2f + noise_filter);          // Scale by noise filter
        dispersion_noise_ += noise_filter * (noise - dispersion_noise_);  // Low-pass filter

        float dispersion = dispersion_modulation.Next();
        // Stretch point: Allpass filter position (for dispersion)
        float stretch_point = dispersion <= 0.0f
            ? 0.0f
            : dispersion * (2.0f - dispersion) * 0.475f;
        // Noise amount: Amount of frequency modulation (for dispersion > 0.75)
        float noise_amount = dispersion > 0.75f
            ? 4.0f * (dispersion - 0.75f)
            : 0.0f;
        // Bridge curving: Non-linear boundary condition (for negative dispersion)
        float bridge_curving = dispersion < 0.0f
            ? -dispersion
            : 0.0f;
        
        noise_amount = noise_amount * noise_amount * 0.025f;  // Square and scale
        float ac_blocking_amount = bridge_curving;            // AC blocking amount

        bridge_curving = bridge_curving * bridge_curving * 0.01f;  // Square and scale
        // Allpass gain: Golden ratio-based gain for allpass filter
        float ap_gain = -0.618f * dispersion / (0.15f + fabs(dispersion));
        
        // Delay frequency modulation: Apply dispersion noise and bridge curving
        float delay_fm = 1.0f;
        delay_fm += dispersion_noise_ * noise_amount;      // Add noise modulation
        delay_fm -= curved_bridge_ * bridge_curving;       // Subtract bridge curving
        delay *= delay_fm;                                  // Modulate delay
        
        // Split delay: Main delay and allpass delay (for dispersion)
        float ap_delay = delay * stretch_point;
        float main_delay = delay - ap_delay;
        if (ap_delay >= 4.0f && main_delay >= 4.0f) {
          // Both delays valid: Use allpass filter for dispersion
          s = string_.ReadHermite(main_delay);              // Read from main delay
          s = stretch_.Allpass(s, ap_delay, ap_gain);       // Apply allpass filter
        } else {
          // Delays too short: Fall back to simple delay
          s = string_.ReadHermite(delay);
        }
        // AC blocking: Remove DC component (for bridge curving)
        float s_ac = s;
        dc_blocker_.Process(&s_ac, 1);
        s += ac_blocking_amount * (s_ac - s);
        
        // Curved bridge: Non-linear boundary condition (rectifier-like)
        float value = fabs(s) - 0.025f;
        float sign = s > 0.0f ? 1.0f : -1.5f;  // Asymmetric rectification
        curved_bridge_ = (fabs(value) + value) * sign;  // Half-wave rectification
      } else {
        // No dispersion: Simple delay line read
        s = string_.ReadHermite(delay);
      }
    
      // Add input: Mix excitation signal (causes bitcrushing for f0 < 11.7Hz)
      s += *in;
      // Apply damping filters: FIR and IIR filters
      s = fir_damping_filter_.Process(s);  // FIR damping filter
#ifndef MIC_W
      s = iir_damping_filter_.Process<FILTER_MODE_LOW_PASS>(s);  // IIR damping filter
#endif  // MIC_W
      // Write to delay line: Feedback signal
      string_.Write(s);

      // Update upsampler buffers: Store samples for interpolation
      out_sample_[1] = out_sample_[0];
      aux_sample_[1] = aux_sample_[0];

      out_sample_[0] = s;                          // Main output sample
      aux_sample_[0] = string_.Read(comb_delay);   // Comb output sample (position-dependent)
    }
    // Upsampler output: Crossfade between samples for smooth interpolation
    *out++ += Crossfade(out_sample_[1], out_sample_[0], src_phase_);
    *aux++ += Crossfade(aux_sample_[1], aux_sample_[0], src_phase_);
    in++;
  }
}

// Process: Main processing function (routes to template-based implementation)
void String::Process(const float* in, float* out, float* aux, size_t size) {
  // Route to appropriate template instantiation based on dispersion setting
  if (enable_dispersion_) {
    ProcessInternal<true>(in, out, aux, size);   // With dispersion (non-linear string)
  } else {
    ProcessInternal<false>(in, out, aux, size);  // Without dispersion (simple comb filter)
  }
}

}  // namespace rings
