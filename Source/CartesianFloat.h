#pragma once

#include "vessl/vessl.h"

/**
* A structure defining a floating point Cartesian coordinate.
*/
struct CartesianFloat
{
  float x, y, z;
  
  constexpr CartesianFloat() : x(0), y(0), z(0) {}
  constexpr CartesianFloat(const CartesianFloat&) = default;
  constexpr CartesianFloat(float x, float y, float z) : x(x), y(y), z(z) {}
  constexpr CartesianFloat(CartesianFloat&&) = default;
  ~CartesianFloat() = default;
  
  vessl::matrix<float> toMatrix() { return vessl::matrix<float>(&x, 3, 1); }

  [[nodiscard]] float getMagnitude() const
  {
    return vessl::math::sqrt(x*x + y*y + z*z);
  }

  [[nodiscard]] float getMagnitudeSquared() const
  {
    return x*x + y*y + z*z;
  }

  void setSpherical(float radius, float inclination, float azimuth)
  {
    x = radius * vessl::math::cos(azimuth) * vessl::math::sin(inclination);
    y = radius * vessl::math::sin(azimuth) * vessl::math::sin(inclination);
    z = radius * vessl::math::cos(inclination);
  }

  CartesianFloat& operator=(const CartesianFloat& other)  = default;
  CartesianFloat& operator=(CartesianFloat&& other) = default;

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
