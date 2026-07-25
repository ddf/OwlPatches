#pragma once

#include "Patch.h"
#include "vessicle/vessl/vessl.h"

#define SPECTRUM_SIZE 2048

using FastFourierTransform = vessl::transform::fft<float>;
using Complex = vessl::transform::complex<float>;
using ComplexArray = vessl::array<Complex>;
using SampleArray = vessl::array<float>;

class FFTTestPatch : public Patch
{
  FastFourierTransform fft;
  ComplexArray complex;
  SampleArray output_a;
  SampleArray output_b;
  SampleArray window;
  size_t output_a_idx;
  size_t output_b_idx;

public:
  FFTTestPatch() : Patch()
  , fft(SPECTRUM_SIZE)
  , complex(new Complex[SPECTRUM_SIZE/2], SPECTRUM_SIZE/2)
  , output_a(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , output_b(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , window(new float[SPECTRUM_SIZE], SPECTRUM_SIZE)
  , output_a_idx(SPECTRUM_SIZE)
  , output_b_idx(SPECTRUM_SIZE/2)
  {
    vessl::sample::windows::render(vessl::sample::windows::type::triangle, window);
    complex[128].set_polar(8.f, 0);
    complex[0] = Complex(0,0);
    registerParameter(PARAMETER_F, "CPU>>");
  }

  ~FFTTestPatch()
  {
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
        fft.inverse(complex, output_a);
        output_a_idx = 0;
      }
      if (output_b_idx == SPECTRUM_SIZE)
      {
        fft.inverse(complex, output_b);
        output_b_idx = 0;
      }
      audio.getSamples(0)[i] = out;
      audio.getSamples(1)[i] = out;
    }
  }

};
