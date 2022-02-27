#include "CircularBuffer.h"
#include "InterpolatingCircularBuffer.h"
#include "Interpolator.h"

template<typename DataType, typename IndexType = size_t>
class CircularTexture
{
  CircularBuffer<DataType, IndexType> buffer;
  IndexType sizeX, sizeY;

public:
  CircularTexture(DataType* data, IndexType sizeX, IndexType sizeY) 
    : buffer(data,sizeX*sizeY), sizeX(sizeX), sizeY(sizeY)
  {
  }

  DataType* getData() 
  {
    return buffer.getData();
  }

  IndexType getWidth() const { return sizeX; }
  IndexType getHeight() const { return sizeY; }

  CircularTexture subtexture(IndexType w, IndexType h)
  {
    CircularTexture subTex = *this;
    subTex.sizeX = w;
    subTex.sizeY = h;
    return subTex;
  }

  void write(DataType value)
  {
    buffer.write(value);
  }

  DataType read(IndexType x, IndexType y)
  {
    // add buffer size we don't have to worry about negative indices
    IndexType index = buffer.getWriteIndex() + buffer.getSize() - 1 - (y*sizeX + x);
    return buffer.readAt(index);
  }

  DataType readBilinear(float u, float v)
  {
    float x = u * sizeX;
    IndexType x1 = IndexType(x);
    IndexType x2 = x1 + 1;
    float xt = x - x1;

    float y = v * sizeY;
    IndexType y1 = IndexType(y);
    IndexType y2 = y1 + 1;
    float yt = y - y1;

    float xv1 = Interpolator::linear(read(x1, y1), read(x2, y1), xt);
    float xv2 = Interpolator::linear(read(x1, y2), read(x2, y2), xt);
    return Interpolator::linear(xv1, xv2, yt);
  }
};

template<>
class CircularTexture<float, float>
{
  InterpolatingCircularFloatBuffer<LINEAR_INTERPOLATION> buffer;
  float  sizeX;
  size_t sizeY;
  float  readOffset;

public:
  CircularTexture(float* data, float sizeX, size_t sizeY)
    : buffer(data, sizeX*sizeY), sizeX(sizeX), sizeY(sizeY)
  {
    readOffset = buffer.getSize() * 0.5f;
  }

  float* getData()
  {
    return buffer.getData();
  }

  size_t getDataSize() const
  {
    return buffer.getSize();
  }

  float getWidth() const { return sizeX; }
  size_t getHeight() const { return sizeY; }

  CircularTexture subtexture(float w, size_t h)
  {
    CircularTexture subTex = *this;
    subTex.sizeX = w;
    subTex.sizeY = h;
    return subTex;
  }

  void write(float value)
  {
    buffer.write(value);
  }

  void setReadOffset(float offset)
  {
    readOffset = offset - 1;
  }

  float read(float x, size_t y)
  {
    float index = buffer.getWriteIndex() + buffer.getSize() - readOffset + y*sizeX + x;
    return buffer.readAt(index);
  }

  float readBilinear(float u, float v)
  {
    float x = u * sizeX;

    float y = v * sizeY;
    size_t y1 = size_t(y);
    size_t y2 = y1 + 1;
    float yt = y - y1;

    float xv1 = read(x, y1);
    float xv2 = read(x, y2);
    return Interpolator::linear(xv1, xv2, yt);
  }
};

typedef CircularTexture<float> CircularFloatTexture;
