#include "CircularBuffer.h"

template<typename DataType, typename IndexType = size_t>
class CircularTexture
{
  CircularBuffer<DataType, IndexType> buffer;
  DataType sizeX, sizeY

public:
  CircularTexture(DataType* data, IndexType sizeX, IndexType sizeY) 
    : buffer(data,sizeX*sizeY), sizeX(sizeX), sizeY(sizeY)
  {
  }

  DataType* getData() 
  {
    return buffer.getData();
  }
};

typedef CircularTexture<float> CircularFloatTexture;
