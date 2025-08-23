#pragma once

#include "MonochromeScreenPatch.h"
#include "FloatArray.h"
#include "vessl/vessl.h"

// @todo need to test stage independently because it seems to not be working properly
class EnvTestPatch : public MonochromeScreenPatch
{
  vessl::envelope<float>::stage envStage;
  vessl::ad<float> ad;
  vessl::asr<float> asr;
  
public:
  EnvTestPatch()
  : envStage(getSampleRate())
  , ad(0.1f, 0.1f, getSampleRate())
  , asr(0.1f, 0.1f, getSampleRate())
  {
    envStage.target() << 1.0f;
    envStage.duration() << 4.0f;
    envStage.setSampleRate(getSampleRate());
    
    registerParameter(PARAMETER_A, "att dur");
    registerParameter(PARAMETER_B, "dec dur");
    registerParameter(PARAMETER_C, "asr gate");
    registerParameter(PARAMETER_D, "???");
  }
  
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      ad.trigger();
      //envStage.start(0);
    }
    else if (bid == BUTTON_2 && value == ON)
    {
      asr.trigger();
    }
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    ad.attack().duration() << getParameterValue(PARAMETER_A)*4;
    ad.decay().duration() << getParameterValue(PARAMETER_B)*4;

    asr.attack().duration() << *ad.attack().duration();
    asr.decay().duration() << *ad.decay().duration();

    float sustain = getParameterValue(PARAMETER_C); 
    asr.gate(sustain);
    
    FloatArray outLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray outRight = audio.getSamples(RIGHT_CHANNEL);
    for (int i = 0; i < outLeft.getSize(); ++i)
    {
      outLeft[i] = ad.generate();
      outRight[i] = asr.generate();
    }
    
    setButton(BUTTON_1, ad.attack().active().read<uint16_t>());
    setButton(BUTTON_2, ad.decay().active().read<uint16_t>());
    setButton(PUSHBUTTON, ad.eoc().read<uint16_t>());
    setParameterValue(PARAMETER_F, *asr.attack().target());
    setParameterValue(PARAMETER_G, sustain);
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};