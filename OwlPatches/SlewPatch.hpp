#pragma once

#include "MonochromeScreenPatch.h"
#include "basicmaths.h"
#include "MonochromeScreenPatch.h"
#include "vessl/vessl.h"

class SlewPatch : public MonochromeScreenPatch
{
  vessl::slew<float> slew;

public:
  SlewPatch() : slew(getSampleRate(), 1, 1)
  {
    registerParameter(PARAMETER_A, "rise");
    registerParameter(PARAMETER_B, "fall");
  }

  void processAudio(AudioBuffer& audio) override
  {
    slew.rise() << getParameterValue(PARAMETER_A)*10;
    slew.fall() << getParameterValue(PARAMETER_B)*10;

    vessl::array<float> in(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    vessl::array<float> out(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    in.scale(-1);
    slew.process(in, out);
    out.scale(-1);

    setButton(BUTTON_1, slew.rising().read<uint16_t>());
    setButton(BUTTON_2, slew.falling().read<uint16_t>());
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};