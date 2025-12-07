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
// String synth part.

#include "rings/dsp/string_synth_part.h"

#include "rings/dsp/dsp.h"

namespace rings {

using namespace std;
using namespace stmlib;

// Initialize string synth: Set up voices, groups, filters, and effects
void StringSynthPart::Init(uint16_t* reverb_buffer) {
  active_group_ = 0;        // Start with group 0 active
  acquisition_delay_ = 0;   // Reset acquisition delay
  
  polyphony_ = 1;           // Default to monophonic (1 group)
  fx_type_ = FX_ENSEMBLE;   // Default FX: Ensemble effect

  // Initialize all 12 voices: Each voice has 3 harmonics
  for (int32_t i = 0; i < kStringSynthVoices; ++i) {
    voice_[i].Init();  // Initialize harmonic oscillators
  }
  
  // Initialize voice groups: Up to 4 groups for polyphony
  for (int32_t i = 0; i < kMaxStringSynthPolyphony; ++i) {
    group_[i].tonic = 0.0f;        // Initialize tonic to 0
    group_[i].envelope.Init();     // Initialize AD envelope
  }
  
  // Initialize formant filters: 3 filters for vowel sounds
  for (int32_t i = 0; i < kNumFormants; ++i) {
    formant_filter_[i].Init();  // Initialize bandpass filters
  }
  
  limiter_.Init();  // Initialize output limiter
  
  // Initialize effects: All share the same reverb buffer
  reverb_.Init(reverb_buffer);   // Initialize reverb
  chorus_.Init(reverb_buffer);   // Initialize chorus
  ensemble_.Init(reverb_buffer);  // Initialize ensemble
  
  // Initialize note filter: Smooths note transitions (faster than main Part)
  note_filter_.Init(
      kSampleRate / kMaxBlockSize,  // Update rate (2000 Hz)
      0.001f,  // Lag time with a sharp edge on the V/Oct input or trigger (1ms)
      0.005f,  // Lag time after the trigger has been received (5ms, faster than Part)
      0.050f,  // Time to transition from reactive to filtered (50ms)
      0.004f); // Prevent a sharp edge to partly leak on the previous voice (4ms)
}

// Registration table: 11 different harmonic combinations (organ-like registrations)
// Each registration defines amplitudes for 3 harmonics × 2 (odd/even) = 6 values
const int32_t kRegistrationTableSize = 11;
const float registrations[kRegistrationTableSize][kNumHarmonics * 2] = {
  { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },  // Registration 0: Fundamental only
  { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },  // Registration 1: Fundamental + 2nd harmonic
  { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },  // Registration 2: Fundamental + 3rd harmonic
  { 1.0f, 0.1f, 0.0f, 0.0f, 1.0f, 0.0f },  // Registration 3: Fundamental + 4th harmonic
  { 1.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f },  // Registration 4: Mixed harmonics
  { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },  // Registration 5: All harmonics (full)
  { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f },  // Registration 6: No fundamental
  { 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f },  // Registration 7: Upper harmonics
  { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f },  // Registration 8: 3rd and 4th only
  { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },  // Registration 9: 4th harmonic only
  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },  // Registration 10: Even harmonics only
};

// Compute registration: Calculate harmonic amplitudes based on registration parameter
// Interpolates between registration table entries and normalizes
void StringSynthPart::ComputeRegistration(
    float gain,              // Overall gain
    float registration,      // Registration parameter (0.0-1.0, maps to table index)
    float* amplitudes) {      // Output: Harmonic amplitudes (6 values: 3 harmonics × 2)
  // Map registration to table index: 0.0-1.0 -> 0-10
  registration *= (kRegistrationTableSize - 1.001f);
  MAKE_INTEGRAL_FRACTIONAL(registration);  // Split into integral and fractional parts
  
  // Interpolate between adjacent registrations
  float total = 0.0f;
  for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
    float a = registrations[registration_integral][i];      // Lower registration
    float b = registrations[registration_integral + 1][i];  // Upper registration
    amplitudes[i] = a + (b - a) * registration_fractional;    // Linear interpolation
    total += amplitudes[i];  // Sum for normalization
  }
  // Normalize and apply gain: Ensure constant total amplitude
  for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
    amplitudes[i] = gain * amplitudes[i] / total;  // Normalize and scale
  }
}

#ifdef BRYAN_CHORDS

// Chord table by Bryan Noll:
// - more compact, leaving room for a bass
// - more frequent note changes between adjacent chords.
// - dropped fifth.
const float chords[kMaxStringSynthPolyphony][kNumChords][kMaxChordSize] = {
  {
    { -12.0f, -0.01f,  0.0f,  0.01f,  0.02f, 11.99f, 12.0f, 24.0f }, // OCT
    { -12.0f, -5.01f, -5.0f,  0.0f,   7.0f,  12.0f,  19.0f, 24.0f }, // 5
    { -12.0f, -5.0f,   0.0f,  5.0f,   7.0f,  12.0f,  17.0f, 24.0f }, // sus4
    { -12.0f, -5.0f,   0.0f,  0.01f,  3.0f,  12.0f,  19.0f, 24.0f }, // m
    { -12.0f, -5.01f, -5.0f,  0.0f,   3.0f,  10.0f,  19.0f, 24.0f }, // m7
    { -12.0f, -5.0f,   0.0f,  3.0f,  10.0f,  14.0f,  19.0f, 24.0f }, // m9
    { -12.0f, -5.01f, -5.0f,  0.0f,   3.0f,  10.0f,  17.0f, 24.0f }, // m11
    { -12.0f, -5.0f,   0.0f,  2.0f,   9.0f,  16.0f,  19.0f, 24.0f }, // 69
    { -12.0f, -5.0f,   0.0f,  4.0f,  11.0f,  14.0f,  19.0f, 24.0f }, // M9
    { -12.0f, -5.0f,   0.0f,  4.0f,   7.0f,  11.0f,  19.0f, 24.0f }, // M7
    { -12.0f, -5.0f,   0.0f,  4.0f,   7.0f,  12.0f,  19.0f, 24.0f }, // M
  },
  {
    { -12.0f, -0.01f,  0.0f,  0.01f, 12.0f,  12.01f }, // OCT
    { -12.0f, -5.01f, -5.0f,  0.0f,   7.0f,  12.0f  }, // 5
    { -12.0f, -5.0f,   0.0f,  5.0f,   7.0f,  12.0f  }, // sus4
    { -12.0f, -5.0f,   0.0f,  0.01f,  3.0f,  12.0f  }, // m
    { -12.0f, -5.01f, -5.0f,  0.0f,   3.0f,  10.0f  }, // m7
    { -12.0f, -5.0f,   0.0f,  3.0f,  10.0f,  14.0f  }, // m9
    { -12.0f, -5.0f,   0.0f,  3.0f,  10.0f,  17.0f  }, // m11
    { -12.0f, -5.0f,   0.0f,  2.0f,   9.0f,  16.0f  }, // 69
    { -12.0f, -5.0f,   0.0f,  4.0f,  11.0f,  14.0f  }, // M9
    { -12.0f, -5.0f,   0.0f,  4.0f,   7.0f,  11.0f  }, // M7
    { -12.0f, -5.0f,   0.0f,  4.0f,   7.0f,  12.0f  }, // M
  },
  {
    { -12.0f, 0.0f,  0.01f, 12.0f }, // OCT
    { -12.0f, 6.99f, 7.0f,  12.0f }, // 5
    { -12.0f, 5.0f,  7.0f,  12.0f }, // sus4
    { -12.0f, 3.0f, 11.99f, 12.0f }, // m
    { -12.0f, 3.0f,  9.99f, 10.0f }, // m7
    { -12.0f, 3.0f, 10.0f,  14.0f }, // m9
    { -12.0f, 3.0f, 10.0f,  17.0f }, // m11
    { -12.0f, 2.0f,  9.0f,  16.0f }, // 69
    { -12.0f, 4.0f, 11.0f,  14.0f }, // M9
    { -12.0f, 4.0f,  7.0f,  11.0f }, // M7
    { -12.0f, 4.0f,  7.0f,  12.0f }, // M
  },
  {
    { 0.0f,  0.01f, 12.0f }, // OCT
    { 0.0f,  7.0f,  12.0f }, // 5
    { 5.0f,  7.0f,  12.0f }, // sus4
    { 0.0f,  3.0f,  12.0f }, // m
    { 0.0f,  3.0f,  10.0f }, // m7
    { 3.0f, 10.0f,  14.0f }, // m9
    { 3.0f, 10.0f,  17.0f }, // m11
    { 2.0f,  9.0f,  16.0f }, // 69
    { 4.0f, 11.0f,  14.0f }, // M9
    { 4.0f,  7.0f,  11.0f }, // M7
    { 4.0f,  7.0f,  12.0f }, // M
  }
};

#else

// Original chord table:
// - wider, occupies more room in the spectrum.
// - minimum number of note changes between adjacent chords.
// - consistant with the chord table used for the sympathetic strings model.
const float chords[kMaxStringSynthPolyphony][kNumChords][kMaxChordSize] = {
  {
    { -24.0f, -12.0f, 0.0f, 0.01f, 0.02f, 11.99f, 12.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 3.0f,  7.0f,  10.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 3.0f,  7.0f,  12.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 3.0f,  7.0f,  14.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 3.0f,  7.0f,  17.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 6.99f, 7.0f,  18.99f, 19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 4.0f,  7.0f,  17.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 4.0f,  7.0f,  14.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 4.0f,  7.0f,  12.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 4.0f,  7.0f,  11.0f,  19.0f, 24.0f },
    { -24.0f, -12.0f, 0.0f, 5.0f,  7.0f,  12.0f,  17.0f, 24.0f },
  },
  {
    { -24.0f, -12.0f, 0.0f, 0.01f, 12.0f, 12.01f },
    { -24.0f, -12.0f, 0.0f, 3.00f, 7.0f,  10.0f },
    { -24.0f, -12.0f, 0.0f, 3.00f, 7.0f,  12.0f },
    { -24.0f, -12.0f, 0.0f, 3.00f, 7.0f,  14.0f },
    { -24.0f, -12.0f, 0.0f, 3.00f, 7.0f,  17.0f },
    { -24.0f, -12.0f, 0.0f, 6.99f, 12.0f, 19.0f },
    { -24.0f, -12.0f, 0.0f, 4.00f, 7.0f,  17.0f },
    { -24.0f, -12.0f, 0.0f, 4.00f, 7.0f,  14.0f },
    { -24.0f, -12.0f, 0.0f, 4.00f, 7.0f,  12.0f },
    { -24.0f, -12.0f, 0.0f, 4.00f, 7.0f,  11.0f },
    { -24.0f, -12.0f, 0.0f, 5.00f, 7.0f, 12.0f },
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
    { -12.0f, 5.0f, 7.0f, 12.0f },
  },
  {
    { 0.0f, 0.01f, 12.0f },
    { 0.0f, 3.0f,  10.0f },
    { 0.0f, 3.0f,  7.0f },
    { 0.0f, 3.0f,  14.0f },
    { 0.0f, 3.0f,  17.0f },
    { 0.0f, 7.0f,  19.0f },
    { 0.0f, 4.0f,  17.0f },
    { 0.0f, 4.0f,  14.0f },
    { 0.0f, 4.0f,  7.0f },
    { 0.0f, 4.0f,  11.0f },
    { 0.0f, 5.0f,  7.0f },
  }
};

#endif  // BRYAN_CHORDS

// Process envelopes: Generate amplitude envelopes for all voice groups
void StringSynthPart::ProcessEnvelopes(
    float shape,          // Envelope shape parameter (0.0-1.0: decay, 0.5-1.0: attack+decay)
    uint8_t* flags,       // Envelope flags per group (rising/falling edge, gate)
    float* values) {      // Output: Envelope values per group
  float decay = shape;    // Decay always follows shape parameter
  float attack = 0.0f;    // Attack: Only when shape > 0.5
  if (shape < 0.5f) {
    attack = 0.0f;        // No attack when shape < 0.5
  } else {
    attack = (shape - 0.5f) * 2.0f;  // Attack: 0.0-1.0 when shape 0.5-1.0
  }
  
  // Convert shape values to time constants: Exponential mapping
  float period = kSampleRate / kMaxBlockSize;  // Update period (2000 Hz)
  // Attack time: Exponential mapping (0.005s base, scaled by attack)
  float attack_time = SemitonesToRatio(attack * 96.0f) * 0.005f * period;
  // Decay time: Exponential mapping (0.180s base, scaled by decay)
  float decay_time = SemitonesToRatio(decay * 84.0f) * 0.180f * period;
  // Convert times to rates: Rate = 1 / time
  float attack_rate = 1.0f / attack_time;
  float decay_rate = 1.0f / decay_time;
  
  // Process envelopes for each group
  for (int32_t i = 0; i < polyphony_; ++i) {
    // Drone mode: When shape >= 0.98, add constant level (sustain)
    float drone = shape < 0.98f ? 0.0f : (shape - 0.98f) * 55.0f;  // 0.0-1.0 range
    if (drone >= 1.0f) drone = 1.0f;  // Clamp to 1.0

    // Configure and process envelope: AD envelope with attack/decay rates
    group_[i].envelope.set_ad(attack_rate, decay_rate);
    float value = group_[i].envelope.Process(flags[i]);  // Process envelope
    // Mix envelope with drone: value + (1 - value) * drone
    values[i] = value + (1.0f - value) * drone;  // Drone adds constant level
  }
}

// Formant table: 5 vowel sounds with 3 formant frequencies each (in Hz)
const int32_t kFormantTableSize = 5;
const float formants[kFormantTableSize][kNumFormants] = {
  { 700, 1100, 2400 },   // Vowel 0: "A" (as in "father")
  { 500, 1300, 1700 },   // Vowel 1: "E" (as in "bed")
  { 400, 2000, 2500 },   // Vowel 2: "I" (as in "beet")
  { 600, 800, 2400 },    // Vowel 3: "O" (as in "boat")
  { 300, 900, 2200 },    // Vowel 4: "U" (as in "boot")
};

// Process formant filter: Apply formant filtering for vowel sounds
void StringSynthPart::ProcessFormantFilter(
    float vowel,          // Vowel selection (0.0-1.0, maps to formant table)
    float shift,          // Frequency shift (1.0 = normal, >1.0 = higher)
    float resonance,      // Filter resonance (Q factor)
    float* out,           // Output buffer (left channel)
    float* aux,           // Output buffer (right channel)
    size_t size) {        // Block size
  // Sum left and right channels: Formant filtering is mono
  for (size_t i = 0; i < size; ++i) {
    filter_in_buffer_[i] = out[i] + aux[i];  // Sum channels
  }
  // Clear output buffers: Will be filled by formant filters
  fill(&out[0], &out[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);

  // Map vowel parameter to formant table index: 0.0-1.0 -> 0-4
  vowel *= (kFormantTableSize - 1.001f);
  MAKE_INTEGRAL_FRACTIONAL(vowel);  // Split into integral and fractional parts
  
  // Process each formant filter: 3 formants per vowel
  for (int32_t i = 0; i < kNumFormants; ++i) {
    // Interpolate formant frequency: Between adjacent vowels
    float a = formants[vowel_integral][i];      // Lower vowel formant
    float b = formants[vowel_integral + 1][i];  // Upper vowel formant
    float f = a + (b - a) * vowel_fractional;   // Linear interpolation
    f *= shift;  // Apply frequency shift
    // Set filter frequency and Q: Convert Hz to normalized frequency
    formant_filter_[i].set_f_q<FREQUENCY_DIRTY>(f / kSampleRate, resonance);
    // Process through bandpass filter: Extract formant
    formant_filter_[i].Process<FILTER_MODE_BAND_PASS>(
        filter_in_buffer_,   // Input: summed channels
        filter_out_buffer_,  // Output: filtered signal
        size);
    // Pan formants: Distribute across stereo field
    const float pan = i * 0.3f + 0.2f;  // Pan: 0.2, 0.5, 0.8 (left to right)
    for (size_t j = 0; j < size; ++j) {
      // Mix formant to outputs: Panning with 0.5 gain
      out[j] += filter_out_buffer_[j] * pan * 0.5f;           // Left channel
      aux[j] += filter_out_buffer_[j] * (1.0f - pan) * 0.5f;  // Right channel
    }
  }
}

struct ChordNote {
  float note;
  float amplitude;
};

// Process audio: Main string synth processing function
void StringSynthPart::Process(
    const PerformanceState& performance_state,  // Performance state
    const Patch& patch,                          // Patch parameters
    const float* in,                             // Input audio (unused, kept for compatibility)
    float* out,                                  // Output buffer
    float* aux,                                   // Auxiliary output buffer
    size_t size) {                               // Block size
  // Voice allocation: Assign note to active voice group
  uint8_t envelope_flags[kMaxStringSynthPolyphony];
  
  // Initialize envelope flags: All groups start with no flags
  fill(&envelope_flags[0], &envelope_flags[polyphony_], 0);
  // Process note filter: Smooth note transitions
  note_filter_.Process(performance_state.note, performance_state.strum);
  
  // Strum trigger: Allocate new note to voice group
  if (performance_state.strum) {
    group_[active_group_].tonic = note_filter_.stable_note();  // Assign stable note
    envelope_flags[active_group_] = ENVELOPE_FLAG_FALLING_EDGE; // Trigger release on old group
    active_group_ = (active_group_ + 1) % polyphony_;          // Move to next group
    envelope_flags[active_group_] = ENVELOPE_FLAG_RISING_EDGE;  // Trigger attack on new group
    acquisition_delay_ = 3;  // 3-sample delay before updating parameters
  }
  // Acquisition delay: Wait before updating parameters (prevents glitches)
  if (acquisition_delay_) {
    --acquisition_delay_;
  } else {
    // Update active group parameters: Note, chord, structure
    group_[active_group_].tonic = note_filter_.note();              // Follow note filter
    group_[active_group_].chord = performance_state.chord;           // Set chord index
    group_[active_group_].structure = patch.structure;               // Set structure (registration)
    envelope_flags[active_group_] |= ENVELOPE_FLAG_GATE;            // Hold gate (sustain)
  }

  // Process envelopes: Generate amplitude envelopes for all groups
  float envelope_values[kMaxStringSynthPolyphony];
  ProcessEnvelopes(patch.damping, envelope_flags, envelope_values);
  
  // Copy input to outputs: Input is passed through (though typically unused)
  copy(&in[0], &in[size], &aux[0]);
  copy(&in[0], &in[size], &out[0]);
  
  // Calculate chord size: Number of voices per group
  int32_t chord_size = min(kStringSynthVoices / polyphony_, kMaxChordSize);
  
  // Process each voice group
  for (int32_t group = 0; group < polyphony_; ++group) {
    ChordNote notes[kMaxChordSize];  // Chord notes for this group
    float harmonics[kNumHarmonics * 2];  // Harmonic amplitudes (6 values)
    
    // Compute registration: Calculate harmonic amplitudes based on brightness
    ComputeRegistration(
        envelope_values[group] * 0.25f,  // Gain: envelope * 0.25
        patch.brightness,                 // Registration: brightness parameter
        harmonics);                        // Output: harmonic amplitudes
    
    // Get chord notes: Look up chord intervals from chord table
    for (int32_t i = 0; i < chord_size; ++i) {
      float n = chords[polyphony_ - 1][group_[group].chord][i];  // Get chord interval
      notes[i].note = n;  // Store interval
      // Amplitude: Full amplitude for notes in range, reduced for extreme notes
      notes[i].amplitude = n >= 0.0f && n <= 17.0f ? 1.0f : 0.7f;
    }

    // Render each chord note: Generate audio from multiple voices
    for (int32_t chord_note = 0; chord_note < chord_size; ++chord_note) {
      // Calculate total note: Sum all note components
      float note = 0.0f;
      note += group_[group].tonic;              // Group tonic
      note += performance_state.tonic;           // Performance tonic (transpose)
      note += performance_state.fm;              // Frequency modulation
      note += notes[chord_note].note;            // Chord interval
      
      // Calculate amplitudes: Multiply chord note amplitude by harmonic amplitudes
      float amplitudes[kNumHarmonics * 2];
      for (int32_t i = 0; i < kNumHarmonics * 2; ++i) {
        amplitudes[i] = notes[chord_note].amplitude * harmonics[i];
      }
      
      // Fold truncated harmonics: When polyphony >= 2 and chord_note < 2,
      // reduce harmonics to prevent aliasing (fold highest harmonic into previous)
      size_t num_harmonics = polyphony_ >= 2 && chord_note < 2
          ? kNumHarmonics - 1  // Use 2 harmonics instead of 3
          : kNumHarmonics;     // Use all 3 harmonics
      // Fold highest harmonic into previous harmonic
      for (int32_t i = num_harmonics; i < kNumHarmonics; ++i) {
        amplitudes[2 * (num_harmonics - 1)] += amplitudes[2 * i];         // Odd harmonic
        amplitudes[2 * (num_harmonics - 1) + 1] += amplitudes[2 * i + 1]; // Even harmonic
      }

      // Convert note to frequency: MIDI note to normalized frequency
      float frequency = SemitonesToRatio(note - 69.0f) * a3;
      // Render voice: Generate audio with multiple harmonics
      voice_[group * chord_size + chord_note].Render(
          frequency,
          amplitudes,
          num_harmonics,
          (group + chord_note) & 1 ? out : aux,  // Alternate outputs: odd/even routing
          size);
    }
  }
  
  // Clear FX: Reset reverb when switching FX types
  if (clear_fx_) {
    reverb_.Clear();  // Clear reverb delay lines
    clear_fx_ = false;
  }
  
  // Apply FX: Process through selected effect
  switch (fx_type_) {
    case FX_FORMANT:
    case FX_FORMANT_2:
      // Formant filtering: Vowel sounds
      ProcessFormantFilter(
          patch.position,                                    // Vowel selection
          fx_type_ == FX_FORMANT ? 1.0f : 1.1f,             // Frequency shift
          fx_type_ == FX_FORMANT ? 25.0f : 10.0f,           // Resonance (Q)
          out,
          aux,
          size);
      break;

    case FX_CHORUS:
      // Chorus effect: Dual LFO modulation
      chorus_.set_amount(patch.position);                   // Amount: position parameter
      chorus_.set_depth(0.15f + 0.5f * patch.position);     // Depth: 0.15-0.65
      chorus_.Process(out, aux, size);
      break;
    
    case FX_ENSEMBLE:
      // Ensemble effect: Multi-voice detuning
      ensemble_.set_amount(patch.position * (2.0f - patch.position));  // Amount: bell curve
      ensemble_.set_depth(0.2f + 0.8f * patch.position * patch.position);  // Depth: quadratic
      ensemble_.Process(out, aux, size);
      break;
  
    case FX_REVERB:
    case FX_REVERB_2:
      // Reverb effect: Griesinger topology reverb
      reverb_.set_amount(patch.position * 0.5f);            // Amount: 0.0-0.5
      reverb_.set_diffusion(0.625f);                        // Diffusion: fixed
      reverb_.set_time(fx_type_ == FX_REVERB
        ? (0.5f + 0.49f * patch.position)                   // Time: 0.5-0.99 (FX_REVERB)
        : (0.3f + 0.6f * patch.position));                  // Time: 0.3-0.9 (FX_REVERB_2)
      reverb_.set_input_gain(0.2f);                         // Input gain: fixed
      reverb_.set_lp(fx_type_ == FX_REVERB ? 0.3f : 0.6f);  // LP cutoff: 0.3 or 0.6
      reverb_.Process(out, aux, size);
      break;
    
    default:
      break;
  }

  // Invert aux channel: Prevents signal cancellation when outputs are normalized
  // (EVEN and ODD outputs are summed through normalization in hardware)
  for (size_t i = 0; i < size; ++i) {
    aux[i] = -aux[i];  // Invert aux channel
  }
  // Apply limiter: Prevent clipping (gain = 1.0)
  limiter_.Process(out, aux, size, 1.0f);
}

}  // namespace rings