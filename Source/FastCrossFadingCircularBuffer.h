#pragma once
#ifndef __FAST_CROSS_FADING_CIRCULAR_BUFFER_H__
#define __FAST_CROSS_FADING_CIRCULAR_BUFFER_H__

#include "CrossFadingCircularBuffer.h"

template<typename T>
class FastCrossFadingCircularBuffer : public CrossFadingCircularBuffer<T>
{
private:
  static FloatArray sharedBuffer;

public:
  FastCrossFadingCircularBuffer(T* data, size_t size) 
    : CrossFadingCircularBuffer<T>(data, size, sharedBuffer)
  {

  }

  static void init(int blockSize)
  {
    ASSERT(sharedBuffer.getSize() == 0, "FastCrossFadingCircularBuffer already initialized!");
    sharedBuffer = FloatArray::create(blockSize);
  }

  static void deinit()
  {
    ASSERT(sharedBuffer.getSize() != 0, "FastCrossFadingCircularBuffer already deinitialized!");
    FloatArray::destroy(sharedBuffer);
    sharedBuffer = FloatArray(NULL, 0);
  }

  static FastCrossFadingCircularBuffer* create(size_t len)
  {
    ASSERT(sharedBuffer.getSize() == len, "FastCrossFadingCircularBuffer has not been initialized for this length!");
    FastCrossFadingCircularBuffer* obj = new FastCrossFadingCircularBuffer(new T[len], len);
    obj->clear();
    return obj;
  }

  static void destroy(FastCrossFadingCircularBuffer* obj)
  {
    delete[] obj->data;
    delete obj;
  }
};

template<typename T>
FloatArray FastCrossFadingCircularBuffer<T>::sharedBuffer;

typedef FastCrossFadingCircularBuffer<float> FastCrossFadingCircularFloatBuffer;

#endif // __FAST_CROSS_FADING_CIRCULAR_BUFFER_H__
