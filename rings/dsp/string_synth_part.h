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
// Part for the string synth easter egg.

#ifndef RINGS_DSP_STRING_SYNTH_PART_H_
#define RINGS_DSP_STRING_SYNTH_PART_H_

#include "stmlib/stmlib.h"

#include "stmlib/dsp/filter.h"

#include "rings/dsp/dsp.h"
#include "rings/dsp/fx/chorus.h"
#include "rings/dsp/fx/ensemble.h"
#include "rings/dsp/fx/reverb.h"
#include "rings/dsp/limiter.h"
#include "rings/dsp/note_filter.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "rings/dsp/string_synth_envelope.h"
#include "rings/dsp/string_synth_voice.h"

namespace rings {

// String synth constants: Easter egg "Disastrous Peace" mode parameters
const int32_t kMaxStringSynthPolyphony = 4;  // Maximum polyphony (1, 2, or 4 groups)
const int32_t kStringSynthVoices = 12;       // Total number of voices (12 voices)
const int32_t kMaxChordSize = 8;             // Maximum chord size (8 notes)
const int32_t kNumHarmonics = 3;             // Number of harmonics per voice
const int32_t kNumFormants = 3;             // Number of formant filters

// FX types: Effects available in string synth mode
enum FxType {
  FX_FORMANT,        // Formant filtering (vowel sounds)
  FX_CHORUS,         // Chorus effect
  FX_REVERB,         // Reverb effect
  FX_FORMANT_2,      // Alternative formant filtering
  FX_ENSEMBLE,       // Ensemble effect (multiple detuned voices)
  FX_REVERB_2,       // Alternative reverb
  FX_LAST
};

// VoiceGroup: Group of voices with shared parameters
struct VoiceGroup {
  float tonic;                    // Base frequency (MIDI note)
  StringSynthEnvelope envelope;  // Amplitude envelope
  int32_t chord;                  // Chord index (0-10)
  float structure;                // Structure parameter (registration)
};

// StringSynthPart: Easter egg "Disastrous Peace" string synthesizer
// 12-voice polyphonic string synthesizer with formant filtering, chorus, ensemble, and reverb
class StringSynthPart {
 public:
  StringSynthPart() { }
  ~StringSynthPart() { }
  
  // Initialize string synth: Set up voices, groups, and effects
  void Init(uint16_t* reverb_buffer);
  
  // Process audio: Main audio processing function
  void Process(
      const PerformanceState& performance_state,  // Performance state
      const Patch& patch,                          // Patch parameters
      const float* in,                             // Input audio (unused)
      float* out,                                  // Output buffer
      float* aux,                                  // Auxiliary output buffer
      size_t size);                                // Block size

  // Set polyphony: Configure number of voice groups (1, 2, or 4)
  inline void set_polyphony(int32_t polyphony) {
    int32_t old_polyphony = polyphony_;
    polyphony_ = std::min(polyphony, kMaxStringSynthPolyphony);  // Clamp to maximum
    // Initialize new groups with slight detuning
    for (int32_t i = old_polyphony; i < polyphony_; ++i) {
      group_[i].tonic = group_[0].tonic + i * 0.01f;  // 0.01 semitone detuning per group
    }
    // Ensure active group is within valid range
    if (active_group_ >= polyphony_) {
      active_group_ = 0;
    }
  }
  
  // Set FX type: Change effect type (clears FX state when switching categories)
  inline void set_fx(FxType fx_type) {
    // Clear FX when switching between categories (formant/chorus/reverb)
    if ((fx_type % 3) != (fx_type_ % 3)) {
      clear_fx_ = true;  // Mark for FX clearing
    }
    fx_type_ = fx_type;
  }
  
 private:
  // Process envelopes: Generate amplitude envelopes for voices
  void ProcessEnvelopes(float shape, uint8_t* flags, float* values);
  // Compute registration: Calculate harmonic amplitudes based on registration parameter
  void ComputeRegistration(float gain, float registration, float* amplitudes);

  // Process formant filter: Apply formant filtering (vowel sounds)
  void ProcessFormantFilter(float vowel, float shift, float resonance,
                            float* out, float* aux, size_t size);
  
  // Voices: 12 string synth voices (each with 3 harmonics)
  StringSynthVoice<kNumHarmonics> voice_[kStringSynthVoices];
  // Voice groups: 4 groups for polyphony (each group has multiple voices)
  VoiceGroup group_[kMaxStringSynthPolyphony];
  
  // Effects: Formant filters, ensemble, reverb, chorus, limiter
  stmlib::Svf formant_filter_[kNumFormants];  // Formant filters (3 filters for vowel sounds)
  Ensemble ensemble_;                          // Ensemble effect (multiple detuned voices)
  Reverb reverb_;                             // Reverb effect
  Chorus chorus_;                             // Chorus effect
  Limiter limiter_;                           // Output limiter

  int32_t num_voices_;        // Number of active voices per group
  int32_t active_group_;      // Currently active voice group
  uint32_t step_counter_;    // Step counter for voice allocation
  int32_t polyphony_;        // Current polyphony setting (1, 2, or 4)
  int32_t acquisition_delay_; // Acquisition delay (for note filtering)
  
  FxType fx_type_;           // Current FX type
  
  NoteFilter note_filter_;   // Note filter for smooth note transitions
  
  // Processing buffers: Input and output buffers for FX processing
  float filter_in_buffer_[kMaxBlockSize];   // Input buffer for FX
  float filter_out_buffer_[kMaxBlockSize];  // Output buffer for FX
  
  bool clear_fx_;            // Flag to clear FX state (when switching FX types)
  
  DISALLOW_COPY_AND_ASSIGN(StringSynthPart);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_SYNTH_VOICE_H_
