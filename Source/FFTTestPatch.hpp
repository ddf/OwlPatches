#pragma once

#include "Patch.h"
#include "FastFourierTransform.h"
#include "FloatArray.h"
#include "ComplexFloatArray.h"

#define SPECTRUM_SIZE 2048

class FFTTestPatch : public Patch
{
  FastFourierTransform* fft;
  ComplexFloatArray complex;
  FloatArray output;

public:
  FFTTestPatch() : Patch()
  {
    fft = FastFourierTransform::create(SPECTRUM_SIZE);
    complex = ComplexFloatArray::create(SPECTRUM_SIZE);
    output = FloatArray::create(SPECTRUM_SIZE);

    registerParameter(PARAMETER_F, "CPU>>");
  }

  ~FFTTestPatch()
  {
    FastFourierTransform::destroy(fft);
    ComplexFloatArray::destroy(complex);
    FloatArray::destroy(output);
  }

  // returns CPU% as [0,1] value
  float getElapsedTime()
  {
    return getElapsedCycles() / getBlockSize() / 10000.0f;
  }

  void processAudio(AudioBuffer& audio) override
  {
    float time = getElapsedTime();
    fft->ifft(complex, output);
    float delta = getElapsedTime() - time;
    setParameterValue(PARAMETER_F, delta);
  }

};
