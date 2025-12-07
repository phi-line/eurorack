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
// Voice for the string synth easter egg.

#ifndef RINGS_DSP_STRING_SYNTH_VOICE_H_
#define RINGS_DSP_STRING_SYNTH_VOICE_H_

#include "stmlib/stmlib.h"

#include "rings/dsp/string_synth_oscillator.h"

namespace rings {

// StringSynthVoice: Template-based string synth voice with multiple harmonics
// Each voice consists of multiple oscillators (harmonics) that are summed together
template<size_t num_harmonics>
class StringSynthVoice {
 public:
  StringSynthVoice() { }
  ~StringSynthVoice() { }
  
  // Initialize voice: Set up all harmonic oscillators
  void Init() {
    for (size_t i = 0; i < num_harmonics; ++i) {
      oscillator_[i].Init();  // Initialize each harmonic oscillator
    }
  }
  
  // Render voice: Generate audio from multiple harmonics
  void Render(
      float frequency,              // Fundamental frequency (normalized)
      const float* amplitudes,      // Harmonic amplitudes (2 per harmonic: odd/even)
      size_t summed_harmonics,      // Number of harmonics to sum
      float* out,                   // Output buffer
      size_t size) {                // Block size
    // Fundamental: Dark square wave (additive synthesis)
    oscillator_[0].template Render<OSCILLATOR_SHAPE_DARK_SQUARE, true>(
        frequency, amplitudes[0], amplitudes[1], out, size);
    amplitudes += 2;  // Move to next harmonic amplitudes
    
    // Harmonics: Bright square waves at multiples of fundamental
    for (size_t i = 1; i < summed_harmonics; ++i) {
      frequency *= 2.0f;  // Double frequency for each harmonic (2x, 4x, 8x, ...)
      // Bright square wave (additive synthesis, accumulates to output)
      oscillator_[i].template Render<OSCILLATOR_SHAPE_BRIGHT_SQUARE, false>(
          frequency, amplitudes[0], amplitudes[1], out, size);
      amplitudes += 2;  // Move to next harmonic amplitudes
    }
  }

 private:
  StringSynthOscillator oscillator_[num_harmonics];  // Harmonic oscillators (typically 3)
  DISALLOW_COPY_AND_ASSIGN(StringSynthVoice);
};

}  // namespace rings

#endif  // RINGS_DSP_STRING_SYNTH_VOICE_H_
