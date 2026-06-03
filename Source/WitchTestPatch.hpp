#pragma once

#include "Patch.h"
#include "vessicle/vessl/vessl.h"

// @todo full workout of all cv inputs/outputs and all buttons.
struct WitchTestPatch : Patch
{
  using Waveform = vessl::sample::waves::sine<float>;
  using Oscil = vessl::generators::oscil<Waveform>;
  using Clock = vessl::generators::clock<float>;

  Oscil oscillator;
  Clock clock;
  
  WitchTestPatch() // NOLINT(modernize-use-equals-default)
  : oscillator(getSampleRate(), 220.f)  
  , clock(getBlockRate(), 2, getBlockRate()*10)
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
    
    float cs = clock.generate();
    setButton(BUTTON_6, cs > 0);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_3 && value == ON)
    {
      clock.tap(samples);
    }
  }
};