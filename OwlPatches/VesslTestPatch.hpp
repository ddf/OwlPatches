#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "vessl/vessl.h"

// turns out one doesn't need a very large wavetable (32 samples!) to have a decent sounding sine wave at lower frequencies
using Sine = vessl::waves::sine<float>;
using Wavetable = vessl::wavetable<float, 1024>;
using Oscil = vessl::oscil<float>;
using Ramp = vessl::ramp<float>;
using Delay = vessl::delay<float>;
using Array = vessl::array<float>;
using AudioReader = vessl::array<float>::reader;
using AudioWriter = vessl::array<float>::writer;
using CircularBuffer = vessl::ring<float>;

class VesslTestPatch final : public MonochromeScreenPatch
{
  Sine sine;
  Wavetable wave;
  Oscil osc;
  VoltsPerOctave voct;
  Ramp ramp;

  FloatArray delayBuffer;
  Delay delay;
  SmoothFloat delayTime;

  FloatArray delayLineBuffer;
  vessl::delayline<float> delayLine;
  Oscil delayOscil;
  
public:
  VesslTestPatch() : wave(sine), osc(getSampleRate(), wave), voct(true), ramp(getSampleRate(), 0, 1, 0)
  , delayBuffer(FloatArray::create(static_cast<int>(getSampleRate())*2))
  , delay(Array(delayBuffer.getData(), delayBuffer.getSize()), getSampleRate(), 0.2f)
  , delayLineBuffer(FloatArray::create(static_cast<int>(getSampleRate())))
  , delayLine(delayLineBuffer.getData(), delayLineBuffer.getSize())
  , delayOscil(getSampleRate(), delayLine, 2.f)
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
    AudioWriter lout(audioLeft);
    AudioWriter rout(audioRight);
    while (lout)
    {
      float pm = pmIn.read();
      float fm = fmIn.read();
      osc.pm() << pm;
      osc.fmExp() << fm;

      delayOscil.pm() << pm;
      delayOscil.fmExp() << fm;

      float s = (osc.generate() * ramp.generate());
      float d = delayOscil.generate();
      lout << s*0.5f + d*0.5f;

      delayLine.write(-s+d*0.5f);
      
      if (eorState == OFF)
      {
        eorState = ramp.eor() > 0;
        eorIndex = eorState == OFF ? eorIndex + 1 : eorIndex;
      }
    }

    AudioReader dry(audioLeft);
    AudioWriter wet(audioRight);
    delay.process(dry, wet);
    
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