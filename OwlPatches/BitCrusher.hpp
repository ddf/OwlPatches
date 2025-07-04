#pragma once

#include "SignalProcessor.h"
#include "Easing.h"

template<uint32_t MAX_BITS>
class BitCrusher final : public SignalProcessor
{
  float sampleRate;
  float bitRate;
  float bitDepth;
  float bitsVal;
  float sampleCount;
  float sample;
  float prevInput;
  bool mangle;

public:
  BitCrusher(const float sr, const float br, const int depth = MAX_BITS)
    : sampleRate(sr), sampleCount(1), mangle(false), prevInput(0)
  {
    setBitRate(br);
    setBitDepth(depth);
  }

  void setBitRate(const float rate)
  {
    bitRate = max(1.0f, rate) / sampleRate;
  }

  void setBitDepth(const float bits)
  {
    bitDepth = min(max(2.0f, bits), static_cast<float>(MAX_BITS));
    bitsVal = powf(2, bitDepth) - 1;
  }

  void setMangle(const bool on)
  {
    mangle = on;
  }

  using SignalProcessor::process;

  float process(float input) override
  {
    sampleCount += bitRate;

    if (sampleCount >= 1)
    {
      sampleCount -= 1;
      sample = Easing::interp(prevInput, input, sampleCount);
    }

    int val = sample * bitsVal;
    if (mangle)
    {
      val ^= static_cast<int>(prevInput * bitsVal);
    }
    prevInput = input;
    return static_cast<float>(val) / bitsVal;
  }

  static BitCrusher* create(float sampleRate, float bitRate)
  {
    return new BitCrusher(sampleRate, bitRate);
  }

  static void destroy(const BitCrusher* bitCrusher)
  {
    delete bitCrusher;
  }

};
