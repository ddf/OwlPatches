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
  bool mangle;
  float prev;

  const int maxBitsVal = (1 << MAX_BITS) - 1;

public:
  BitCrusher(float sr, float br, int depth = MAX_BITS)
    : sampleRate(sr), sampleCount(1), mangle(false), prev(0)
  {
    setBitRate(br);
    setBitDepth(depth);
  }

  void setBitRate(float rate)
  {
    bitRate = max(1.0f, rate) / sampleRate;
  }

  void setBitDepth(float bits)
  {
    bitDepth = min(max(2.0f, bits), (float)MAX_BITS);
    bitsVal = powf(2, bitDepth) - 1;
  }

  void setMangle(bool on)
  {
    mangle = on;
  }

  float process(float input) override
  {
    sampleCount += bitRate;

    if (sampleCount >= 1)
    {
      sample = input;
      sampleCount -= 1;
    }

    int val = sample * bitsVal;
    if (mangle)
    {
      val ^= int(prev * bitsVal);
    }
    prev = input;
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
