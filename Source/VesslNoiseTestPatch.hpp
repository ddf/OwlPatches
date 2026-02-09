#pragma once

#include "MonochromeScreenPatch.h"
#include "VesslTestPatch.hpp"
#include "vessl/vessl.h"

using Array = vessl::array<float>;
using WhiteNoise = vessl::noiseGenerator<float, vessl::noise::white>;
using PinkNoise = vessl::noiseGenerator<float, vessl::noise::pink>;
using RedNoise = vessl::noiseGenerator<float, vessl::noise::red>;

using namespace vessl::filtering;
using SlewFilter = vessl::slew<float>;
using Filter = vessl::filter<float, biquad<1>::lowPass>;

class VesslNoiseTestPatch final : public MonochromeScreenPatch
{
  WhiteNoise whiteNoise;
  PinkNoise pinkNoise;
  RedNoise redNoise;
  
  SlewFilter blockNoiseFilter; // this can sound essentially the same as red noise!
  Filter blockNoiseBiquad;
  WhiteNoise blockNoise;
    
public:
  VesslNoiseTestPatch() : whiteNoise(getSampleRate()), pinkNoise(getSampleRate()), redNoise(getSampleRate())
  , blockNoiseFilter(getBlockRate(), 3.f, 3.f)
  , blockNoiseBiquad(getBlockRate(), 20.f)
  , blockNoise(getBlockRate())
  {
    blockNoise.rate() = 10.f;
    registerParameter(PARAMETER_AA, "Smooth>");
    registerParameter(PARAMETER_AB, "Rando>");
  }

  ~VesslNoiseTestPatch() override = default;

  void processAudio(AudioBuffer& audio) override
  {
    Array audioLeft(audio.getSamples(0), audio.getSize());
    Array audioRight(audio.getSamples(1), audio.getSize());
    
    AudioWriter outL(audioLeft);
    AudioWriter outR(audioRight);
    while (outL)
    {
      outL << 2.f*redNoise.generate()  - 1.f;
      outR << 2.f*pinkNoise.generate() - 1.f;
    }

    float bnsz = blockNoise.generate();
    setParameterValue(PARAMETER_AA, blockNoiseFilter.process(bnsz));
    setParameterValue(PARAMETER_AB, vessl::math::constrain(blockNoiseBiquad.process(bnsz),0.f, 0.999f));
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};