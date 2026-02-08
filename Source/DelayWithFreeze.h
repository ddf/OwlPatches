#pragma once

#include "vessl/vessl.h"

template<typename T>
class DelayWithFreeze : public vessl::unitProcessor<T>
{
  using param = vessl::parameter;
  static constexpr param::desc d_e = { "freeze enabled", 'e', vessl::binary_p::type };
  using pdl = param::desclist<5>;
  static constexpr pdl p = {{ vessl::delay<T>::d_t, vessl::delay<T>::d_f, d_e, vessl::freeze<T>::d_p, vessl::freeze<T>::d_s }};
    
  struct P : vessl::plist<pdl::size>
  {
    DelayWithFreeze* outer;
    vessl::binary_p  freezeEnabled;
    
    param::list<pdl::size> get() const override
    {
      return { outer->time(), outer->feedback(), outer->freezeEnabled(), outer->freezePosition(), outer->freezeSize() };
    }
  };
  
public:
  DelayWithFreeze(vessl::array<T> buffer, float sampleRate, float delayInSeconds = 0, float feedback = 0)
    : vessl::unitProcessor<T>()
    , fader(0.95f, 0)
    , delayProc(buffer, sampleRate, delayInSeconds, feedback)
    , freezeProc(buffer, sampleRate)
  {
    params.outer = this;
  }
  
  vessl::unit::description getDescription() const override  // NOLINT(portability-template-virtual-member-function)
  {
    return { "delay with freeze", p.descs, pdl::size };
  }
  
  const vessl::list<vessl::parameter>& getParameters() const override { return params; }  // NOLINT(portability-template-virtual-member-function)

  param time() { return delayProc.time(); }
  param feedback() { return delayProc.feedback(); }
  param freezeEnabled() { return params.freezeEnabled(d_e); }
  param freezePosition() { return freezeProc.position(); }
  param freezeSize() { return freezeProc.size(); }

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

private:
  P params;
  vessl::smoother<> fader;
  vessl::delay<T> delayProc;
  vessl::freeze<T> freezeProc;
};