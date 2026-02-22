#pragma once

#include "MonochromeScreenPatch.h"
#include "FloatArray.h"
#include "vessicle/vessl/vessl.h"

class EnvTestPatch : public MonochromeScreenPatch
{
  vessl::envelope<float>::stage envStage;
  vessl::ad<float> ad;
  vessl::asr<float> asr;
  vessl::adsr<float> adsr;
  
public:
  EnvTestPatch()
  : envStage(getSampleRate())
  , ad(0.1f, 0.1f, getSampleRate())
  , asr(0.1f, 0.1f, getSampleRate())
  , adsr(0.1f, 0.1f, 0.5f, 1.0f, getSampleRate())
  {
    envStage.target() = 1.0f;
    envStage.duration() = 4.0f;
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
    else if (bid == BUTTON_2)
    {
      // if (value == ON)
      // {
      //   adsr.trigger();
      // }
      adsr.gate(value == ON);
    }
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    ad.attack().duration() = getParameterValue(PARAMETER_A)*4;
    ad.decay().duration() = getParameterValue(PARAMETER_B)*4;

    asr.attack().duration() = ad.attack().duration();
    asr.release().duration() = ad.decay().duration();

    adsr.attack().duration() = ad.attack().duration();
    adsr.decay().duration() = ad.decay().duration();

    float sustain = getParameterValue(PARAMETER_C); 
    asr.gate(sustain);
    
    FloatArray outLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray outRight = audio.getSamples(RIGHT_CHANNEL);
    for (int i = 0; i < outLeft.getSize(); ++i)
    {
      outLeft[i] = ad.generate<vessl::easing::expo::in>();
      outRight[i] = asr.generate<vessl::easing::expo::out>();
    }
    
    setButton(BUTTON_1, asr.attack().active().read<uint16_t>());
    setButton(BUTTON_2, asr.release().active().read<uint16_t>());
    setButton(PUSHBUTTON, asr.eoc().read<uint16_t>());
    // setParameterValue(PARAMETER_F, *asr.attack().target());
    // setParameterValue(PARAMETER_G, sustain);
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }
};