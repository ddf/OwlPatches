#ifndef __STEREO_DELAY_PROCESSOR_H__
#define __STEREO_DELAY_PROCESSOR_H__

#include "Patch.h" // for LEFT_CHANNEL and RIGHT_CHANNEL
#include "Delay.h"
//#include "DelayProcessor.h"
//#include "DelayFreezeProcessor.h"

template<class BufferType>
class StereoDelayProcessor : public MultiSignalProcessor
{
protected:
  using DelayType = DelayProcessor<BufferType>;

  DelayType* delays[2];

public:
  StereoDelayProcessor(DelayType* leftDelay, DelayType* rightDelay)
  {
    delays[0] = leftDelay;
    delays[1] = rightDelay;
  }

  float getDelay(PatchChannelId channel = LEFT_CHANNEL) const
  {
    return delays[0]->getDelay();
  }

  void setDelay(PatchChannelId channel, float samples)
  {
    delays[channel]->setDelay(samples);
  }

  void setDelay(float leftSamples, float rightSamples)
  {
    delays[0]->setDelay(leftSamples);
    delays[1]->setDelay(rightSamples);
  }

  void process(AudioBuffer& input, AudioBuffer& output)
  {
    delays[0]->process(input.getSamples(LEFT_CHANNEL), output.getSamples(LEFT_CHANNEL));
    delays[1]->process(input.getSamples(RIGHT_CHANNEL), output.getSamples(RIGHT_CHANNEL));
  }

  static StereoDelayProcessor* create(size_t delayLength, size_t blockSize)
  {
    return new StereoDelayProcessor(DelayType::create(delayLength, blockSize), DelayType::create(delayLength, blockSize));
  }

  static void destroy(StereoDelayProcessor* obj)
  {
    DelayType::destroy(obj->delays[0]);
    DelayType::destroy(obj->delays[1]);
  }
};

template<class BufferType>
class StereoDelayWithFreezeProcessor : public StereoDelayProcessor<BufferType>
{
  using FreezeType = DelayWithFreezeProcessor<BufferType>;
  using StereoDelayProcessor<BufferType>::delays;

public:
  StereoDelayWithFreezeProcessor(FreezeType* left, FreezeType* right)
    : StereoDelayProcessor<BufferType>(left, right)
  {

  }

  void setFreeze(bool enabled)
  {
    static_cast<FreezeType*>(delays[0])->setFreeze(enabled);
    static_cast<FreezeType*>(delays[1])->setFreeze(enabled);
  }

  void setPosition(float position)
  {
    static_cast<FreezeType*>(delays[0])->setPosition(position);
    static_cast<FreezeType*>(delays[1])->setPosition(position);
  }

  void setPosition(float leftPosition, float rightPosition)
  {
    static_cast<FreezeType*>(delays[0])->setPosition(leftPosition);
    static_cast<FreezeType*>(delays[1])->setPosition(rightPosition);
  }

  float getPosition() const
  {
    return static_cast<FreezeType*>(delays[0])->getPosition();
  }

  static StereoDelayWithFreezeProcessor* create(size_t delayLen, size_t blockSize)
  {
    return new StereoDelayWithFreezeProcessor(FreezeType::create(delayLen, blockSize), FreezeType::create(delayLen, blockSize));
  }

  static void destroy(StereoDelayWithFreezeProcessor* obj)
  {
    FreezeType::destroy(static_cast<FreezeType*>(obj->delays[0]));
    FreezeType::destroy(static_cast<FreezeType*>(obj->delays[1]));
    delete obj;
  }

};

#endif // __STEREO_DELAY_PROCESSOR_H__
