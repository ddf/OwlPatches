#pragma once
#ifndef __DELAY_H__
#define __DELAY_H__

#include "SignalProcessor.h"
#include "CircularBuffer.h"
#include "CrossFadingCircularBuffer.h"
#include "InterpolatingCircularBuffer.h"
#include "FractionalCircularBuffer.h"

template<class BufferType>
class DelayProcessor : SignalProcessor
{
protected:
  BufferType* buffer;
  float delaySamples;

public:
  DelayProcessor(BufferType* buffer) : buffer(buffer), delaySamples(0)
  {
    buffer->setDelay(delaySamples);
  }

  float getDelay() const { return delaySamples; }  
  void setDelay(float samples) { delaySamples = samples; }
  void clear() { buffer->clear(); }

  float process(float input) override
  {
    buffer->setDelay(delaySamples);
    buffer->write(input);
    return buffer->read();
  }

  void process(FloatArray input, FloatArray output) override
  {
    buffer->delay(input, output, input.getSize(), buffer->getDelay(), delaySamples);
    buffer->setDelay(delaySamples);
  }

  static DelayProcessor* create(size_t maxDelayLength, size_t blockSize)
  {
    return new DelayProcessor(BufferType::create(maxDelayLength));
  }

  static void destroy(DelayProcessor* obj)
  {
    BufferType::destroy(obj->buffer);
    delete obj;
  }
};

typedef DelayProcessor<CrossFadingCircularFloatBuffer> CrossFadingDelayProcessor;
typedef DelayProcessor<InterpolatingCircularFloatBuffer<LINEAR_INTERPOLATION>> InterpolatingDelayProcessor;
typedef DelayProcessor<FractionalCircularFloatBuffer> FractionalDelayProcessor;

template<>
CrossFadingDelayProcessor* CrossFadingDelayProcessor::create(size_t maxDelayLength, size_t blockSize)
{
  return new CrossFadingDelayProcessor(CrossFadingCircularFloatBuffer::create(maxDelayLength, blockSize));
}

template<class BufferType>
class DelayWithFreezeProcessor : public DelayProcessor<BufferType>
{
private:
  using DelayProcessor<BufferType>::buffer;
  using DelayProcessor<BufferType>::delaySamples;

  bool freeze;
  int freezeRead;
  int pos;
  float fade;
  float fadeInc;
  int   fadeLen;

public:
  DelayWithFreezeProcessor(BufferType* buffer)
    : DelayProcessor<BufferType>(buffer), freeze(false), pos(0), fade(0), fadeInc(0), fadeLen(0)
  {

  }

  void setFreeze(bool enabled)
  {
    freeze = enabled;
    freezeRead = 0; // buffer->getReadCapacity();
  }

  void setPosition(float samples)
  {
    pos = samples;
  }

  float getPosition() const 
  {
    return pos;
  }

  void process(FloatArray input, FloatArray output) override
  {
    if (freeze)
    {
      int readSz = output.getSize();
      float* out = output;
      while (readSz)
      {
        if (freezeRead <= 0)
        {
          buffer->setDelay(delaySamples + pos);
          freezeRead = delaySamples;
          beginFade(input.getSize());
        }
        else if (freezeRead < readSz)
        {
          buffer->read(out, freezeRead);
          processFade(out, freezeRead);
          out += freezeRead;
          readSz -= freezeRead;
          freezeRead = 0;
        }
        else
        {
          buffer->read(out, readSz);
          processFade(out, readSz);
          freezeRead -= readSz;
          readSz = 0;
        }
      }
    }
    else
    {
      DelayProcessor<BufferType>::process(input, output);
    }
  }

private:
  void beginFade(int blockSize)
  {
    fade = 0.0f;
    fadeLen = min((float)blockSize, max(delaySamples / 8, 1.0f));
    fadeInc = 1.0f / fadeLen;
  }

  void processFade(float* buffer, int len)
  {
    int remaining = freezeRead;
    while(len--)
    {
      *buffer++ = *buffer * fade;
      remaining--;
      if (fadeInc != 0)
      {
        fade += fadeInc;
        if (fade <= 0 || fade >= 1)
        {
          fadeInc = 0;
          fade = clamp(fade, 0.0f, 1.0f);
        }
      }
      else if (remaining <= fadeLen)
      {
        fadeInc = -1.0f / fadeLen;
      }
    }
  }

public:
  static DelayWithFreezeProcessor* create(size_t maxDelayLength, size_t blockSize)
  {
    return new DelayWithFreezeProcessor(BufferType::create(maxDelayLength));
  }

  static void destroy(DelayWithFreezeProcessor* obj)
  {
    DelayProcessor<BufferType>::destroy(obj);
  }
};

typedef DelayWithFreezeProcessor<CrossFadingCircularFloatBuffer> CrossFadingDelayWithFreezeProcessor;
typedef DelayWithFreezeProcessor<InterpolatingCircularFloatBuffer<LINEAR_INTERPOLATION>> InterpolatingDelayWithFreezeProcessor;
typedef DelayWithFreezeProcessor<FractionalCircularFloatBuffer> FractionalDelayWithFreezeProcessor;

template<>
CrossFadingDelayWithFreezeProcessor* CrossFadingDelayWithFreezeProcessor::create(size_t maxDelayLength, size_t blockSize)
{
  return new CrossFadingDelayWithFreezeProcessor(CrossFadingCircularFloatBuffer::create(maxDelayLength, blockSize));
}

#endif // __DELAY_H__
