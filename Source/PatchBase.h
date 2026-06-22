#pragma once

#define USE_MIDI_CALLBACK
#include "MonochromeScreenPatch.h"
#include "OpenWareMidiControl.h"

class PatchBase : public MonochromeScreenPatch
{
public:
  void processScreen(MonochromeScreenBuffer &screen) override
  {
    
  }
  
  
  void processAudio(AudioBuffer &audio) override
  {
#ifdef OWL_WITCH
    char debug_msg[64];
    char* debug_cpy = stpcpy(debug_msg, "BA ");
    debug_cpy = stpcpy(debug_cpy, msg_ftoa(getParameterValue(PARAMETER_BA), 10));
    debug_cpy = stpcpy(debug_cpy, " BB ");
    debug_cpy = stpcpy(debug_cpy, msg_ftoa(getParameterValue(PARAMETER_BB), 10));
    debug_cpy = stpcpy(debug_cpy, " BC ");
    debug_cpy = stpcpy(debug_cpy, msg_ftoa(getParameterValue(PARAMETER_BC), 10));
    debug_cpy = stpcpy(debug_cpy, " BD ");
    debug_cpy = stpcpy(debug_cpy, msg_ftoa(getParameterValue(PARAMETER_BD), 10));
    debugMessage(debug_msg);
#endif
  }

  void processMidi(MidiMessage msg) override
  {
    MonochromeScreenPatch::processMidi(msg);
    
#ifdef OWL_WITCH
    if (msg.isControlChange())
    {
      // to get the same -2x to 2x behavior as changing these with MODE + KNOB
      // we have to set the parameter with values [-2,2]
      auto constexpr to_attenuverter = [](uint8_t midi_value)
      {
        return midi_value < 64 ? -2.f + static_cast<float>(midi_value) / 32
          : midi_value > 64 ? static_cast<float>(midi_value - 64) / 32
          : 0.f;
      };
      
      switch (msg.getControllerNumber())
      {
        case PATCH_PARAMETER_BA:
        {
          setParameterValue(PARAMETER_BA, to_attenuverter(msg.getControllerValue()));
        }
        break;
        
        case PATCH_PARAMETER_BB:
        {
          setParameterValue(PARAMETER_BB, to_attenuverter(msg.getControllerValue()));
        }
        break;
        
        case PATCH_PARAMETER_BC:
        {
          setParameterValue(PARAMETER_BC, to_attenuverter(msg.getControllerValue()));
        }
        break;
        
        case PATCH_PARAMETER_BD:
        {
          setParameterValue(PARAMETER_BD, to_attenuverter(msg.getControllerValue()));
        }
        break;
        
        default: break;
      }
    }
#endif
  }
};