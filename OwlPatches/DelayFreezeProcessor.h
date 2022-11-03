#pragma once
#ifndef __DELAY_FREEZE_PROCESSOR__
#define __DELAY_FREEZE_PROCESSOR__

#include "DelayProcessor.h"

class CrossFadingDelayWithFreezeProcessor : public CrossFadingDelayProcessor
{
private:
  bool freeze;
  int freezeRead;
  int pos;

public:
  CrossFadingDelayWithFreezeProcessor(CrossFadingCircularFloatBuffer* buffer)
    : CrossFadingDelayProcessor(buffer), freeze(false), pos(0)
  {

  }

  void setFreeze(bool enabled)
  {
    freeze = enabled;
    freezeRead = ringbuffer->getReadCapacity();
  }

  void setPosition(float samples)
  {
    pos = samples;
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
          ringbuffer->setDelay(delay+pos);
          freezeRead = delay;
        }
        else if (freezeRead < readSz)
        {
          ringbuffer->read(out, freezeRead);
          out += freezeRead;
          readSz -= freezeRead;
          freezeRead = 0;
        }
        else
        {
          ringbuffer->read(out, readSz);
          freezeRead -= readSz;
          readSz = 0;
        }
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
