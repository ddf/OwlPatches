#pragma once

#include "MonochromeScreenPatch.h"
#include "VoltsPerOctave.h"
#include "DelayWithFreeze.h"
#include "vessl/vessl.h"

// turns out one doesn't need a very large wavetable (32 samples!) to have a decent sounding sine wave at lower frequencies
using Sine = vessl::waves::sine<>;
using Wavetable = vessl::wavetable<float, 1024>;
using Oscil = vessl::oscil<Wavetable>;
using Ramp = vessl::ramp<float>;
using Delay = DelayWithFreeze<float>;
using Array = vessl::array<float>;
using AudioReader = vessl::array<float>::reader;
using AudioWriter = vessl::array<float>::writer;
using FreezeBuffer = vessl::array<float>;
using Freeze = vessl::freeze<float>;

class VesslTestPatch final : public MonochromeScreenPatch
{
  Oscil osc;
  VoltsPerOctave voct;
  Ramp ramp;

  FloatArray delayBuffer;
  Delay delay;
  SmoothFloat delayTime;

  FreezeBuffer freezeBuffer;
  Freeze freeze;
  StiffFloat freezeDelay;
  StiffFloat freezeSize;
  
public:
  VesslTestPatch() : osc(getSampleRate(), 440, Sine()), voct(true), ramp(getSampleRate(), 0, 1, 0)
  , delayBuffer(FloatArray::create(static_cast<int>(getSampleRate())*2))
  , delay(Array(delayBuffer.getData(), delayBuffer.getSize()), getSampleRate(), 0.2f)
  , freezeBuffer(delayBuffer.getData(), delayBuffer.getSize()), freeze(freezeBuffer, getSampleRate())
  , freezeDelay(getBlockSize()*4), freezeSize(getBlockSize()*4)
  {
    registerParameter(PARAMETER_A, ramp.duration().getName());
    setParameterValue(PARAMETER_A, 0.1f);

    int pid = PARAMETER_B;
    for (auto& param : osc)
    {
      registerParameter(static_cast<PatchParameterId>(pid++), param.getName());
    }

    ramp.duration() << 0.1;
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
    // need to use StiffFloat or some other way of snapping size when using duration::mode::fade
    freezeDelay = getParameterValue(PARAMETER_B)*getSampleRate();
    freezeSize = vessl::easing::lerp(getSampleRate()/256, getSampleRate(), getParameterValue(PARAMETER_C));

    //delay.time() << vessl::duration::fromSeconds(getParameterValue(PARAMETER_A)*2.0f, getSampleRate());
    // note: HAS to be analog_t, otherwise the set by pointer conversion won't work.
    delay.time() << static_cast<vessl::analog_t>(getParameterValue(PARAMETER_E)*getSampleRate());
    delay.feedback() << getParameterValue(PARAMETER_F);
    delay.freezePosition() << freezeDelay.getValue();
    delay.freezeSize() << freezeSize.getValue();
    
    freeze.position() << freezeDelay.getValue();
    freeze.size() << freezeSize.getValue();
    freeze.rate() << -1.0f + 2.0f * getParameterValue(PARAMETER_D);
    
    uint16_t eorState = OFF;
    uint16_t eorIndex = 0;
    AudioReader pmIn(audioLeft);
    AudioReader fmIn(audioRight);
    AudioWriter out(audioLeft);
    while (out)
    {
      float pm = pmIn.read();
      float fm = fmIn.read();
      osc.pm() << pm;
      osc.fmExp() << fm;

      float s = (osc.generate() * ramp.generate());
      out << s;
      
      if (eorState == OFF)
      {
        eorState = ramp.eor() > 0;
        eorIndex = eorState == OFF ? eorIndex + 1 : eorIndex;
      }
    }
    if (delay.freezeEnabled())
    {
      delay.process<vessl::duration::mode::fade>(audioLeft, audioLeft);
    }
    else
    {
      delay.process<vessl::duration::mode::fade>(audioLeft, audioRight);
      audioLeft.add(audioRight).scale(0.5f);
    }
    audioRight << audioLeft;
    
    setButton(BUTTON_1, eorState, eorIndex);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      ramp.trigger();
    }

    if (bid == BUTTON_2 && value == ON)
    {
      vessl::binary_t freezeState = !freeze.enabled().readBinary(); 
      freeze.enabled() << freezeState;
      delay.freezeEnabled() << freezeState;
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 10);
    //screen.setTextSize(2);
    // @todo this doesn't work for some reason, something is getting lost with the use of unitInit
    //screen.print(osc.name());
    screen.print("freeze: "); screen.print(freeze.enabled() ? "ON" : "OFF");
  }
};