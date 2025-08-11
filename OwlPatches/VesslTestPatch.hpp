#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using Oscil = vessl::oscil<float, vessl::waves::sine<float>>;
using Ramp = vessl::ramp<float>;
using AudioReader = vessl::array<float>::reader;
using AudioWriter = vessl::array<float>::writer;
using CircularBuffer = vessl::ring<float>;

class VesslTestPatch final : public MonochromeScreenPatch
{
  Oscil osc;
  VoltsPerOctave voct;
  Ramp ramp;

  float buffData[512];
  CircularBuffer buffer;
public:
  VesslTestPatch() : osc(getSampleRate()), voct(true), ramp(getSampleRate(), 0, 1, 0), buffer(buffData, 512)
  {
    registerParameter(PARAMETER_A, ramp.duration().name());
    setParameterValue(PARAMETER_A, 0.1f);

    int pid = PARAMETER_B;
    for (auto& param : osc)
    {
      registerParameter(static_cast<PatchParameterId>(pid++), param.name());
    }

    ramp.trigger();
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    int bufferSize = audio.getSize();
    AudioReader inLeft = AudioReader(audio.getSamples(LEFT_CHANNEL), bufferSize);
    AudioReader inRight = AudioReader(audio.getSamples(RIGHT_CHANNEL), bufferSize);
    AudioWriter outL = AudioWriter(audio.getSamples(LEFT_CHANNEL), bufferSize);
    AudioWriter outR = AudioWriter(audio.getSamples(RIGHT_CHANNEL), bufferSize);
    
    ramp.duration() << getParameterValue(PARAMETER_A);
    osc.fHz() << 60 + getParameterValue(PARAMETER_B)*4000;

    uint16_t eorState = OFF;
    uint16_t eorIndex = 0;
    while (inLeft)
    {
      osc.pm() << inLeft.read();
      osc.fmExp() << inRight.read();
      
      outL << (osc.generate() * ramp.generate());
      
      if (eorState == OFF)
      {
        eorState = *ramp.eor() > 0;
        eorIndex = eorState == OFF ? eorIndex + 1 : eorIndex;
      }
    }

    buffer << inLeft.reset();
    outR << buffer;
    
    //outR << inLeft.reset();

    setButton(BUTTON_1, eorState, eorIndex);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      ramp.trigger();
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 10);
    //screen.setTextSize(2);
    // @todo this doesn't work for some reason, something is getting lost with the use of unitInit
    screen.print(osc.name());
    //screen.print("hello world");
  }
};