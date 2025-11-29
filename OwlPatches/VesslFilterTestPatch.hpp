#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using namespace vessl::filtering;
using DcBlock = vessl::filter<float, dcblock>;
using Filter = vessl::filter<float, biquad<4>::lowPass>;

class VesslFilterTestPatch : public MonochromeScreenPatch
{
  DcBlock dcBlock;
  Filter filter;
  Filter::function filterFunc;

public:
  VesslFilterTestPatch() : dcBlock(getSampleRate()), filter(getSampleRate(), 120, q::butterworth<float>())
  {
    registerParameter(PARAMETER_A, "Fc");
    registerParameter(PARAMETER_B, "Q");
    registerParameter(PARAMETER_C, "Gain");
  }

  void processAudio(AudioBuffer& audio) override
  {
    float cutoff = 60 + VoltsPerOctave::voltsToHertz(getParameterValue(PARAMETER_A)*4);
    float q = vessl::easing::lerp(q::butterworth<float>(), 5.0f, getParameterValue(PARAMETER_B));
    vessl::gain g = vessl::gain::fromDecibels(vessl::easing::lerp(-6.f, 6.f, getParameterValue(PARAMETER_C)));
    filter.cutoff() = cutoff;
    filter.q() = q;
    filter.emphasis() = g;
    
    vessl::array<float> inout(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    // add a dc offset so we can see that it is removed by dc block
    inout.offset(2.0f);
    inout >> dcBlock >> filter >> inout;

    float dummy = 0;
    args fargs(getSampleRate(), cutoff, q, g);
    filterFunc.process(&dummy, &dummy, 1, fargs);
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    //screen.clear();
    screen.setCursor(0, 8);
    screen.print("Coeff: "); screen.print(static_cast<int>(filterFunc.df2.getCoeffSize())); screen.print("\n");
    screen.print("States: "); screen.print(static_cast<int>(filterFunc.df2.getStateSize())); screen.print("\n");
    screen.print("Stages: "); screen.print(static_cast<int>(filterFunc.df2.getStageCount())); screen.print("\n");
    for (int i = 0; i < filterFunc.df2.getCoeffSize() / filterFunc.df2.getStageCount(); ++i)
    {
      screen.print(filterFunc.df2.coeff[i]);
      screen.print(" ");
    }
    for (int i = 0; i < filterFunc.df2.getStateSize(); ++i)
    {
      screen.print(filterFunc.df2.state[i]);
      screen.print(" ");
    }
  }
};