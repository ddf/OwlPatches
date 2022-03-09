#include "CircularBuffer.h"
#include "InterpolatingCircularBuffer.h"
#include "Interpolator.h"

template<typename DataType, typename IndexType = size_t>
class CircularTexture
{
  CircularBuffer<DataType, IndexType> buffer;
  IndexType sizeX, sizeY;

public:
  CircularTexture(DataType* data, IndexType dataSize,  IndexType sizeX, IndexType sizeY) 
    : buffer(data,dataSize), sizeX(sizeX), sizeY(sizeY)
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
    CircularTexture sub = CircularTexture(buffer.getData(), buffer.getSize(), w, h);
    // have to do this because the CircularBuffer constructor initializes writepos to 0.
    sub.buffer.setWriteIndex(buffer.getWriteIndex());
    return sub;
  }

  void write(DataType value)
  {
    buffer.write(value);
  }

  DataType read(IndexType x, IndexType y)
  {
    // add buffer size so we don't have to worry about negative indices
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

typedef CircularTexture<float> CircularFloatTexture;
