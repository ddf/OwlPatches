#pragma once

#include "Patch.h"
#include "VesslSpectralTestPatch.hpp"
#include "vessicle/vessl/vessl.h"

#define SPECTRUM_SIZE 2048

using FastFourierTransform = vessl::transform::fft<float>;
using FrequencyBand = SpectralGenerator<float, SPECTRUM_SIZE>::frequency_band;
using Complex = vessl::transform::complex<float>;
using ComplexArray = vessl::array<Complex>;
using SampleArray = vessl::array<float>;

class FFTTestPatch : public Patch
{
  FastFourierTransform fft;
  vessl::array<FrequencyBand> bands;
  ComplexArray complex;
  SampleArray output_a;
  SampleArray output_b;
  SampleArray window;
  size_t output_a_idx;
  size_t output_b_idx;

public:
  FFTTestPatch() : Patch()
  , fft(SPECTRUM_SIZE)
  , bands(new FrequencyBand[SPECTRUM_SIZE/2], SPECTRUM_SIZE/2)
  , complex(new Complex[SPECTRUM_SIZE/2], SPECTRUM_SIZE/2)
  , output_a(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , output_b(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , window(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , output_a_idx(SPECTRUM_SIZE)
  , output_b_idx(SPECTRUM_SIZE/2)
  {
    vessl::sample::windows::render(vessl::sample::windows::type::triangle, window);
    for (int i = 1; i < bands.size(); ++i)
    {
      bands[i].magnitude = 0;
      bands[i].phase = vessl::math::random::u32();
    }
    bands[62].magnitude = 8.f;
    registerParameter(PARAMETER_F, "CPU>>");
  }

  ~FFTTestPatch() override
  {
    delete[] bands.data();
    delete[] complex.data();
    delete[] output_a.data();
    delete[] output_b.data();
    delete[] window.data();
  }

  // returns CPU% as [0,1] value
  float getElapsedTime()
  {
    return getElapsedCycles() / getBlockSize() / 10000.0f;
  }

  virtual void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
  }

  void processAudio(AudioBuffer& audio) override
  {
    for(int i = 0; i < audio.getSize(); ++i)
    {
      float out = 0;
      if (output_a_idx < SPECTRUM_SIZE)
      {
        out += output_a[output_a_idx] * window[output_a_idx];
        ++output_a_idx;
      }
      if (output_b_idx < SPECTRUM_SIZE)
      {
        out += output_b[output_b_idx] * window[output_b_idx];
        ++output_b_idx;
      }
      if (output_a_idx == SPECTRUM_SIZE)
      {
        complex[0].set_complex(0,0);
        for (int c = 1; c < complex.size(); ++c)
        {
          FrequencyBand& band = bands[c-1];
          float m = band.magnitude;
          vessl::phase_t z = m>0 ? band.phase : vessl::phase_zero;
          complex[c].set_polar(m, z);
        }
        fft.inverse(complex, output_a);
        output_a_idx = 0;
      }
      if (output_b_idx == SPECTRUM_SIZE)
      {
        complex[0].set_complex(0,0);
        for (int c = 1; c < complex.size(); ++c)
        {
          FrequencyBand& band = bands[c-1];
          float m = band.magnitude;
          vessl::phase_t z = (m > 0) ? (c&1 ? band.phase + vessl::phase_180 : band.phase) : vessl::phase_zero; 
          complex[c].set_polar(m, z);
        }
        fft.inverse(complex, output_b);
        output_b_idx = 0;
      }
      audio.getSamples(0)[i] = out;
      audio.getSamples(1)[i] = out;
    }
  }

};
