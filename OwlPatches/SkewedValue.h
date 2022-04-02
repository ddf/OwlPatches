#include "basicmaths.h"

class SkewedFloat
{
  float value;
  float center;
  float skew;
  bool enabled;

public:
  SkewedFloat(float value) : value(value), center(value), skew(0), enabled(false) {}

  void toggleSkew()
  {
    enabled = !enabled;
  }

  void setSkew(float s)
  {
    skew = s;
  }

  void resetSkew()
  {
    center = value;
    skew = 0;
  }

  bool skewEnabled() const
  {
    return enabled;
  }

  float getLeft() const
  {
    return center - skew;
  }

  float getRight() const
  {
    return center + skew;
  }

  void update(float v)
  {
    float delta = v - value;
    if (enabled)
    {
      skew += delta;
    }
    else
    {
      center += delta;
    }
    value = v;
  }

  SkewedFloat& operator=(const float& other) 
  {
    update(other);
    return *this;
  }
};


