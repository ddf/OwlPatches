#pragma once

#include "owl/PatchBase.h"
#include "vessicle/vessl/vessl.h"

class _SlewPatch : public PatchBase
{
  vessl::processors::slew<float> slew;

public:
  _SlewPatch() : slew(getSampleRate(), 1, 1)
  {
    registerParameter(PARAMETER_A, "rise");
    registerParameter(PARAMETER_B, "fall");
  }

  void processAudio(AudioBuffer& audio) override
  {
    slew.rise() = 10 - getParameterValue(PARAMETER_A)*10;
    slew.fall() = 10 - getParameterValue(PARAMETER_B)*10;

    vessl::array<float> in(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    vessl::array<float> out(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    (in.scale(-1.f) >> slew >> out).scale(-1.f);

    setButton(BUTTON_1, slew.rising().read<uint16_t>());
    setButton(BUTTON_2, slew.falling().read<uint16_t>());
  }
};