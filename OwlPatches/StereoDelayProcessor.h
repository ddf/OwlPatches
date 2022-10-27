#ifndef __STEREO_DELAY_PROCESSOR_H__
#define __STEREO_DELAY_PROCESSOR_H__

#include "DelayProcessor.h"

class StereoCrossFadingDelayProcessor : public MultiSignalProcessor
{
private:
  CrossFadingDelayProcessor* processor_left;
  CrossFadingDelayProcessor* processor_right;

public:
  StereoCrossFadingDelayProcessor(CrossFadingDelayProcessor* left, CrossFadingDelayProcessor* right)
    : processor_left(left), processor_right(right)
  {

  }

  float getDelay() 
  {
    return processor_left->getDelay();
  }

  void setDelay(float samples) 
  {
    processor_right->setDelay(samples);
    processor_left->setDelay(samples);
  }

  void setDelay(float samplesLeft, float samplesRight)
  {
    processor_left->setDelay(samplesLeft);
    processor_right->setDelay(samplesRight);
  }

  void clear() 
  {
    processor_right->clear();
    processor_left->clear();
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    processor_left->process(input.getSamples(LEFT_CHANNEL), output.getSamples(LEFT_CHANNEL));
    processor_right->process(input.getSamples(RIGHT_CHANNEL), output.getSamples(RIGHT_CHANNEL));
  }

  static StereoCrossFadingDelayProcessor* create(size_t delayLen, size_t blockSize)
  {
    CrossFadingDelayProcessor* left = CrossFadingDelayProcessor::create(delayLen, blockSize);
    CrossFadingDelayProcessor* right = CrossFadingDelayProcessor::create(delayLen, blockSize);
    return new StereoCrossFadingDelayProcessor(left, right);
  }

  static void destroy(StereoCrossFadingDelayProcessor* obj)
  {
    CrossFadingDelayProcessor::destroy(obj->processor_left);
    CrossFadingDelayProcessor::destroy(obj->processor_right);
    delete obj;
  }

};

#endif // __STEREO_DELAY_PROCESSOR_H__
