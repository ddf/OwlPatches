#ifndef __CartesianFloatArray_h__
#define __CartesianFloatArray_h__

#include "CartesianFloat.h"

class CartesianFloatArray : public SimpleArray<CartesianFloat> {
public:
  CartesianFloatArray() {}
  CartesianFloatArray(CartesianFloat* data, size_t size) :
    SimpleArray(data, size) {}
};

#endif // __CartesianFloatArray_h__
