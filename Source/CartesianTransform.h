#ifndef __CartesianTransform_h__
#define __CartesianTransform_h__

#include "CartesianFloat.h"

template<typename Operation>
class CartesianTransform
{
public:
  CartesianTransform() : matrix(matrixData, 3, 3)
  {
    resetMatrix();
  }
  FloatMatrix getMatrix() {
    return matrix;
  }
  void resetMatrix() {
    matrix.clear();
    for (size_t i = 0; i < 3; i++) {
      matrix[i][i] = 1;
    }
  }

protected:
  float matrixData[3 * 3];
  FloatMatrix matrix;

public:
  CartesianFloat process(CartesianFloat input)
  {
    CartesianFloat output;

    //output.x = matrix[0][0] * input.x + matrix[0][1] * input.y + matrix[0][2] * input.z;
    //output.y = matrix[1][0] * input.x + matrix[1][1] * input.y + matrix[1][2] * input.z;
    //output.z = matrix[2][0] * input.x + matrix[2][1] * input.y + matrix[2][2] * input.z;

    // this might be faster?
    matrix.multiply(input, output);

    return output;
  }

  static Operation* create() {
    return new Operation();
  }
  static void destroy(Operation* transform) {
    delete transform;
  }
};

class Rotation3D : public CartesianTransform<Rotation3D>
{
public:
  using CartesianTransform<Rotation3D>::CartesianTransform;

  void setEuler(float pitch, float yaw, float roll)
  {
    float cosa = cosf(roll);
    float sina = sinf(roll);

    float cosb = cosf(yaw);
    float sinb = sinf(yaw);

    float cosc = cosf(pitch);
    float sinc = sinf(pitch);

    matrix[0][0] = cosa * cosb;
    matrix[0][1] = cosa * sinb*sinc - sina * cosc;
    matrix[0][2] = cosa * sinb*cosc + sina * sinc;

    matrix[1][0] = sina * cosb;
    matrix[1][1] = sina * sinb*sinc + cosa * cosc;
    matrix[1][2] = sina * sinb*cosc - cosa * sinc;

    matrix[2][0] = -sinb;
    matrix[2][1] = cosb * sinc;
    matrix[2][2] = cosb * cosc;
  }
};

#endif // __CartesianTransform_h__
