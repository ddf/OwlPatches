#pragma once

#include "MonochromeScreenPatch.h"
#include "VesslTestPatch.hpp"
#include "vessl/vessl.h"

using Array = vessl::array<float>;
using WhiteNoise = vessl::noiseGenerator<float, vessl::noise::white>;
using PinkNoise = vessl::noiseGenerator<float, vessl::noise::pink>;
using RedNoise = vessl::noiseGenerator<float, vessl::noise::red>;

using namespace vessl::filtering;
using SmoothingFilter = vessl::slew<float>;
using SmoothNoise = vessl::unitProcGen<float, SmoothingFilter, WhiteNoise>;

class VesslNoiseTestPatch final : public MonochromeScreenPatch
{
  WhiteNoise whiteNoise;
  PinkNoise pinkNoise;
  RedNoise redNoise;
  SmoothNoise smoothNoise; // this can sound essentially the same as red noise!
  WhiteNoise smoothStepNoise;
    
public:
  VesslNoiseTestPatch() : whiteNoise(getSampleRate()), pinkNoise(getSampleRate()), redNoise(getSampleRate())
  , smoothNoise(new SmoothingFilter(getBlockRate(), 2, 2), new WhiteNoise(getBlockRate()), getBlockRate())
  , smoothStepNoise(getBlockRate())
  {
    smoothNoise.gen()->rate() << 10;
    smoothStepNoise.rate() << 10;
    registerParameter(PARAMETER_AA, "Smooth>");
    registerParameter(PARAMETER_AB, "Rando>");
  }

  ~VesslNoiseTestPatch() override
  {
    delete smoothNoise.proc();
    delete smoothNoise.gen();
  }
    
  void processAudio(AudioBuffer& audio) override
  {
    Array audioLeft(audio.getSamples(0), audio.getSize());
    Array audioRight(audio.getSamples(1), audio.getSize());
    
    AudioWriter outL(audioLeft);
    AudioWriter outR(audioRight);
    while (outL)
    {
      outL << 2*redNoise.generate() - 1;
      outR << 2*pinkNoise.generate() - 1;
    }
    setParameterValue(PARAMETER_AA, smoothNoise.generate());
    setParameterValue(PARAMETER_AB, smoothStepNoise.generate<vessl::easing::smoothstep>());
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};