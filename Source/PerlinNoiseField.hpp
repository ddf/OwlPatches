#pragma once

#include "SignalProcessor.h"
#include "Patch.h"
#include "vessicle/Noise.hpp"
#include "vessicle/vessl/vessl.h"

class PerlinNoiseField : public MultiSignalProcessor
{
  float frequency;
  unsigned int octaves;
  float offsetX, offsetY;

  PerlinNoiseField()
    : frequency(1), octaves(1), offsetX(0), offsetY(0)
  {

  }

public:
  static PerlinNoiseField* create()
  {
    return new PerlinNoiseField();
  }

  static void destroy(PerlinNoiseField* pnf) 
  {
    delete pnf;
  }

  void setFrequency(float freq)
  {
    frequency = vessl::math::max(1.0f, freq);
  }

  void setDepth(unsigned int depth)
  {
    octaves = vessl::math::max((unsigned)1, depth);
  }

  void setOffsetX(float offset)
  {
    offsetX = vessl::math::max(0.0f, offset);
  }

  void setOffsetY(float offset)
  {
    offsetY = vessl::math::max(0.0f, offset);
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    process(input, output, FloatArray());
  }

  void process(AudioBuffer& input, AudioBuffer& output, FloatArray fm)
  {
    FloatArray xin = input.getSamples(LEFT_CHANNEL);
    FloatArray yin = input.getChannels() >= 2 ? input.getSamples(RIGHT_CHANNEL) : xin;
    float* fmData = fm.getSize() > 0 ? fm.getData() : nullptr;
    const int outChannels = output.getChannels();
    const int blockSize = vessl::math::min(input.getSize(), output.getSize());
    for (int i = 0; i < blockSize; ++i)
    {
      float leftIn = xin[i];
      float rightIn = yin[i];
      float x = leftIn * 0.5f + 0.5f;
      float y = rightIn * 0.5f + 0.5f;
      float f = fmData ? fmData[i] : 0;
      float nz = vessicle::perlin2d(x + offsetX, y + offsetY, frequency + f, octaves);
      for (int c = 0; c < outChannels; ++c)
      {
        output.getSamples(c)[i] = nz;
      }
    }
  }

};
