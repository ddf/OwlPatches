#include "SignalProcessor.h"

template<int MAX_BITS>
class BitCrusher : public SignalProcessor
{
  const float sampleRate;
  float bitRate;
  float bitDepth;
  float bitsVal;
  float sampleCount;
  float sample;

  const int maxBitsVal = (1 << MAX_BITS) - 1;

public:
  BitCrusher(float sr, float br, int depth = MAX_BITS)
    : sampleRate(sr), sampleCount(1)
  {
    setBitRate(br);
    setBitDepth(depth);
  }

  void setBitRate(float rate)
  {
    bitRate = max(1, rate) / sampleRate;
  }

  void setBitDepth(float bits)
  {
    bitDepth = min(max(2, bits), MAX_BITS);
    bitsVal = powf(2, bitDepth) - 1;
  }

  float process(float input) override
  {
    sampleCount += bitRate;

    if (sampleCount >= 1)
    {
      sample = input;
      sampleCount -= 1;
    }

    int val = (sample) * bitsVal;
    //val = val >> (MAX_BITS - bitDepth);
    return ((float)val / bitsVal);
  }

  void process(FloatArray input, FloatArray output) override
  {
    SignalProcessor::process(input, output);
  }

  static BitCrusher* create(float sampleRate, float bitRate)
  {
    return new BitCrusher(sampleRate, bitRate);
  }

  static void destroy(BitCrusher* bitCrusher)
  {
    delete bitCrusher;
  }

};
