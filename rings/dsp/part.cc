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
// Group of voices.

#include "rings/dsp/part.h"

#include "stmlib/dsp/units.h"

#include "rings/resources.h"

namespace rings {

using namespace std;
using namespace stmlib;

// Initialize Part: Sets up all voice objects, filters, and processing components
void Part::Init(uint16_t* reverb_buffer) {
  active_voice_ = 0;  // Start with voice 0 active
  
  fill(&note_[0], &note_[kMaxPolyphony], 0.0f);  // Initialize all note values to 0
  
  bypass_ = false;                    // Disable bypass mode
  polyphony_ = 1;                     // Default to monophonic (1 voice)
  model_ = RESONATOR_MODEL_MODAL;     // Default to modal resonator model
  dirty_ = true;                      // Mark for initial configuration
  
  // Initialize per-voice processing objects
  for (int32_t i = 0; i < kMaxPolyphony; ++i) {
    excitation_filter_[i].Init();     // Initialize excitation low-pass filters
    plucker_[i].Init();               // Initialize pluckers for noise burst exciter
    // Initialize DC blockers (high-pass filter with 10Hz cutoff)
    dc_blocker_[i].Init(1.0f - 10.0f / kSampleRate);
  }
  
  reverb_.Init(reverb_buffer);  // Initialize reverb with shared buffer
  limiter_.Init();              // Initialize limiter

  // Initialize note filter: Smooths note transitions and prevents glitches
  note_filter_.Init(
      kSampleRate / kMaxBlockSize,  // Update rate (2000 Hz: 48kHz / 24 samples)
      0.001f,  // Lag time with a sharp edge on the V/Oct input or trigger (1ms)
      0.010f,  // Lag time after the trigger has been received (10ms)
      0.050f,  // Time to transition from reactive to filtered (50ms)
      0.004f); // Prevent a sharp edge to partly leak on the previous voice (4ms)
}

// Configure resonators: Reinitializes resonator objects when model or polyphony changes
void Part::ConfigureResonators() {
  if (!dirty_) {
    return;  // No reconfiguration needed
  }
  
  switch (model_) {
    case RESONATOR_MODEL_MODAL:
      {
        // Modal resonator: Resolution scales with polyphony (more voices = fewer modes per voice)
        int32_t resolution = 64 / polyphony_ - 4;  // 1 voice: 60 modes, 2 voices: 28 modes, 4 voices: 12 modes
        for (int32_t i = 0; i < polyphony_; ++i) {
          resonator_[i].Init();
          resonator_[i].set_resolution(resolution);  // Set number of modal filters
        }
      }
      break;
    
    case RESONATOR_MODEL_SYMPATHETIC_STRING:
    case RESONATOR_MODEL_STRING:
    case RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED:
    case RESONATOR_MODEL_STRING_AND_REVERB:
      {
        // String models: Initialize strings with dispersion (for non-linear string models)
        float lfo_frequencies[kNumStrings] = {
          0.5f, 0.4f, 0.35f, 0.23f, 0.211f, 0.2f, 0.171f  // LFO frequencies for sympathetic modulation
        };
        for (int32_t i = 0; i < kNumStrings; ++i) {
          // Enable dispersion for non-linear string models (affects wave velocity)
          bool has_dispersion = model_ == RESONATOR_MODEL_STRING || \
              model_ == RESONATOR_MODEL_STRING_AND_REVERB;
          string_[i].Init(has_dispersion);

          // Initialize LFOs: Convert frequency to normalized rate (block-based)
          float f_lfo = float(kMaxBlockSize) / float(kSampleRate);  // Block duration
          f_lfo *= lfo_frequencies[i];  // Multiply by LFO frequency
          lfo_[i].Init<COSINE_OSCILLATOR_APPROXIMATE>(f_lfo);  // Initialize cosine LFO
        }
        // Reinitialize pluckers for internal exciter
        for (int32_t i = 0; i < polyphony_; ++i) {
          plucker_[i].Init();
        }
      }
      break;
    
    case RESONATOR_MODEL_FM_VOICE:
      {
        // FM voice: Initialize FM synthesis voices
        for (int32_t i = 0; i < polyphony_; ++i) {
          fm_voice_[i].Init();
        }
      }
      break;
    
    default:
      break;
  }

  // Ensure active voice is within valid range
  if (active_voice_ >= polyphony_) {
    active_voice_ = 0;
  }
  dirty_ = false;  // Mark as configured
}

#ifdef BRYAN_CHORDS

// Chord table by Bryan Noll:
float chords[kMaxPolyphony][11][8] = {
  {
    { -12.0f, -0.01f, 0.0f,  0.01f, 0.02f, 11.98f, 11.99f, 12.0f }, // OCT
    { -12.0f, -5.0f,  0.0f,  6.99f, 7.0f,  11.99f, 12.0f,  19.0f }, // 5
    { -12.0f, -5.0f,  0.0f,  5.0f,  7.0f,  11.99f, 12.0f,  17.0f }, // sus4
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 12.0f,  19.0f }, // m 
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  19.0f }, // m7
    { -12.0f, -5.0f,  0.0f,  3.0f, 14.0f,   3.01f, 10.0f,  19.0f }, // m9
    { -12.0f, -5.0f,  0.0f,  3.0f,  7.0f,   3.01f, 10.0f,  17.0f }, // m11
    { -12.0f, -5.0f,  0.0f,  2.0f,  7.0f,   9.0f,  16.0f,  19.0f }, // 69
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  14.0f,  19.0f }, // M9
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.0f,  10.99f, 19.0f }, // M7
    { -12.0f, -5.0f,  0.0f,  4.0f,  7.0f,  11.99f, 12.0f,  19.0f } // M
  },
  { 
    { -12.0f, 0.0f,  0.01f, 12.0f }, // OCT
    { -12.0f, 6.99f, 7.0f,  12.0f }, // 5
    { -12.0f, 5.0f,  7.0f,  12.0f }, // sus4
    { -12.0f, 3.0f, 11.99f, 12.0f }, // m 
    { -12.0f, 3.0f, 10.0f,  12.0f }, // m7
    { -12.0f, 3.0f, 10.0f,  14.0f }, // m9
    { -12.0f, 3.0f, 10.0f,  17.0f }, // m11
    { -12.0f, 2.0f,  9.0f,  16.0f }, // 69
    { -12.0f, 4.0f, 11.0f,  14.0f }, // M9
    { -12.0f, 4.0f,  7.0f,  11.0f }, // M7
    { -12.0f, 4.0f,  7.0f,  12.0f }, // M
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 9.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  }
};

#else

// Original chord table
float chords[kMaxPolyphony][11][8] = {
  {
    { -12.0f, 0.0f, 0.01f, 0.02f, 0.03f, 11.98f, 11.99f, 12.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  9.99f,  10.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  11.99f, 12.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  13.99f, 14.0f,  19.0f },
    { -12.0f, 0.0f, 3.0f,  3.01f, 7.0f,  16.99f, 17.0f,  19.0f },
    { -12.0f, 0.0f, 6.98f, 6.99f, 7.0f,  12.00f, 18.99f, 19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  16.99f, 17.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  13.99f, 14.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  11.99f, 12.0f,  19.0f },
    { -12.0f, 0.0f, 3.99f, 4.0f,  7.0f,  10.99f, 11.0f,  19.0f },
    { -12.0f, 0.0f, 4.99f, 5.0f,  7.0f,  11.99f, 12.0f,  17.0f }
  },
  { 
    { -12.0f, 0.0f, 0.01f, 12.0f },
    { -12.0f, 3.0f, 7.0f,  10.0f },
    { -12.0f, 3.0f, 7.0f,  12.0f },
    { -12.0f, 3.0f, 7.0f,  14.0f },
    { -12.0f, 3.0f, 7.0f,  17.0f },
    { -12.0f, 7.0f, 12.0f, 19.0f },
    { -12.0f, 4.0f, 7.0f,  17.0f },
    { -12.0f, 4.0f, 7.0f,  14.0f },
    { -12.0f, 4.0f, 7.0f,  12.0f },
    { -12.0f, 4.0f, 7.0f,  11.0f },
    { -12.0f, 5.0f, 7.0f,  12.0f },
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 0.01f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  },
  {
    { 0.0f, -12.0f },
    { 0.0f, 0.01f },
    { 0.0f, 2.0f },
    { 0.0f, 3.0f },
    { 0.0f, 4.0f },
    { 0.0f, 5.0f },
    { 0.0f, 7.0f },
    { 0.0f, 10.0f },
    { 0.0f, 11.0f },
    { 0.0f, 12.0f },
    { -12.0f, 12.0f }
  }
};

#endif  // BRYAN_CHORDS

// Compute sympathetic strings notes: Calculates frequencies for sympathetic string network
// Interpolates between harmonic intervals based on structure parameter
void Part::ComputeSympatheticStringsNotes(
    float tonic,              // Base frequency (MIDI note)
    float note,               // Main note (MIDI note)
    float parameter,          // Structure parameter (0-1) or chord index (if >= 2.0)
    float* destination,       // Output array for string frequencies (MIDI notes)
    size_t num_strings) {     // Number of strings to compute
  // Base interval table: Harmonic intervals relative to main note (in semitones)
  float notes[9] = {
    tonic,                    // Tonic (base frequency)
    note - 12.0f,             // Octave below
    note - 7.01955f,          // Fifth below (just intonation)
    note,                     // Unison
    note + 7.01955f,          // Fifth above
    note + 12.0f,             // Octave above
    note + 19.01955f,         // Octave + fifth above
    note + 24.0f,             // Two octaves above
    note + 24.0f              // Two octaves above (duplicate for safety)
  };
  // Detuning amounts: Small detunings for secondary strings (cycled)
  const float detunings[4] = {
      0.013f, 0.011f, 0.007f, 0.017f  // Semitones
  };
  
  // Quantized chord mode: Use chord table when parameter >= 2.0
  if (parameter >= 2.0f) {
    int32_t chord_index = parameter - 2.0f;  // Extract chord index
    const float* chord = chords[polyphony_ - 1][chord_index];  // Get chord for current polyphony
    for (size_t i = 0; i < num_strings; ++i) {
      destination[i] = chord[i] + note;  // Add chord intervals to main note
    }
    return;
  }

  // Continuous mode: Interpolate between intervals using structure parameter
  size_t num_detuned_strings = (num_strings - 1) >> 1;  // Half of strings get detuned
  size_t first_detuned_string = num_strings - num_detuned_strings;  // Starting index for detuned strings
  
  for (size_t i = 0; i < first_detuned_string; ++i) {
    float note = 3.0f;  // Default to index 3 (unison)
    if (i != 0) {
      // Interpolate between intervals: parameter * 7.0 maps to interval index
      note = parameter * 7.0f;
      // Gradually approach unison as parameter increases (damping effect)
      parameter += (1.0f - parameter) * 0.2f;
    }
    
    // Split note into integral and fractional parts
    MAKE_INTEGRAL_FRACTIONAL(note);
    // Apply squash function to fractional part for smooth interpolation
    note_fractional = Squash(note_fractional);

    // Linear interpolation between adjacent intervals
    float a = notes[note_integral];
    float b = notes[note_integral + 1];
    note = a + (b - a) * note_fractional;
    destination[i] = note;
    
    // Add detuned copy of string (for realism)
    if (i + first_detuned_string < num_strings) {
      destination[i + first_detuned_string] = destination[i] + detunings[i & 3];  // Cycle detunings
    }
  }
}

// Render modal voice: Processes audio through modal resonator (bank of modal filters)
void Part::RenderModalVoice(
    int32_t voice,                              // Voice index
    const PerformanceState& performance_state,  // Performance state
    const Patch& patch,                         // Patch parameters
    float frequency,                            // Voice frequency (normalized)
    float filter_cutoff,                        // Excitation filter cutoff
    size_t size) {                              // Block size
  // Internal exciter: Pulse generator triggered on strum (pre-filter)
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    // Generate pulse: Amplitude scales with filter cutoff (brightness-dependent)
    // Pulse amplitude: 0.25 * SemitonesToRatio(cutoff^2 * 24) / cutoff
    resonator_input_[0] += 0.25f * SemitonesToRatio(
        filter_cutoff * filter_cutoff * 24.0f) / filter_cutoff;
  }
  
  // Process input through excitation filter: Low-pass filter based on brightness
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  // Configure and process through modal resonator
  Resonator& r = resonator_[voice];
  r.set_frequency(frequency);                            // Set fundamental frequency
  r.set_structure(patch.structure);                      // Set inharmonicity
  r.set_brightness(patch.brightness * patch.brightness); // Squared for smoother response
  r.set_position(patch.position);                        // Set excitation point
  r.set_damping(patch.damping);                          // Set decay time
  r.Process(resonator_input_, out_buffer_, aux_buffer_, size);  // Process audio
}

// Render FM voice: Processes audio through FM synthesis voice
void Part::RenderFMVoice(
    int32_t voice,                               // Voice index
    const PerformanceState& performance_state,   // Performance state
    const Patch& patch,                          // Patch parameters
    float frequency,                             // Voice frequency (normalized)
    float filter_cutoff,                         // Excitation filter cutoff (unused for FM)
    size_t size) {                               // Block size
  FMVoice& v = fm_voice_[voice];
  // Internal exciter: Trigger envelope on strum
  if (performance_state.internal_exciter &&
      voice == active_voice_ &&
      performance_state.strum) {
    v.TriggerInternalEnvelope();  // Trigger internal envelope
  }

  // Configure FM voice parameters
  v.set_frequency(frequency);              // Set carrier frequency
  v.set_ratio(patch.structure);            // Set FM ratio (modulator/carrier)
  v.set_brightness(patch.brightness);      // Set brightness (modulation index)
  v.set_feedback_amount(patch.position);   // Set feedback amount
  v.set_position(/*patch.position*/ 0.0f); // Position not used for FM (set to 0)
  v.set_damping(patch.damping);            // Set damping (envelope decay)
  v.Process(resonator_input_, out_buffer_, aux_buffer_, size);  // Process audio
}

// Render string voice: Processes audio through string model with sympathetic resonance
void Part::RenderStringVoice(
    int32_t voice,                              // Voice index
    const PerformanceState& performance_state,  // Performance state
    const Patch& patch,                         // Patch parameters
    float frequency,                            // Voice frequency (normalized)
    float filter_cutoff,                        // Excitation filter cutoff
    size_t size) {                              // Block size
  // Compute number of strings and their frequencies
  int32_t num_strings = 1;  // Default: single string
  float frequencies[kNumStrings];

  // Sympathetic strings models: Compute multiple string frequencies
  if (model_ == RESONATOR_MODEL_SYMPATHETIC_STRING ||
      model_ == RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED) {
    num_strings = 2 * kMaxPolyphony / polyphony_;  // 2 strings per voice (8 total for 1 voice, 4 for 2, 2 for 4)
    // Use structure parameter or chord index
    float parameter = model_ == RESONATOR_MODEL_SYMPATHETIC_STRING
        ? patch.structure                    // Continuous mode: use structure parameter
        : 2.0f + performance_state.chord;    // Quantized mode: use chord index (offset by 2.0)
    // Compute string frequencies based on intervals
    ComputeSympatheticStringsNotes(
        performance_state.tonic + performance_state.fm,                 // Base frequency
        performance_state.tonic + note_[voice] + performance_state.fm,  // Main note
        parameter,
        frequencies,
        num_strings);
    // Convert MIDI notes to normalized frequencies
    for (int32_t i = 0; i < num_strings; ++i) {
      frequencies[i] = SemitonesToRatio(frequencies[i] - 69.0f) * a3;  // A3 = MIDI 69
    }
  } else {
    frequencies[0] = frequency;  // Single string: use voice frequency
  }

  // Gain compensation: Reduce input gain for multiple strings (prevents clipping)
  if (voice == active_voice_) {
    const float gain = 1.0f / Sqrt(static_cast<float>(num_strings) * 2.0f);
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] *= gain;
    }
  }

  // Process external input through excitation filter
  excitation_filter_[voice].Process<FILTER_MODE_LOW_PASS>(
      resonator_input_, resonator_input_, size);

  // Internal exciter: Add noise burst (plucker) triggered on strum
  if (performance_state.internal_exciter) {
    if (voice == active_voice_ && performance_state.strum) {
      // Trigger plucker: frequency, cutoff (8x filter cutoff), position
      plucker_[voice].Trigger(frequency, filter_cutoff * 8.0f, patch.position);
    }
    plucker_[voice].Process(noise_burst_buffer_, size);  // Generate noise burst
    for (size_t i = 0; i < size; ++i) {
      resonator_input_[i] += noise_burst_buffer_[i];     // Add to input
    }
  }
  // DC blocking: Remove DC offset from input
  dc_blocker_[voice].Process(resonator_input_, size);
  
  // Clear output buffers
  fill(&out_buffer_[0], &out_buffer_[size], 0.0f);
  fill(&aux_buffer_[0], &aux_buffer_[size], 0.0f);
  
  // Compute dispersion: Frequency-dependent wave velocity (stiffness)
  float structure = patch.structure;
  float dispersion = structure < 0.24f
      ? (structure - 0.24f) * 4.166f      // Negative dispersion (stiff string)
      : (structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f);  // Positive dispersion (flexible string)
  
  // Process each string
  for (int32_t string = 0; string < num_strings; ++string) {
    int32_t i = voice + string * polyphony_;  // String index (interleaved across voices)
    String& s = string_[i];
    float lfo_value = lfo_[i].Next();  // Get LFO value for modulation
    
    float brightness = patch.brightness;
    float damping = patch.damping;
    float position = patch.position;
    float glide = 1.0f;  // Frequency glide (for sympathetic strings)
    float string_index = static_cast<float>(string) / static_cast<float>(num_strings);
    const float* input = resonator_input_;  // Default: use external input
    
    // String+reverb model: Modify damping curve
    if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
      damping *= (2.0f - damping);  // Non-linear damping curve
    }
    
    // Sympathetic resonance: Modify parameters for secondary strings
    // When internal exciter is used: string 0 is main source, others are sympathetic
    // When external exciter is used: all strings vibrate sympathetically
    if (string > 0 && performance_state.internal_exciter) {
      brightness *= (2.0f - brightness);  // Reduce brightness (squared)
      brightness *= (2.0f - brightness);
      damping = 0.7f + patch.damping * 0.27f;  // Increase damping (longer decay)
      // LFO modulation: Amount based on position (max at 0.5)
      float amount = (0.5f - fabs(0.5f - patch.position)) * 0.9f;
      position = patch.position + lfo_value * amount;  // Modulate position
      // Frequency glide: Based on brightness (creates detuning)
      glide = SemitonesToRatio((brightness - 1.0f) * 36.0f);
      input = sympathetic_resonator_input_;  // Use sympathetic input (from string 0)
    }
    
    // Configure and process string
    s.set_dispersion(dispersion);                    // Set dispersion (stiffness)
    s.set_frequency(frequencies[string], glide);     // Set frequency and glide
    s.set_brightness(brightness);                    // Set brightness
    s.set_position(position);                        // Set pluck position
    s.set_damping(damping + string_index * (0.95f - damping));  // Increase damping for higher strings
    s.Process(input, out_buffer_, aux_buffer_, size);  // Process audio
    
    // Sympathetic coupling: Extract signal from string 0 for sympathetic strings
    if (string == 0) {
      // Gain: 0.2 / num_strings (was 0.1, changed to 0.2 by Ben Wilson)
      float gain = 0.2f / static_cast<float>(num_strings);
      for (size_t i = 0; i < size; ++i) {
        float sum = out_buffer_[i] - aux_buffer_[i];   // Difference signal
        sympathetic_resonator_input_[i] = gain * sum;  // Store for sympathetic strings
      }
    }
  }
}

// Ping-pong pattern: Special voice allocation pattern for 3-voice mode (unused in current implementation)
const int32_t kPingPattern[] = {
  1, 0, 2, 1, 0, 2, 1, 0  // Pattern cycles through voices 1, 0, 2
};

// Main audio processing function: Coordinates polyphonic voice processing
void Part::Process(
    const PerformanceState& performance_state,   // Performance state (notes, strum, etc.)
    const Patch& patch,                          // Patch parameters
    const float* in,                             // Input audio buffer
    float* out,                                  // Output buffer (odd partials/voices)
    float* aux,                                  // Auxiliary output buffer (even partials/voices)
    size_t size) {                               // Block size (typically 24 samples)

  // Bypass mode: Pass input directly to outputs
  if (bypass_) {
    copy(&in[0], &in[size], &out[0]);
    copy(&in[0], &in[size], &aux[0]);
    return;
  }
  
  // Configure resonators if model or polyphony changed
  ConfigureResonators();
  
  // Process note filter: Smooths note transitions and prevents glitches
  note_filter_.Process(
      performance_state.note,      // Current note input
      performance_state.strum);    // Strum trigger

  // Voice allocation: Assign new note to active voice on strum
  if (performance_state.strum) {
    note_[active_voice_] = note_filter_.stable_note();  // Assign stable note to current voice
    // Voice allocation: Round-robin or ping-pong pattern
    if (polyphony_ > 1 && polyphony_ & 1) {
      // Ping-pong pattern for odd polyphony (3 voices, though max is 4)
      active_voice_ = kPingPattern[step_counter_ % 8];
      step_counter_ = (step_counter_ + 1) % 8;
    } else {
      // Standard round-robin allocation
      active_voice_ = (active_voice_ + 1) % polyphony_;
    }
  }
  
  // Update active voice note: Follow note filter output
  note_[active_voice_] = note_filter_.note();
  
  // Clear output buffers
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  
  // Process each voice
  for (int32_t voice = 0; voice < polyphony_; ++voice) {
    // Compute MIDI note value, frequency, and cutoff frequency for excitation filter
    float cutoff = patch.brightness * (2.0f - patch.brightness);  // Brightness curve
    float note = note_[voice] + performance_state.tonic + performance_state.fm;  // Total note with transpose and FM
    float frequency = SemitonesToRatio(note - 69.0f) * a3;  // Convert MIDI note to normalized frequency (A3 = 69)
    
    // Filter cutoff: Different ranges for internal vs external exciter
    float filter_cutoff_range = performance_state.internal_exciter
      ? frequency * SemitonesToRatio((cutoff - 0.5f) * 96.0f)   // Internal: frequency-relative (±48 semitones)
      : 0.4f * SemitonesToRatio((cutoff - 1.0f) * 108.0f);      // External: fixed range (±54 semitones)
    
    // Filter cutoff: Active voice uses full range, inactive voices use minimum (10Hz)
    float filter_cutoff = min(voice == active_voice_
      ? filter_cutoff_range
      : (10.0f / kSampleRate), 0.499f);  // Clamp to Nyquist/2
    float filter_q = performance_state.internal_exciter ? 1.5f : 0.8f;  // Q factor

    // Process input with excitation filter: Inactive voices receive silence
    excitation_filter_[voice].set_f_q<FREQUENCY_DIRTY>(filter_cutoff, filter_q);
    if (voice == active_voice_) {
      copy(&in[0], &in[size], &resonator_input_[0]);  // Active voice: copy input
    } else {
      fill(&resonator_input_[0], &resonator_input_[size], 0.0f);  // Inactive voices: silence
    }
    
    // Route to appropriate resonator model
    if (model_ == RESONATOR_MODEL_MODAL) {
      RenderModalVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else if (model_ == RESONATOR_MODEL_FM_VOICE) {
      RenderFMVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    } else {
      RenderStringVoice(
          voice, performance_state, patch, frequency, filter_cutoff, size);
    }
    
    // Output routing: Different for monophonic vs polyphonic
    if (polyphony_ == 1) {
      // Monophonic: Send odd/even partials to separate outputs
      for (size_t i = 0; i < size; ++i) {
        out[i] += out_buffer_[i];   // Odd partials/harmonics
        aux[i] += aux_buffer_[i];   // Even partials/harmonics
      }
    } else {
      // Polyphonic: Dispatch odd/even voices to separate outputs
      float* destination = voice & 1 ? aux : out;  // Odd voices -> aux, even voices -> out
      for (size_t i = 0; i < size; ++i) {
        destination[i] += out_buffer_[i] - aux_buffer_[i];  // Difference signal
      }
    }
  }
  
  // String+reverb model: Apply reverb processing
  if (model_ == RESONATOR_MODEL_STRING_AND_REVERB) {
    // Stereo crossfade: Position parameter controls crossfade amount
    for (size_t i = 0; i < size; ++i) {
      float l = out[i];
      float r = aux[i];
      out[i] = l * patch.position + (1.0f - patch.position) * r;  // Crossfade
      aux[i] = r * patch.position + (1.0f - patch.position) * l;
    }
    // Configure reverb parameters
    reverb_.set_amount(0.1f + patch.damping * 0.5f);         // Amount: 0.1-0.6
    reverb_.set_diffusion(0.625f);                           // Diffusion: fixed
    reverb_.set_time(0.35f + 0.63f * patch.damping);         // Time: 0.35-0.98
    reverb_.set_input_gain(0.2f);                            // Input gain: fixed
    reverb_.set_lp(0.3f + patch.brightness * 0.6f);          // LP cutoff: 0.3-0.9
    reverb_.Process(out, aux, size);                         // Process reverb
    // Invert aux channel: Creates stereo width
    for (size_t i = 0; i < size; ++i) {
      aux[i] = -aux[i];
    }
  }
  
  // Apply limiter: Prevents clipping with model-specific gain compensation
  limiter_.Process(out, aux, size, model_gains_[model_]);
}

/* static */
// Model-specific output gain compensation: Prevents clipping and balances output levels
float Part::model_gains_[] = {
  1.4f,  // RESONATOR_MODEL_MODAL (modal resonators are louder)
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING (normal gain)
  1.4f,  // RESONATOR_MODEL_STRING (string models are louder)
  0.7f,  // RESONATOR_MODEL_FM_VOICE (FM voice is quieter, needs boost)
  1.0f,  // RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED (normal gain)
  1.4f,  // RESONATOR_MODEL_STRING_AND_REVERB (string+reverb is louder)
};

}  // namespace rings