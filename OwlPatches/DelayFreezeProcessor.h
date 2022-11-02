#pragma once
#ifndef __DELAY_FREEZE_PROCESSOR__
#define __DELAY_FREEZE_PROCESSOR__

#include "DelayProcessor.h"

class CrossFadingDelayWithFreezeProcessor : public CrossFadingDelayProcessor
{
private:
  bool freeze;

public:
  CrossFadingDelayWithFreezeProcessor(CrossFadingCircularFloatBuffer* buffer)
    : CrossFadingDelayProcessor(buffer), freeze(false)
  {

  }

  void setFreeze(bool enabled)
  {
    freeze = enabled;
  }

  void process(FloatArray input, FloatArray output) override
  {
    if (freeze)
    {
      int readSz = output.getSize();
      int readCap = ringbuffer->getReadCapacity();
      float* out = output;
      if (readSz < readCap)
      {
        ringbuffer->read(out, readSz);
      }
      else
      {
        ringbuffer->read(out, readCap);
        ringbuffer->setDelay(delay);

        out += readCap;
        ringbuffer->read(out, readSz - readCap);
      }
    }
    else
    {
      CrossFadingDelayProcessor::process(input, output);
    }
  }

  static CrossFadingDelayWithFreezeProcessor* create(size_t delay_len, size_t buffer_len)
  {
    return new CrossFadingDelayWithFreezeProcessor(CrossFadingCircularFloatBuffer::create(delay_len, buffer_len));
  }

  static void destroy(CrossFadingDelayWithFreezeProcessor* obj)
  {
    CrossFadingDelayProcessor::destroy(obj);
  }
};

#endif // __DELAY_FREEZE_PROCESSOR__
