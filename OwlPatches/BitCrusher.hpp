#include "SignalProcessor.h"

template<int MAX_BITS>
class BitCrusher : public SignalProcessor
{
  const float sampleRate;
  int bitRate;
  int bitDepth;
  int bitsVal;
  int sampleCount;
  float sample;

  const int maxBitsVal = (1 << MAX_BITS) - 1;

public:
  BitCrusher(float sampleRate, float rate, int depth = MAX_BITS)
    : sampleRate(sr), sampleCount(0)
  {
    setBitRate(rate);
    setBitDepth(depth);
  }

  void setBitRate(float rate)
  {
    bitRate = (int)(sampleRate / max(1, rate));
  }

  void setBitDepth(int bits)
  {
    bitDepth = min(max(2, bits), MAX_BITS);
    bitsVal = (1 << bitDepth) - 1;
  }

  float process(float input) override
  {
    if (sampleCount == 0)
    {
      sample = input;
    }

    sampleCount = (sampleCount + 1) % bitRate;

    int val = (sample*0.5f + 0.5f) * maxBitsVal;
    val = val >> (MAX_BITS - bitDepth);
    return ((float)val / bitsVal) * 2 - 1;
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
