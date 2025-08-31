#pragma once

#include "vessl/vessl.h"

template<typename T>
class CircularTexture
{
  vessl::ring<T> buffer;
  size_t sizeX, sizeY;

public:
  CircularTexture() : buffer(nullptr, 0), sizeX(0), sizeY(0) {}
  CircularTexture(T* data, size_t dataSize,  size_t sizeX, size_t sizeY) 
    : buffer(data, dataSize), sizeX(sizeX), sizeY(sizeY)
  {
  }

  T* getData() 
  {
    return buffer.getData();
  }

  size_t getWidth() const { return sizeX; }
  size_t getHeight() const { return sizeY; }

  CircularTexture subtexture(size_t w, size_t h)
  {
    CircularTexture sub = CircularTexture(buffer.getData(), buffer.getSize(), w, h);
    // have to do this because the ring constructor initializes write index to 0.
    sub.buffer.setWriteIndex(buffer.getWriteIndex());
    return sub;
  }

  void write(const T& value)
  {
    buffer.write(value);
  }

  T read(size_t x, size_t y)
  {
    // add buffer size so we don't have to worry about negative indices
    size_t index = buffer.getWriteIndex() + buffer.getSize() - 1 - (y*sizeX + x);
    return buffer.getData()[index%buffer.getSize()];
  }

  T readBilinear(float u, float v)
  {
    float x = u * static_cast<float>(sizeX);
    size_t x1 = (size_t)x;
    size_t x2 = x1 + 1;
    float xt = x - static_cast<float>(x1);

    float y = v * static_cast<float>(sizeY);
    size_t y1 = (size_t)y;
    size_t y2 = y1 + 1;
    float yt = y - static_cast<float>(y1);

    float xv1 = vessl::easing::lerp(read(x1, y1), read(x2, y1), xt);
    float xv2 = vessl::easing::lerp(read(x1, y2), read(x2, y2), xt);
    return vessl::easing::lerp(xv1, xv2, yt);
  }
};
