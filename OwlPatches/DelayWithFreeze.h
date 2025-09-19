#pragma once

#include "vessl/vessl.h"

template<typename T>
class DelayWithFreeze : vessl::unitProcessor<T>
{
  vessl::unit::init<1> init = {
    "delay with freeze",
    {
      vessl::parameter("freeze", vessl::parameter::type::binary)
    }
  };

  vessl::delay<T> delayProc;
  vessl::freeze<T> freezeProc;
  vessl::smoother<vessl::analog_t> fader;
  
public:
  DelayWithFreeze(vessl::array<T> buffer, float sampleRate, float delayInSeconds = 0, float feedback = 0)
    : vessl::unitProcessor<T>(init, sampleRate)
    , delayProc(buffer, sampleRate, delayInSeconds, feedback)
    , freezeProc(buffer, sampleRate)
    , fader(0.95f, 0)
  {
  }

  vessl::parameter& time() { return delayProc.time(); }
  vessl::parameter& feedback() { return delayProc.feedback(); }
  vessl::parameter& freezeEnabled() { return init.params[0]; }
  vessl::parameter& freezePosition() { return freezeProc.position(); }
  vessl::parameter& freezeSize() { return freezeProc.size(); }

  T process(const T& in) override
  {
    vessl::binary_t frozen = freezeEnabled().readBinary();
    vessl::analog_t fade = fader.process(frozen ? 1.0 : 0.0);
    T s1 = frozen ? in : delayProc.process(in);
    if (!frozen)
    {
      freezeProc.getBuffer().setWriteIndex(delayProc.getBuffer().getWriteIndex());
    } 
    T s2 = fade > 0 ? freezeProc.generate() : 0.f;
    return vessl::mixing::crossfade(s1, s2, fade);
  }

  template<vessl:: duration::mode TimeMode = vessl::duration::mode::slew>
  void process(vessl::array<T> input, vessl::array<T> output)
  {
    if (freezeEnabled().readBinary())
    {
      freezeProc.getBuffer().setWriteIndex(delayProc.getBuffer().getWriteIndex());
      if (fader.value() < 0.999f)
      {
        auto r = input.getReader();
        auto w = output.getWriter();
        while (r)
        {
          w << process(r.read());
        }
      }
      else
      {
        freezeProc.template generate<TimeMode>(output);
      }
    }
    else
    {
      if (fader.value() > 0.001f)
      {
        auto r = input.getReader();
        auto w = output.getWriter();
        while (r)
        {
          w << process(r.read());
        }
      }
      else
      {
        delayProc.template process<TimeMode>(input, output);
      }
    }
  }
};