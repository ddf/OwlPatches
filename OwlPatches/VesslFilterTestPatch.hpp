#pragma once

#include <functional>

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using namespace vessl::filtering;
using FilterType = biquad::df2T<float, biquad::hp<4>>;
using Filter = vessl::filter<float, FilterType>;
// @todo don't like that I want to define filter type and filter separately because the required syntax
// would like to be able to something like this:
// using Filter = vessl::filter<float, biquad::lowpass<2>>;
// which might mean making vessl::filter exclusively for use with biquad::df2T
// there are worse things?

static constexpr float BESSEL_Q = 0.57735026919f; // 1/sqrt(3)
static constexpr float SALLEN_KEY_Q = 0.5f; // 1/2
static constexpr float BUTTERWORTH_Q = 0.70710678118f; 

class VesslFilterTestPatch : public MonochromeScreenPatch
{
  Filter filter;
  FilterType df2T;

public:
  VesslFilterTestPatch() : filter(getSampleRate(), 120, 0)
  {
    registerParameter(PARAMETER_A, "Fc");
    registerParameter(PARAMETER_B, "Q");

    setParameterValue(PARAMETER_B, BUTTERWORTH_Q*0.5f);
  }

  void processAudio(AudioBuffer& audio) override
  {
    float cutoff = 120 + VoltsPerOctave::voltsToHertz(getParameterValue(PARAMETER_A)*5);
    float q = getParameterValue(PARAMETER_B)*2.0f;
    filter.cutoff() << cutoff;
    filter.q() << q;
    
    vessl::array<float> inout(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    filter.process(inout, inout);

    //float omega = vessl::math::pi<float>() * cutoff / getSampleRate();
    //df2T.set(omega, q);
    //df2T.process(inout.getData(), inout.getData(), inout.getSize());
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    //screen.clear();
    screen.setCursor(0, 8);
    screen.print("Coeff: "); screen.print((int)df2T.getCoeffSize()); screen.print("\n");
    screen.print("States: "); screen.print((int)df2T.getStateSize()); screen.print("\n");
    screen.print("Stages: "); screen.print((int)df2T.getStageCount()); screen.print("\n");
    for (int i = 0; i < df2T.getCoeffSize() / df2T.getStageCount(); ++i)
    {
      screen.print(df2T.coeff[i]);
      screen.print(" ");
    }
    for (int i = 0; i < df2T.getStateSize(); ++i)
    {
      screen.print(df2T.state[i]);
      screen.print(" ");
    }
  }
};