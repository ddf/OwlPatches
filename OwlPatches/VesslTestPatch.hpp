#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using Oscil = vessl::oscil<float, vessl::waves::sine<float>>;
using Ramp = vessl::ramp<float>;

class VesslTestPatch final : public MonochromeScreenPatch
{
  Oscil osc;
  VoltsPerOctave voct;
  Ramp ramp;
public:
  VesslTestPatch() : osc(getSampleRate()), voct(true), ramp(getSampleRate(), 0, 1, 0)
  {
    registerParameter(PARAMETER_A, ramp.duration().name());
    setParameterValue(PARAMETER_A, 0.1f);
    
    for (int i = 0; i < osc.getSize(); ++i)
    {
      registerParameter(static_cast<PatchParameterId>(PARAMETER_B + i), osc[i].name());
    }

    ramp.trigger();
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    ramp.duration() << getParameterValue(PARAMETER_A);
    osc.fHz() << 60 + getParameterValue(PARAMETER_B)*4000;
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);
    uint16_t eorState = OFF;
    uint16_t eorIndex = 0;
    for (int i = 0; i < audio.getSize(); ++i)
    {
      osc.pm() << left[i];
      osc.fmExp() << voct.sampleToVolts(right[i]);
      float s = osc.generate() * ramp.generate();
      left[i] = s;
      right[i] = s;
      if (eorState == OFF && *ramp.eor() > 0)
      {
        eorState = ON;
        eorIndex = i;
      }
    }

    setButton(BUTTON_1, eorState, eorIndex);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      ramp.trigger();
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 10);
    //screen.setTextSize(2);
    // @todo this doesn't work for some reason, something is getting lost with the use of unitInit
    screen.print(osc.name());
    //screen.print("hello world");
  }
};