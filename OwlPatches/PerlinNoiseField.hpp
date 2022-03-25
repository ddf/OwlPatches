#pragma once

#include "SignalProcessor.h"
#include "Patch.h"
#include "Noise.hpp"

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
    frequency = max(1.0f, freq);
  }

  void setDepth(unsigned int depth)
  {
    octaves = max((unsigned)1, depth);
  }

  void setOffsetX(float offset)
  {
    offsetX = max(0.0f, offset);
  }

  void setOffsetY(float offset)
  {
    offsetY = max(0.0f, offset);
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
    const int blockSize = min(input.getSize(), output.getSize());
    for (int i = 0; i < blockSize; ++i)
    {
      float leftIn = xin[i];
      float rightIn = yin[i];
      float x = leftIn * 0.5f + 0.5f;
      float y = rightIn * 0.5f + 0.5f;
      float f = fmData ? fmData[i] : 0;
      float nz = perlin2d(x + offsetX, y + offsetY, frequency + f, octaves);
      for (int c = 0; c < outChannels; ++c)
      {
        output.getSamples(c)[i] = nz;
      }
    }
  }

};
