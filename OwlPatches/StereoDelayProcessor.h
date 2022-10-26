#ifndef __STEREO_DELAY_PROCESSOR_H__
#define __STEREO_DELAY_PROCESSOR_H__

#include "DelayProcessor.h"
#include "FeedbackProcessor.h"

class StereoCrossFadingDelayProcessor : public StereoFeedbackProcessor
{
public:
  StereoCrossFadingDelayProcessor(CrossFadingDelayProcessor* left, CrossFadingDelayProcessor* right, FloatArray fbl, FloatArray fbr)
    : StereoFeedbackProcessor(left, right, fbl, fbr)
  {

  }

  float getDelay() 
  {
    return static_cast<CrossFadingDelayProcessor*>(processor_left)->getDelay();
  }

  void setDelay(float samples) 
  {
    static_cast<CrossFadingDelayProcessor*>(processor_right)->setDelay(samples);
    static_cast<CrossFadingDelayProcessor*>(processor_left)->setDelay(samples);
  }

  void setDelay(float samplesLeft, float samplesRight)
  {
    static_cast<CrossFadingDelayProcessor*>(processor_left)->setDelay(samplesLeft);
    static_cast<CrossFadingDelayProcessor*>(processor_right)->setDelay(samplesRight);
  }

  void clear() 
  {
    static_cast<CrossFadingDelayProcessor*>(processor_right)->clear();
    static_cast<CrossFadingDelayProcessor*>(processor_left)->clear();
  }

  static StereoCrossFadingDelayProcessor* create(size_t delayLen, size_t blockSize)
  {
    CrossFadingDelayProcessor* left = CrossFadingDelayProcessor::create(delayLen, blockSize);
    CrossFadingDelayProcessor* right = CrossFadingDelayProcessor::create(delayLen, blockSize);
    return new StereoCrossFadingDelayProcessor(left, right, FloatArray::create(blockSize), FloatArray::create(blockSize));
  }

  static void destroy(StereoCrossFadingDelayProcessor* obj)
  {
    CrossFadingDelayProcessor::destroy(static_cast<CrossFadingDelayProcessor*>(obj->processor_left));
    CrossFadingDelayProcessor::destroy(static_cast<CrossFadingDelayProcessor*>(obj->processor_right));
    StereoFeedbackProcessor::destroy(obj);
  }
};

#endif // __STEREO_DELAY_PROCESSOR_H__
