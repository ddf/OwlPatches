#pragma once

#include "AudioBuffer.h"
#include "vessl/vessl.h"

class AudioBufferReader : public vessl::source<vessl::frame::stereo<float>>
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
  
  vessl::frame::stereo<float> read() override
  {
    vessl::frame::stereo<float> frame;
    if (!isEmpty())
    {
      frame.left() = left.read();
      frame.right() =right.read();
    }
    return frame;
  }
};

class AudioBufferWriter : public vessl::sink<vessl::frame::stereo<float>>
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
  
  void write(const vessl::frame::stereo<float>& value) override
  {
    if (!isFull())
    {
      left << value.left();
      right << value.right();
    }
  }
};