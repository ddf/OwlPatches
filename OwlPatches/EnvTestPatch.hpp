#pragma once

#include "MonochromeScreenPatch.h"
#include "FloatArray.h"
#include "vessl/vessl.h"

// @todo need to test stage independently because it seems to not be working properly
class EnvTestPatch : public MonochromeScreenPatch
{
  vessl::envelope<float>::stage envStage;
  vessl::ad<float> ad;
  
public:
  EnvTestPatch() : envStage(getSampleRate()), ad(0.1f, 0.1f, getSampleRate())
  {
    envStage.target() << 1.0f;
    envStage.duration() << 4.0f;
    envStage.setSampleRate(getSampleRate());
    
    registerParameter(PARAMETER_A, "att dur");
    registerParameter(PARAMETER_B, "dec dur");
    registerParameter(PARAMETER_C, "???");
    registerParameter(PARAMETER_D, "???");
  }
  
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      ad.trigger();
      //envStage.start(0);
    }
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    ad.attack().duration() << getParameterValue(PARAMETER_A)*4;
    ad.decay().duration() << getParameterValue(PARAMETER_B)*4;
    
    FloatArray outLeft = audio.getSamples(LEFT_CHANNEL);
    for (int i = 0; i < outLeft.getSize(); ++i)
    {
      outLeft[i] = ad.generate();
      //outLeft[i] = envStage.generate();
    }
    
    setButton(BUTTON_1, ad.attack().active().read<uint16_t>());
    setButton(BUTTON_2, ad.decay().active().read<uint16_t>());
    setButton(PUSHBUTTON, ad.eoc().read<uint16_t>());
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};