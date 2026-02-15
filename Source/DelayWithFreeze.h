#pragma once

#include "vessl/vessl.h"

template<typename T>
class DelayWithFreeze : public vessl::unitProcessor<T>, protected vessl::plist<5>
{
  using param = vessl::parameter;
public:
  DelayWithFreeze(vessl::array<T> buffer, float sampleRate, float delayInSeconds = 0, float feedback = 0)
    : vessl::unitProcessor<T>()
    , fader(0.95f, 0)
    , delayProc(buffer, sampleRate, delayInSeconds, feedback)
    , freezeProc(buffer, sampleRate)
  {
  }
  
  const parameters& getParameters() const override { return *this; }  // NOLINT(portability-template-virtual-member-function)

  param time() const { return delayProc.time(); }
  param feedback() const { return delayProc.feedback(); }
  param freezeEnabled() const { return params.freezeEnabled({ "freeze enabled", 'e', vessl::binary_p::type }); }
  param freezePosition() const { return freezeProc.position(); }
  param freezeSize() const { return freezeProc.size(); }

  T process(const T& in) override  // NOLINT(portability-template-virtual-member-function)
  {
    vessl::binary_t frozen = params.freezeEnabled.value;
    vessl::analog_t fade = fader = (frozen ? 1.0f : 0.0f);
    T s1 = frozen ? in : delayProc.process(in);
    if (!frozen)
    {
      freezeProc.getBuffer().setWriteIndex(delayProc.getBuffer().getWriteIndex());
    } 
    T s2 = fade > 0 ? freezeProc.generate() : 0.f;
    return vessl::mixing::crossfade(s1, s2, fade);
  }

  template<vessl::duration::mode TimeMode = vessl::duration::mode::slew>
  void process(vessl::array<T> input, vessl::array<T> output)
  {
    if (params.freezeEnabled.value)
    {
      freezeProc.getBuffer().setWriteIndex(delayProc.getBuffer().getWriteIndex());
      if (fader.value < 0.999f)
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
      if (fader.value > 0.001f)
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
  
protected:
  param elementAt(vessl::size_t index) const override
  {
    param p[plsz] = { time(), feedback(), freezeEnabled(), freezePosition(), freezeSize() };
    return p[index];
  }

private:
  struct
  {
    vessl::binary_p freezeEnabled;
  } params;
  vessl::smoother<> fader;
  vessl::delay<T> delayProc;
  vessl::freeze<T> freezeProc;
};