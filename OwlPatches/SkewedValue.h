#include "basicmaths.h"

class SkewedFloat
{
  float value;
  float skew;
  bool enabled;

public:
  SkewedFloat() : value(0), skew(0), enabled(false) {}

  void toggleSkew()
  {
    enabled = !enabled;
  }

  bool skewEnabled() const
  {
    return enabled;
  }

  void setValue(float v)
  {
    value = v;
  }

  float getValue() const
  {
    return value;
  }

  void setSkew(float v)
  {
    skew = v;
  }

  float getSkew() const 
  {
    return skew;
  }

  float getMin() const
  {
    return getValue() - getSkew();
  }

  float getMax() const
  {
    return getValue() + getSkew();
  }

  void update(float delta)
  {
    if (enabled)
    {
      skew += delta;
    }
    else
    {
      value += delta;
    }
  }

  SkewedFloat& operator+=(const float& other) 
  {
    update(value + other);
    return *this;
  }
};


