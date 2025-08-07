#pragma once

#include "Patch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using Oscil = vessl::oscil<float, vessl::waves::sine<float>>;

class VesslTestPatch final : public Patch
{
  Oscil osc;
  VoltsPerOctave voct;
  
public:
  VesslTestPatch() : osc(getSampleRate()), voct(true)
  {
    for (int i = 0; i < osc.getParameterCount(); ++i)
    {
      registerParameter(static_cast<PatchParameterId>(i), osc.getParameter(i).name());
    }
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    osc.fHz().write(60 + getParameterValue(PARAMETER_A)*4000);
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);
    for (int i = 0; i < audio.getSize(); ++i)
    {
      osc.pm().write(left[i]);
      osc.fmExp().write(voct.sampleToVolts(right[i]));
      float s = osc.generate();
      left[i] = s;
      right[i] = s;
    }
  }
};