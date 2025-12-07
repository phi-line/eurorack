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

#ifndef RINGS_DSP_PART_H_
#define RINGS_DSP_PART_H_

#include <algorithm>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/cosine_oscillator.h"
#include "stmlib/dsp/delay_line.h"

#include "rings/dsp/dsp.h"
#include "rings/dsp/fm_voice.h"
#include "rings/dsp/fx/reverb.h"
#include "rings/dsp/limiter.h"
#include "rings/dsp/note_filter.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"
#include "rings/dsp/plucker.h"
#include "rings/dsp/resonator.h"
#include "rings/dsp/string.h"

namespace rings {

// Resonator models: Three standard models plus three bonus models
enum ResonatorModel {
  RESONATOR_MODEL_MODAL,                        // Modal resonator (as used in Elements)
  RESONATOR_MODEL_SYMPATHETIC_STRING,           // Sympathetic strings (network of comb filters)
  RESONATOR_MODEL_STRING,                       // String with non-linearity/dispersion
  
  // Bonus models (hidden in original firmware)
  RESONATOR_MODEL_FM_VOICE,                     // FM synthesis voice
  RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED,  // Quantized sympathetic strings (chord mode)
  RESONATOR_MODEL_STRING_AND_REVERB,            // String model with integrated reverb
  RESONATOR_MODEL_LAST
};

const int32_t kMaxPolyphony = 4;        // Maximum polyphony voices (1, 2, or 4)
const int32_t kNumStrings = kMaxPolyphony * 2;  // Maximum strings (8 total: 2 per voice)

// Part: Main polyphonic voice manager and audio processing coordinator
// Manages multiple voices, voice allocation, and routes audio through appropriate resonator models
class Part {
 public:
  Part() { }
  ~Part() { }
  
  // Initialize Part with shared reverb buffer
  void Init(uint16_t* reverb_buffer);
  
  // Main audio processing function: Processes audio through resonator models
  void Process(
      const PerformanceState& performance_state,  // Performance state (notes, strum, etc.)
      const Patch& patch,                          // Patch parameters (structure, brightness, etc.)
      const float* in,                             // Input audio buffer
      float* out,                                  // Output buffer (odd partials/voices)
      float* aux,                                  // Auxiliary output buffer (even partials/voices)
      size_t size);                                // Block size (typically 24 samples)

  inline bool bypass() const { return bypass_; }
  inline void set_bypass(bool bypass) { bypass_ = bypass; }  // Bypass mode: pass input to output

  inline int32_t polyphony() const { return polyphony_; }
  // Set polyphony: Allocates new voices and initializes note values
  inline void set_polyphony(int32_t polyphony) {
    int32_t old_polyphony = polyphony_;
    polyphony_ = std::min(polyphony, kMaxPolyphony);  // Clamp to maximum
    // Initialize new voices with slight detuning (0.05 semitones per voice)
    for (int32_t i = old_polyphony; i < polyphony_; ++i) {
      note_[i] = note_[0] + i * 0.05f;
    }
    dirty_ = true;  // Mark for reconfiguration
  }
  
  inline ResonatorModel model() const { return model_; }
  // Set resonator model: Marks for reconfiguration if changed
  inline void set_model(ResonatorModel model) {
    if (model != model_) {
      model_ = model;
      dirty_ = true;  // Mark for reconfiguration
    }
  }

 private:
  // Configure resonators: Reinitializes resonator objects when model or polyphony changes
  void ConfigureResonators();
  
  // Render modal voice: Processes audio through modal resonator (bank of modal filters)
  void RenderModalVoice(
      int32_t voice,                              // Voice index
      const PerformanceState& performance_state,  // Performance state
      const Patch& patch,                          // Patch parameters
      float frequency,                             // Voice frequency (normalized)
      float filter_cutoff,                        // Excitation filter cutoff
      size_t size);                               // Block size
  
  // Render string voice: Processes audio through string model (with sympathetic resonance)
  void RenderStringVoice(
      int32_t voice,                              // Voice index
      const PerformanceState& performance_state,  // Performance state
      const Patch& patch,                          // Patch parameters
      float frequency,                             // Voice frequency (normalized)
      float filter_cutoff,                        // Excitation filter cutoff
      size_t size);                               // Block size
  
  // Render FM voice: Processes audio through FM synthesis voice
  void RenderFMVoice(
      int32_t voice,                              // Voice index
      const PerformanceState& performance_state,  // Performance state
      const Patch& patch,                          // Patch parameters
      float frequency,                             // Voice frequency (normalized)
      float filter_cutoff,                        // Excitation filter cutoff
      size_t size);                               // Block size
  
  // Squash function: Non-linear mapping for structure interpolation
  // Creates smooth interpolation with emphasis at extremes (x^32 curve)
  inline float Squash(float x) const {
    if (x < 0.5f) {
      x *= 2.0f;      // Scale to 0-1
      x *= x; x *= x; x *= x; x *= x; x *= x;  // x^32
      x *= 0.5f;      // Scale back
    } else {
      x = 2.0f - 2.0f * x;  // Invert
      x *= x; x *= x; x *= x; x *= x; x *= x;  // (1-x)^32
      x = 1.0f - 0.5f * x;   // Invert and scale
    }
    return x;
  }

  // Compute sympathetic strings notes: Calculates frequencies for sympathetic string network
  // Based on tonic, note, and structure parameter (or chord index for quantized mode)
  void ComputeSympatheticStringsNotes(
      float tonic,           // Base frequency (MIDI note)
      float note,            // Main note (MIDI note)
      float parameter,       // Structure parameter or chord index (if >= 2.0)
      float* destination,    // Output array for string frequencies
      size_t num_strings);   // Number of strings to compute
  
  bool bypass_;              // Bypass mode flag
  bool dirty_;               // Flag indicating resonators need reconfiguration

  ResonatorModel model_;     // Current resonator model

  int32_t num_voices_;       // Number of voices (unused, kept for compatibility)
  int32_t active_voice_;     // Currently active voice (receives input)
  uint32_t step_counter_;    // Step counter for ping-pong voice allocation pattern
  int32_t polyphony_;        // Current polyphony setting (1, 2, or 4)
  
  // Resonator objects: One per voice/model type
  Resonator resonator_[kMaxPolyphony];         // Modal resonators (for modal model)
  String string_[kNumStrings];                 // String objects (for string models, 2 per voice)
  stmlib::CosineOscillator lfo_[kNumStrings];  // LFOs for sympathetic string modulation
  FMVoice fm_voice_[kMaxPolyphony];            // FM voices (for FM model)
  
  // Per-voice processing objects
  stmlib::Svf excitation_filter_[kMaxPolyphony];  // Low-pass filters for input excitation
  stmlib::DCBlocker dc_blocker_[kMaxPolyphony];   // DC blockers for string models
  Plucker plucker_[kMaxPolyphony];                // Pluckers for internal noise burst exciter

  float note_[kMaxPolyphony];  // MIDI note value per voice
  NoteFilter note_filter_;     // Note filter for smooth note transitions

  // Processing buffers (24 samples max)
  float resonator_input_[kMaxBlockSize];              // Input buffer for resonator
  float sympathetic_resonator_input_[kMaxBlockSize];  // Input buffer for sympathetic strings
  float noise_burst_buffer_[kMaxBlockSize];           // Buffer for internal noise burst
  
  float out_buffer_[kMaxBlockSize];  // Output buffer (odd partials/voices)
  float aux_buffer_[kMaxBlockSize];  // Auxiliary buffer (even partials/voices)
  
  Reverb reverb_;    // Reverb effect (for string+reverb model)
  Limiter limiter_;  // Limiter for preventing clipping
  
  static float model_gains_[RESONATOR_MODEL_LAST];  // Model-specific output gain compensation
  
  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace rings

#endif  // RINGS_DSP_PART_H_
