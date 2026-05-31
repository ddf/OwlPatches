#pragma once

#include "Patch.h"
#include "vessicle/vessl/vessl.h"

// @todo full workout of all cv inputs/outputs and all buttons.
struct WitchTestPatch : Patch
{
  using Waveform = vessl::sample::waves::sine<float>;
  using Oscil = vessl::generators::oscil<Waveform>;

  Oscil oscillator;
  
  WitchTestPatch() : oscillator(getSampleRate(), 220.f)  // NOLINT(modernize-use-equals-default)
  {
  }

  void processAudio(AudioBuffer& audio) override
  {
    vessl::array<float> left(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    vessl::array<float> right(audio.getSamples(RIGHT_CHANNEL), audio.getSize());
    
    auto out = left.make_writer();
    while (out.available())
    {
      out << oscillator.generate();
    }
    left.copy_to(right);
  }
};