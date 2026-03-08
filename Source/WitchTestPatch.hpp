#pragma once

#include "Patch.h"
#include "vessicle/vessl/vessl.h"

// @todo full workout of all cv inputs/outputs and all buttons.
class WitchTestPatch : public Patch
{
  vessl::oscil<vessl::waves::sine<>> oscillator;
  
public:
  WitchTestPatch() : oscillator(getSampleRate(), 220.f)
  {
  }

  void processAudio(AudioBuffer& audio) override
  {
    vessl::array<float> left(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    vessl::array<float> right(audio.getSamples(RIGHT_CHANNEL), audio.getSize());
    
    auto out = left.getWriter();
    while (out.available())
    {
      out << oscillator.generate();
    }
    left.copyTo(right);
  }
};