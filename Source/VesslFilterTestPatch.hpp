#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessicle/vessl/vessl.h"
#include "vessicle/vessl/vessl.inl"
#include "vessicle/vessl/vessl_units.h"

using DcBlock = vessl::processors::filter<float, vessl::filtering::dc_block>;
using Filter  = vessl::processors::filter<float, vessl::filtering::moog::v2::low_pass>;
//using Filter  = vessl::processors::filter<float, vessl::filtering::moog::low_pass_2>;
//using Filter  = vessl::processors::filter<float, vessl::filtering::biquad<2>::low_pass>;

class VesslFilterTestPatch : public MonochromeScreenPatch
{
  using Args = vessl::filtering::args;

  DcBlock dcBlock;
  Filter filter;
  Filter::function filterFunc;
  Args funcArgs;

public:
  VesslFilterTestPatch() 
  : dcBlock(getSampleRate())
  , filter(getSampleRate(), 120, vessl::filtering::q::butterworth<float>())
  , funcArgs(getSampleRate(), 1.f, 0.f, vessl::gain_t(0.f))
  {
    int pid = PARAMETER_A;
    for (vessl::parameter param : filter.parameters())
    {
      registerParameter(static_cast<PatchParameterId>(pid++), param.description().name);
    }

    setParameterValue(PARAMETER_B, 0.5f);
    setParameterValue(PARAMETER_C, 0.5f); // 0 dB gain
  }

  void processAudio(AudioBuffer& audio) override
  {
    float cutoff = VoltsPerOctave::voltsToHertz(-3.f + getParameterValue(PARAMETER_A)*6.f);
    float q = vessl::math::lerp(vessl::filtering::q::sallen_key<float>(), 1.0f, getParameterValue(PARAMETER_B));
    //float q = getParameterValue(PARAMETER_B)*0.95f;
    vessl::gain_t g = vessl::gain_t::from_decibels(vessl::math::lerp(-6.f, 6.f, getParameterValue(PARAMETER_C)));
    filter.fhz() = cutoff;
    filter.q() = q;
    filter.emphasis() = g;
    
    vessl::array<float> inout(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    // add a dc offset so we can see that it is removed by dc block
    inout.offset(2.0f);
    inout >> dcBlock >> filter >> inout;

    float dummy = 0;
    funcArgs = Args(getSampleRate(), cutoff, q, g);
    filterFunc.process(&dummy, &dummy, 1, funcArgs);
  }

  template<typename FF>
  static void drawFilter(MonochromeScreenBuffer& screen, const FF& filterFunc, const Args& args)
  {
    screen.setCursor(0, 8);
    screen.print("Fhz: ");
    screen.print(args.hz);
    screen.print("\nFc: ");
    screen.print(100*args.fc());
  }

  void processScreen(MonochromeScreenBuffer& screen) override;
};

template<>
void VesslFilterTestPatch::drawFilter<vessl::filtering::biquad<2>::low_pass<float>>(
  MonochromeScreenBuffer& screen, 
  const vessl::filtering::biquad<2>::low_pass<float>& filterFunc, const Args& args)
{
    screen.setCursor(0, 8);
    screen.print("Coeff: "); screen.print(static_cast<int>(filterFunc.df2.coeff_count)); screen.print("\n");
    screen.print("States: "); screen.print(static_cast<int>(filterFunc.df2.state_count)); screen.print("\n");
    screen.print("Stages: "); screen.print(static_cast<int>(2)); screen.print("\n");
    for (int i = 0; i < filterFunc.df2.coeff_count / 2; ++i)
    {
      screen.print(filterFunc.df2.coeff[i]);
      screen.print(" ");
    }
    for (int i = 0; i < filterFunc.df2.state_count; ++i)
    {
      screen.print(filterFunc.df2.state[i]);
      screen.print(" ");
    }
}

void VesslFilterTestPatch::processScreen(MonochromeScreenBuffer& screen)
{
  drawFilter<Filter::function>(screen, filterFunc, funcArgs);
}