#ifndef __CartesianFloat_h__
#define __CartesianFloat_h__

#include "basicmaths.h"
#include "FloatMatrix.h"

/**
* A structure defining a floating point Cartesian coordinate.
*/

struct CartesianFloat {
  constexpr CartesianFloat() : x(0), y(0), z(0) {}
  constexpr CartesianFloat(const CartesianFloat& in) : x(in.x), y(in.y), z(in.z) {}
  constexpr CartesianFloat(float x, float y, float z) : x(x), y(y), z(z) {}

  float x, y, z;

  operator FloatMatrix()
  {
    return FloatMatrix(&x, 3, 1);
  }

  float getMagnitude() const
  {
    return sqrtf(x*x + y*y + z*z);
  }

  float getMagnitudeSquared() const
  {
    return x*x + y*y + z*z;
  }

  void setSpherical(float radius, float inclination, float azimuth)
  {
    x = radius * cosf(azimuth) * sinf(inclination);
    y = radius * sinf(azimuth) * sinf(inclination);
    z = radius * cosf(inclination);
  }

  CartesianFloat& operator=(const CartesianFloat& other) 
  {
    x = other.x;
    y = other.y;
    z = other.z;
    return *this;
  }

  CartesianFloat& operator+=(const CartesianFloat& other) 
  {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
  }

  friend CartesianFloat operator+(CartesianFloat lhs, const CartesianFloat& rhs)
  {
    lhs += rhs;
    return lhs;
  }

  CartesianFloat& operator-=(const CartesianFloat& other) 
  {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
  }

  friend CartesianFloat operator-(CartesianFloat lhs, const CartesianFloat& rhs)
  {
    lhs -= rhs;
    return lhs;
  }

  CartesianFloat& operator*=(const float& scalar) 
  {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }

  friend CartesianFloat operator*(CartesianFloat lhs, const float& rhs)
  {
    lhs *= rhs;
    return lhs;
  }

  CartesianFloat& operator/=(const float& scalar) 
  {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
  }
};

#endif // __CartesianFloat_h__
