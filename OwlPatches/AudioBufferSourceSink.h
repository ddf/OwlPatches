#pragma once

#include "AudioBuffer.h"
#include "vessl/vessl.h"

template<size_t N>
class AudioBufferReader : public vessl::source<vessl::frame::channels<float, N>>
{
  AudioBuffer* source;
  size_t readIdx;
  
public:
  explicit AudioBufferReader(AudioBuffer& sourceBuffer) : source(&sourceBuffer), readIdx(0)
  {
  }
  
  bool isEmpty() const override { return readIdx == static_cast<size_t>(source->getSize()); }

  vessl::frame::channels<float, N> read() override
  {
    vessl::frame::channels<float, N> frame;
    for (size_t c = 0; c < N; ++c)
    {
      frame.samples[c] = source->getSamples(static_cast<int>(c))[readIdx];
    }
    ++readIdx;
    return frame;
  }

  class MonoReader : public vessl::source<float>
  {
    AudioBufferReader reader;
  public:
    explicit MonoReader(AudioBuffer& sourceBuffer) : reader(&sourceBuffer) {}

    bool isEmpty() const override { return reader.isEmpty(); }
    float read() override
    {
      return reader.read().toMono().value();
    }
  };
};

template<>
class AudioBufferReader<2> : public vessl::source<vessl::frame::stereo::analog_t>
{
  vessl::array<float>::reader left;
  vessl::array<float>::reader right;
  
public:
  explicit AudioBufferReader(AudioBuffer& sourceBuffer)
  : left(sourceBuffer.getSamples(0), sourceBuffer.getSize())
  , right(sourceBuffer.getSamples(1), sourceBuffer.getSize())
  {
    
  }
  
  bool isEmpty() const override { return !left.available(); }
  
  vessl::frame::stereo::analog_t read() override
  {
    vessl::frame::stereo::analog_t frame;
    if (!isEmpty())
    {
      frame.left()  = left.read();
      frame.right() = right.read();
    }
    return frame;
  }

  class MonoReader : public vessl::source<float>
  {
    vessl::array<float>::reader left;
    vessl::array<float>::reader right;
  public:
    explicit MonoReader(AudioBuffer& sourceBuffer)
    : left(sourceBuffer.getSamples(0), sourceBuffer.getSize())
    , right(sourceBuffer.getSamples(1), sourceBuffer.getSize())
    {
    
    }

    bool isEmpty() const override { return !left.available(); }
    float read() override
    {
      return (left.read() + right.read())*0.5f;
    }
  };
};

template<size_t N>
class AudioBufferWriter : public vessl::sink<vessl::frame::channels<float, N>>
{
  AudioBuffer* sink;
  size_t writeIdx;

public:
  explicit AudioBufferWriter(AudioBuffer& sourceBuffer) : sink(&sourceBuffer), writeIdx(0)
  {
  }

  void write(const vessl::frame::channels<float, N>& in) override
  {
    for (size_t c = 0; c < N; ++c)
    {
      sink->getSamples(static_cast<int>(c))[writeIdx] = in.samples[c];
    }
    ++writeIdx;
  }

  bool isFull() const override { return writeIdx == static_cast<size_t>(sink->getSize()); }
};

template<>
class AudioBufferWriter<2> : public vessl::sink<vessl::frame::stereo::analog_t>
{
  vessl::array<float>::writer left;
  vessl::array<float>::writer right;
  
public:
  explicit AudioBufferWriter(AudioBuffer& targetBuffer)
  : left(targetBuffer.getSamples(0), targetBuffer.getSize())
  , right(targetBuffer.getSamples(1), targetBuffer.getSize())
  {
  }
  
  bool isFull() const override { return !left.available(); }
  
  void write(const vessl::frame::stereo::analog_t& value) override
  {
    if (!isFull())
    {
      left << static_cast<vessl::analog_t>(value.left());
      right << static_cast<vessl::analog_t>(value.right());
    }
  }
};