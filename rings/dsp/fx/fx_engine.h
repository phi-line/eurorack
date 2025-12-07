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
// Base class for building reverb.

#ifndef RINGS_DSP_FX_FX_ENGINE_H_
#define RINGS_DSP_FX_FX_ENGINE_H_

#include <algorithm>

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"

namespace rings {

#define TAIL , -1  // Macro for delay line tail access (offset = -1)

// Format: Data format for delay line storage (affects memory usage and quality)
enum Format {
  FORMAT_12_BIT,  // 12-bit fixed point (4KB per sample, lower quality)
  FORMAT_16_BIT,  // 16-bit fixed point (2 bytes per sample, medium quality)
  FORMAT_32_BIT   // 32-bit float (4 bytes per sample, full quality)
};

// LFO index: Two LFOs available for modulation
enum LFOIndex {
  LFO_1,  // Low-frequency oscillator 1
  LFO_2   // Low-frequency oscillator 2
};

// DataType: Template specialization for data compression/decompression
// Converts between float samples and fixed-point storage formats
template<Format format>
struct DataType { };

// 12-bit format: 4KB range (-2048 to 2047)
template<>
struct DataType<FORMAT_12_BIT> {
  typedef uint16_t T;  // Storage type: 16-bit unsigned (12 bits used)
  
  // Decompress: Convert from fixed-point to float
  static inline float Decompress(T value) {
    return static_cast<float>(static_cast<int16_t>(value)) / 4096.0f;  // Scale by 2^12
  }
  
  // Compress: Convert from float to fixed-point
  static inline T Compress(float value) {
    return static_cast<uint16_t>(
        stmlib::Clip16(static_cast<int32_t>(value * 4096.0f)));  // Scale and clip
  }
};

// 16-bit format: 32KB range (-32768 to 32767)
template<>
struct DataType<FORMAT_16_BIT> {
  typedef uint16_t T;  // Storage type: 16-bit unsigned
  
  // Decompress: Convert from fixed-point to float
  static inline float Decompress(T value) {
    return static_cast<float>(static_cast<int16_t>(value)) / 32768.0f;  // Scale by 2^15
  }
  
  // Compress: Convert from float to fixed-point
  static inline T Compress(float value) {
    return static_cast<uint16_t>(
        stmlib::Clip16(static_cast<int32_t>(value * 32768.0f)));  // Scale and clip
  }
};

// 32-bit format: Full precision float (no compression)
template<>
struct DataType<FORMAT_32_BIT> {
  typedef float T;  // Storage type: float (no conversion needed)
  
  // Decompress: No conversion (passthrough)
  static inline float Decompress(T value) {
    return value;  // Passthrough
  }
  
  // Compress: No conversion (passthrough)
  static inline T Compress(float value) {
    return value;  // Passthrough
  }
};

// FxEngine: Base class for building delay-based effects (reverb, chorus, ensemble)
// Template-based system for efficient delay line memory allocation
template<
    size_t size,                    // Total buffer size (in samples)
    Format format = FORMAT_12_BIT>  // Data format (default: 12-bit)
class FxEngine {
 public:
  typedef typename DataType<format>::T T;  // Storage type (uint16_t or float)
  FxEngine() { }
  ~FxEngine() { }

  // Initialize FX engine: Set up buffer and clear delay lines
  void Init(T* buffer) {
    buffer_ = buffer;  // Store buffer pointer
    Clear();           // Clear all delay lines
  }
  
  // Clear: Reset all delay lines to zero
  void Clear() {
    std::fill(&buffer_[0], &buffer_[size], 0);  // Fill buffer with zeros
    write_ptr_ = 0;                              // Reset write pointer
  }

  struct Empty { };  // Empty type for template recursion termination
  
  // Reserve: Template for reserving delay line memory
  // Recursive structure: Each Reserve contains length and tail (next Reserve)
  template<int32_t l, typename T = Empty>
  struct Reserve {
    typedef T Tail;  // Tail: Next Reserve in chain (or Empty)
    enum {
      length = l  // Length: Delay line length in samples
    };
  };
  
  // DelayLine: Computes delay line base address and length from Memory structure
  // Recursively calculates base address by summing previous delay line lengths
  template<typename Memory, int32_t index>
  struct DelayLine {
    enum {
      // Length: Get length from tail (recursive)
      length = DelayLine<typename Memory::Tail, index - 1>::length,
      // Base: Previous base + previous length + 1 (gap for safety)
      base = DelayLine<Memory, index - 1>::base + DelayLine<Memory, index - 1>::length + 1
    };
  };

  // DelayLine base case: First delay line starts at base 0
  template<typename Memory>
  struct DelayLine<Memory, 0> {
    enum {
      length = Memory::length,  // Length from Memory structure
      base = 0                  // Base address: 0
    };
  };

  // Context: Processing context for building delay-based effects
  // Provides accumulator-based API for reading/writing delay lines
  class Context {
   friend class FxEngine;
   public:
    Context() { }
    ~Context() { }
    
    // Load: Set accumulator value (start of processing chain)
    inline void Load(float value) {
      accumulator_ = value;  // Set accumulator
    }

    // Read: Add scaled value to accumulator (mixing)
    inline void Read(float value, float scale) {
      accumulator_ += value * scale;  // Add scaled value
    }

    // Read: Add value to accumulator (mixing, scale = 1.0)
    inline void Read(float value) {
      accumulator_ += value;  // Add value
    }

    // Write: Output accumulator to variable
    inline void Write(float& value) {
      value = accumulator_;  // Copy accumulator
    }

    // Write: Output accumulator and scale it
    inline void Write(float& value, float scale) {
      value = accumulator_;      // Copy accumulator
      accumulator_ *= scale;      // Scale accumulator (for feedback)
    }
    
    // Write to delay line: Write accumulator to delay line at specified offset
    template<typename D>
    inline void Write(D& d, int32_t offset, float scale) {
      STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
      T w = DataType<format>::Compress(accumulator_);  // Compress to storage format
      if (offset == -1) {
        // TAIL: Write to end of delay line (oldest sample)
        buffer_[(write_ptr_ + D::base + D::length - 1) & MASK] = w;
      } else {
        // Normal offset: Write to specified position
        buffer_[(write_ptr_ + D::base + offset) & MASK] = w;
      }
      accumulator_ *= scale;  // Scale accumulator (for feedback)
    }
    
    // Write to delay line: Write at offset 0
    template<typename D>
    inline void Write(D& d, float scale) {
      Write(d, 0, scale);
    }

    // WriteAllPass: Write to delay line and add previous read (allpass filter)
    template<typename D>
    inline void WriteAllPass(D& d, int32_t offset, float scale) {
      Write(d, offset, scale);        // Write to delay line
      accumulator_ += previous_read_; // Add previous read (allpass feedback)
    }
    
    // WriteAllPass: Write at offset 0
    template<typename D>
    inline void WriteAllPass(D& d, float scale) {
      WriteAllPass(d, 0, scale);
    }
    
    // Read from delay line: Read from delay line at specified offset
    template<typename D>
    inline void Read(D& d, int32_t offset, float scale) {
      STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
      T r;
      if (offset == -1) {
        // TAIL: Read from end of delay line (oldest sample)
        r = buffer_[(write_ptr_ + D::base + D::length - 1) & MASK];
      } else {
        // Normal offset: Read from specified position
        r = buffer_[(write_ptr_ + D::base + offset) & MASK];
      }
      float r_f = DataType<format>::Decompress(r);  // Decompress from storage format
      previous_read_ = r_f;                         // Store for allpass feedback
      accumulator_ += r_f * scale;                   // Add scaled value to accumulator
    }
    
    // Read from delay line: Read at offset 0
    template<typename D>
    inline void Read(D& d, float scale) {
      Read(d, 0, scale);
    }
    
    // Lp: Low-pass filter (one-pole)
    inline void Lp(float& state, float coefficient) {
      state += coefficient * (accumulator_ - state);  // Update filter state
      accumulator_ = state;                           // Output filtered value
    }

    // Hp: High-pass filter (one-pole)
    inline void Hp(float& state, float coefficient) {
      state += coefficient * (accumulator_ - state);  // Update filter state
      accumulator_ -= state;                           // Output high-pass (difference)
    }
    
    // Interpolate: Read from delay line with linear interpolation (for fractional delays)
    template<typename D>
    inline void Interpolate(D& d, float offset, float scale) {
      STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
      MAKE_INTEGRAL_FRACTIONAL(offset);  // Split offset into integer and fractional parts
      // Read two adjacent samples for interpolation
      float a = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + D::base) & MASK]);      // Sample at integer offset
      float b = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + D::base + 1) & MASK]);  // Sample at integer+1
      float x = a + (b - a) * offset_fractional;  // Linear interpolation
      previous_read_ = x;                          // Store for allpass feedback
      accumulator_ += x * scale;                   // Add scaled interpolated value
    }
    
    // Interpolate with LFO: Read from delay line with LFO-modulated interpolation
    template<typename D>
    inline void Interpolate(
        D& d, float offset, LFOIndex index, float amplitude, float scale) {
      STATIC_ASSERT(D::base + D::length <= size, delay_memory_full);
      // Add LFO modulation to offset: Creates pitch modulation (chorus/shimmer)
      offset += amplitude * lfo_value_[index];  // Modulate delay time
      MAKE_INTEGRAL_FRACTIONAL(offset);         // Split into integer and fractional parts
      // Read two adjacent samples for interpolation
      float a = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + D::base) & MASK]);      // Sample at integer offset
      float b = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + D::base + 1) & MASK]);  // Sample at integer+1
      float x = a + (b - a) * offset_fractional;  // Linear interpolation
      previous_read_ = x;                          // Store for allpass feedback
      accumulator_ += x * scale;                   // Add scaled interpolated value
    }
    
   private:
    float accumulator_;      // Accumulator: Current processing value
    float previous_read_;     // Previous read: For allpass feedback
    float lfo_value_[2];     // LFO values: Current LFO outputs (updated every 32 samples)
    T* buffer_;              // Buffer pointer: Delay line storage
    int32_t write_ptr_;      // Write pointer: Current write position

    DISALLOW_COPY_AND_ASSIGN(Context);
  };
  
  // Set LFO frequency: Configure LFO for delay modulation
  inline void SetLFOFrequency(LFOIndex index, float frequency) {
    // Initialize cosine oscillator: Frequency scaled by 32 (for block-based updates)
    lfo_[index].template Init<stmlib::COSINE_OSCILLATOR_APPROXIMATE>(frequency * 32.0f);
  }
  
  // Start processing: Initialize context for new sample
  inline void Start(Context* c) {
    // Decrement write pointer: Circular buffer (wraps around)
    --write_ptr_;
    if (write_ptr_ < 0) {
      write_ptr_ += size;  // Wrap around
    }
    // Initialize context: Reset accumulator and set buffer pointers
    c->accumulator_ = 0.0f;      // Reset accumulator
    c->previous_read_ = 0.0f;    // Reset previous read
    c->buffer_ = buffer_;        // Set buffer pointer
    c->write_ptr_ = write_ptr_;  // Set write pointer
    // Update LFOs: Update every 32 samples (reduces computation)
    if ((write_ptr_ & 31) == 0) {
      // Update LFOs: Calculate new values
      c->lfo_value_[0] = lfo_[0].Next();
      c->lfo_value_[1] = lfo_[1].Next();
    } else {
      // Use cached LFO values: No update needed
      c->lfo_value_[0] = lfo_[0].value();
      c->lfo_value_[1] = lfo_[1].value();
    }
  }
  
 private:
  enum {
    MASK = size - 1  // Mask for circular buffer wrapping (requires size = 2^n)
  };
  
  int32_t write_ptr_;                    // Write pointer: Current write position (circular)
  T* buffer_;                            // Buffer: Delay line storage
  stmlib::CosineOscillator lfo_[2];      // LFOs: Two cosine oscillators for modulation
  
  DISALLOW_COPY_AND_ASSIGN(FxEngine);
};

}  // namespace rings

#endif  // RINGS_DSP_FX_FX_ENGINE_H_
