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
    , delayProc(buffer, sampleRate, delayInSeconds, feedback)
    , freezeProc(buffer, sampleRate)
    , fader(0.95f, 0)
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

  template<vessl::duration::mode TimeMode = vessl::duration::mode::slew>
  void process(vessl::array<T> input, vessl::array<T> output)
  {
    if (params.freezeEnabled.value)
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

private:
  P params;
  vessl::delay<T> delayProc;
  vessl::freeze<T> freezeProc;
  vessl::smoother<> fader;
};

// template<typename T>
// class StereoDelayWithFreeze : public MultiSignalProcessor
// {
//   vessl::array<T> bufferLeft, bufferRight;
//   DelayWithFreeze<T> delayProcLeft, delayProcRight;
//
// public:
//   StereoDelayWithFreeze(vessl::array<T> bufferLeft, vessl::array<T> bufferRight, float sampleRate, float delayInSeconds = 0, float feedback = 0)
//     : bufferLeft(bufferLeft), bufferRight(bufferRight)
//     , delayProcLeft(bufferLeft, sampleRate, delayInSeconds, feedback)
//     , delayProcRight(bufferRight, sampleRate, delayInSeconds, feedback)
//   {
//     
//   }
//
//   void setDelay(float left, float right)
//   {
//     delayProcLeft.time() = left;
//     delayProcLeft.freezeSize() = left;
//     delayProcRight.time() = right;
//     delayProcRight.freezeSize() = right;
//   }
//
//   void setFreeze(bool enabled)
//   {
//     delayProcLeft.freezeEnabled() = enabled;
//     delayProcRight.freezeEnabled() = enabled;
//   }
//
//   void setPosition(float position)
//   {
//     delayProcLeft.freezePosition() = position;
//     delayProcRight.freezePosition() = position;
//   }
//
//   void setPosition(float leftPosition, float rightPosition)
//   {
//     delayProcLeft.freezePosition() = leftPosition;
//     delayProcRight.freezePosition() = rightPosition;
//   }
//
//   float getPosition()
//   {
//     return delayProcLeft.freezePosition().readAnalog();
//   }
//
//   template<vessl::duration::mode TimeMode>
//   void process(AudioBuffer& input, AudioBuffer& output)
//   {
//     vessl::array<T> inL(input.getSamples(LEFT_CHANNEL), input.getSize());
//     vessl::array<T> inR(input.getSamples(RIGHT_CHANNEL), input.getSize());
//     vessl::array<T> outL(output.getSamples(LEFT_CHANNEL), output.getSize());
//     vessl::array<T> outR(output.getSamples(RIGHT_CHANNEL), output.getSize());
//     delayProcLeft.template process<TimeMode>(inL, outL);
//     delayProcRight.template process<TimeMode>(inR, outR);
//   }
//
//   void process(AudioBuffer& input, AudioBuffer& output) override
//   {
//     process<vessl::duration::mode::slew>(input, output);
//   }
//
//   static StereoDelayWithFreeze* create(vessl::size_t delayLen, vessl::size_t blockSize, float sampleRate)
//   {
//     vessl::array<T> bufferLeft(new T[delayLen], delayLen);
//     vessl::array<T> bufferRight(new T[delayLen], delayLen);
//     return new StereoDelayWithFreeze(bufferLeft, bufferRight, sampleRate);
//   }
//
//   static void destroy(StereoDelayWithFreeze* obj)
//   {
//     delete[] obj->bufferLeft.getData();
//     delete[] obj->bufferRight.getData();
//     delete obj;
//   }
// };