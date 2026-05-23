#pragma once

#include "AudioBuffer.h"
#include "vessicle/vessl/vessl.h"

template<vessl::size_t N>
class AudioBufferReader : public vessl::source<vessl::sample::frame<float, N>>
{
  AudioBuffer* source;
  vessl::size_t readIdx;
  
public:
  explicit AudioBufferReader(AudioBuffer& sourceBuffer) : source(&sourceBuffer), readIdx(0)
  {
  }

  [[nodiscard]] bool is_empty() const override { return readIdx == static_cast<vessl::size_t>(source->getSize()); }

  vessl::sample::frame<float, N> read() override
  {
    vessl::sample::frame<float, N> frame;
    for (vessl::size_t c = 0; c < N; ++c)
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

    [[nodiscard]] bool is_empty() const override { return reader.is_empty(); }
    float read() override
    {
      return reader.read().toMono().value();
    }
  };
};

template<>
class AudioBufferReader<2> : public vessl::source<vessl::sample::type<float>::stereo>
{
  using sample_t = vessl::sample::type<float>::stereo;
  vessl::array<float>::reader left;
  vessl::array<float>::reader right;
  
public:
  explicit AudioBufferReader(AudioBuffer& sourceBuffer)
  : left(sourceBuffer.getSamples(0), sourceBuffer.getSize())
  , right(sourceBuffer.getSamples(1), sourceBuffer.getSize())
  {
    
  }

  [[nodiscard]] bool is_empty() const override { return !left.available(); }
  
  sample_t read() override
  {
    sample_t frame;
    if (!is_empty())
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

    [[nodiscard]] bool is_empty() const override { return !left.available(); }
    float read() override
    {
      return (left.read() + right.read())*0.5f;
    }
  };
};

template<vessl::size_t N>
class AudioBufferWriter : public vessl::sink<vessl::sample::frame<float, N>>
{
  using sample_t = vessl::sample::frame<float, N>;
  AudioBuffer* sink;
  vessl::size_t writeIdx;

public:
  explicit AudioBufferWriter(AudioBuffer& sourceBuffer) : sink(&sourceBuffer), writeIdx(0)
  {
  }

  void write(const sample_t& in) override
  {
    for (vessl::size_t c = 0; c < N; ++c)
    {
      sink->getSamples(static_cast<int>(c))[writeIdx] = in.samples[c];
    }
    ++writeIdx;
  }

  [[nodiscard]] bool is_full() const override { return writeIdx == static_cast<vessl::size_t>(sink->getSize()); }
};

template<>
class AudioBufferWriter<2> : public vessl::sink<vessl::sample::type<float>::stereo>
{
  using sample_t = vessl::sample::type<float>::stereo;
  vessl::array<float>::writer left;
  vessl::array<float>::writer right;
  
public:
  explicit AudioBufferWriter(AudioBuffer& targetBuffer)
  : left(targetBuffer.getSamples(0), targetBuffer.getSize())
  , right(targetBuffer.getSamples(1), targetBuffer.getSize())
  {
  }

  [[nodiscard]] bool is_full() const override { return !left.available(); }
  
  void write(const sample_t& value) override
  {
    if (!is_full())
    {
      left << value.left();
      right << value.right();
    }
  }
};