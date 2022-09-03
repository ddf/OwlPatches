#ifndef __CartesianFloat_h__
#define __CartesianFloat_h__

#include "basicmaths.h"

/**
* A structure defining a floating point Cartesian coordinate.
*/

struct CartesianFloat {
  constexpr CartesianFloat() : x(0), y(0), z(0) {}
  constexpr CartesianFloat(float x, float y, float z) : x(x), y(y), z(z) {}

  float x, y, z;

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
};

#endif // __CartesianFloat_h__
