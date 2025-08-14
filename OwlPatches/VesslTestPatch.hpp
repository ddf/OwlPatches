#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

using Oscil = vessl::oscil<float, vessl::waves::sine<float>>;
using Ramp = vessl::ramp<float>;
using Delay = vessl::delay<float>;
using Array = vessl::array<float>;
using AudioReader = vessl::array<float>::reader;
using AudioWriter = vessl::array<float>::writer;
using CircularBuffer = vessl::ring<float>;

class VesslTestPatch final : public MonochromeScreenPatch
{
  Oscil osc;
  VoltsPerOctave voct;
  Ramp ramp;

  FloatArray delayBuffer;
  Delay delay;
  SmoothFloat delayTime;
public:
  VesslTestPatch() : osc(getSampleRate()), voct(true), ramp(getSampleRate(), 0, 1, 0)
  , delayBuffer(FloatArray::create(static_cast<int>(getSampleRate())*2))
  , delay(Array(delayBuffer.getData(), delayBuffer.getSize()), getSampleRate(), 0.2f)
  {
    registerParameter(PARAMETER_A, ramp.duration().getName());
    setParameterValue(PARAMETER_A, 0.1f);

    int pid = PARAMETER_B;
    for (auto& param : osc)
    {
      registerParameter(static_cast<PatchParameterId>(pid++), param.getName());
    }

    ramp.duration() << 0.1f;
    ramp.trigger();
  }

  ~VesslTestPatch() override
  {
    FloatArray::destroy(delayBuffer);
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    int bufferSize = audio.getSize();
    Array audioLeft(audio.getSamples(LEFT_CHANNEL), bufferSize);
    Array audioRight(audio.getSamples(RIGHT_CHANNEL), bufferSize);
    
    ramp.duration() << getParameterValue(PARAMETER_A);
    osc.fHz() << 60 + getParameterValue(PARAMETER_B)*4000;
    
    delayTime = getParameterValue(PARAMETER_A)*2.0f;

    delay.time() << delayTime.getValue(); // getParameterValue(PARAMETER_A)*2.0f;
    delay.feedback() << getParameterValue(PARAMETER_B);

    uint16_t eorState = OFF;
    uint16_t eorIndex = 0;
    AudioReader pmIn(audioLeft);
    AudioReader fmIn(audioRight);
    AudioWriter out(audioLeft);
    while (out)
    {
      osc.pm() << pmIn.read();
      osc.fmExp() << fmIn.read();

      out << (osc.generate() * ramp.generate()); 
      
      if (eorState == OFF)
      {
        eorState = ramp.eor() > 0;
        eorIndex = eorState == OFF ? eorIndex + 1 : eorIndex;
      }
    }
    
    delay.process(AudioReader(audioLeft), AudioWriter(audioRight));

    audioRight << audioLeft.add(audioRight).scale(0.5f);
    
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