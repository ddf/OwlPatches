#pragma once

#include "SignalProcessor.h"
#include "Patch.h"
#include "Noise.hpp"

class PerlinNoiseField : public MultiSignalProcessor
{
  float frequency;
  unsigned int depth;
  float offsetX, offsetY;

  PerlinNoiseField()
    : frequency(1), depth(1), offsetX(0), offsetY(0)
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
    frequency = max(0, freq);
  }

  void setDepth(int depth)
  {
    depth = max(1, depth);
  }

  void setOffsetX(float offset)
  {
    offsetX = max(0, offset);
  }

  void setOffsetY(float offset)
  {
    offsetY = max(0, offset);
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    FloatArray xin = input.getSamples(LEFT_CHANNEL);
    FloatArray yin = input.getChannels() >= 2 ? input.getSamples(RIGHT_CHANNEL) : xin;
    FloatArray outL = output.getSamples(LEFT_CHANNEL);
    FloatArray outR = output.getChannels() >= 2 ? output.getSamples(RIGHT_CHANNEL) : outL;
    const int blockSize = min(input.getSize(), output.getSize());
    for (int i = 0; i < blockSize; ++i)
    {
      float leftIn = xin[i];
      float rightIn = yin[i];
      float x = leftIn * 0.5f + 0.5f;
      float y = rightIn * 0.5f + 0.5f;
      float nz = perlin2d(x + offsetX, y + offsetY, frequency, depth) * 2 - 1;
      outL[i] = nz; // leftIn + leftIn * nz;
      outR[i] = nz; // rightIn + rightIn * nz;
    }
  }

};
