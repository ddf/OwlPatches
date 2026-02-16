#pragma once

#include "CartesianFloat.h"
#include "vessl/vessl.h"

template<typename Operation>
class CartesianTransform
{
public:
  using matrix_t = vessl::matrix<vessl::analog_t>;
  
  CartesianTransform() : matrix(matrixData, 3, 3)
  {
    setIdentity();
  }

  [[nodiscard]] matrix_t getMatrix() const { return matrix; }
  
  void setIdentity() 
  {
    matrix.clear();
    for (size_t i = 0; i < 3; i++) {
      matrix.set(i,i, 1);
    }
  }

protected:
  vessl::analog_t matrixData[3 * 3];
  matrix_t matrix;

public:
  [[nodiscard]] CartesianFloat process(CartesianFloat input) const
  {
    CartesianFloat output;

    //output.x = matrix[0][0] * input.x + matrix[0][1] * input.y + matrix[0][2] * input.z;
    //output.y = matrix[1][0] * input.x + matrix[1][1] * input.y + matrix[1][2] * input.z;
    //output.z = matrix[2][0] * input.x + matrix[2][1] * input.y + matrix[2][2] * input.z;

    // this might be faster?
    matrix.multiply(input.toMatrix(), output.toMatrix());

    return output;
  }

  static Operation* create() 
  {
    return new Operation();
  }
  
  static void destroy(const Operation* transform) 
  {
    delete transform;
  }
};

class Rotation3D : public CartesianTransform<Rotation3D>
{
public:
  using CartesianTransform<Rotation3D>::CartesianTransform;

  void setEuler(float pitch, float yaw, float roll)
  {
    float cosa = vessl::math::cos(roll);
    float sina = vessl::math::sin(roll);

    float cosb = vessl::math::cos(yaw);
    float sinb = vessl::math::sin(yaw);

    float cosc = vessl::math::cos(pitch);
    float sinc = vessl::math::sin(pitch);

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
