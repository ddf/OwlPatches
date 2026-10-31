#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessicle/vessl/vessl.h"
#include "vessicle/vessl/vessl.inl"
#include "vessicle/vessl/vessl_units.h"

using DcBlock = vessl::processors::filter<float, vessl::filtering::dc_block>;
using Filter  = vessl::processors::filter<float, vessl::filtering::moog::low_pass_2>;
//using Filter  = vessl::processors::filter<float, vessl::filtering::biquad<2>::low_pass>;

class VesslFilterTestPatch : public MonochromeScreenPatch
{
  DcBlock dcBlock;
  Filter filter;
  Filter::function filterFunc;

public:
  VesslFilterTestPatch() : dcBlock(getSampleRate()), filter(getSampleRate(), 120, vessl::filtering::q::butterworth<float>())
  {
    int pid = PARAMETER_A;
    for (vessl::parameter param : filter.parameters())
    {
      registerParameter(static_cast<PatchParameterId>(pid++), param.description().name);
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    float cutoff = 60 + VoltsPerOctave::voltsToHertz(getParameterValue(PARAMETER_A)*4);
    float q = vessl::math::lerp(vessl::filtering::q::butterworth<float>(), 4.0f, getParameterValue(PARAMETER_B));
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
    vessl::filtering::args fargs(getSampleRate(), cutoff, q, g);
    filterFunc.process(&dummy, &dummy, 1, fargs);
  }

  template<typename FF>
  static void drawFilter(MonochromeScreenBuffer& screen, const FF& filterFunc)
  {
    screen.setCursor(0, 8);
    screen.print("Default");
  }

  void processScreen(MonochromeScreenBuffer& screen) override;
};

template<>
void VesslFilterTestPatch::drawFilter<vessl::filtering::biquad<2>::low_pass<float>>(
  MonochromeScreenBuffer& screen, 
  const vessl::filtering::biquad<2>::low_pass<float>& filterFunc)
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
  drawFilter<Filter::function>(screen, filterFunc);
}