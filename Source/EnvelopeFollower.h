#pragma once
#include "AudioBuffer.h"
#include "CircularBuffer.h"
#include "SignalProcessor.h"
#include "basicmaths.h"

class EnvelopeFollower final : public SignalProcessor
{
  typedef CircularBuffer<float> Window;
  
  Window* window;
  float delta;
  float previous;
  float current;
  
  EnvelopeFollower(const float responseInSeconds, const int windowSize, const float sampleRate)
  : delta(exp(-1.0f / (sampleRate * responseInSeconds)))
  , previous(0), current(0) 
  {
    window = Window::create(windowSize);
  }
  
public:
  static EnvelopeFollower* create(float responseInSeconds, int windowSize, float sampleRate)
  {
    return new EnvelopeFollower(responseInSeconds, windowSize, sampleRate);
  }
  
  static void destroy(const EnvelopeFollower* follower)
  {
    Window::destroy(follower->window);
    delete follower;
  }

  void process(AudioBuffer& input, FloatArray output)
  {
    output.clear();
    const int channelCount = input.getChannels();
    for (int i = 0; i < channelCount; ++i)
    {
      output.add(input.getSamples(i));
    }
    output.multiply(1.0f / static_cast<float>(channelCount));
    SignalProcessor::process(output, output);
  }
  
  float process(const float input) override
  {
    window->write(input);
    if (window->isFull())
    {
      previous = current;
      current = 0;
      size_t readCount = window->getReadCapacity();
      while (readCount--)
      {
        current *= delta;
        current += (1.0f - delta)*abs(window->read());
      }
      window->reset();
    }

    const float t = 1.0f - static_cast<float>(window->getWriteCapacity()) / static_cast<float>(window->getSize());
    return previous + (current - previous)*t;
  }
};
