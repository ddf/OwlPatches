#ifndef __EASING_H__
#define __EASING_H__
// good ole robert penner's easing equations
// t is time
// b is beginning value
// c is change in value
// d is total duration
// s is sigmoid, I think.

#include "basicmaths.h"

namespace Easing
{
  typedef float (*Func)(const float);

  static float linear(const float t)
  {
    return t;
  }

  static float quadIn(const float t)
  {
    return t * t;
  }
  
  static float quadInOut(const float t)
  {
    return t < 0.5f ? 2*t*t : 1.0f - powf(-2.0f*t+2.0f, 2.0f) * 0.5f;  
  }

  static float quadOut(const float t)
  {
    return 1.0f - (1.0f - t) * (1.0f - t);
  }

  static float quadOutIn(const float t)
  {
    return t < 0.5f ? quadOut(2.0f*t) * 0.5f : 1.0f - quadOut(2.0f*t) * 0.5f;
  }
  
  static float expoOut(float begin, float end, float t, float d = 1.0f)
  {
    return (end-begin) * (-powf(2, -10 * t / d) + 1) + begin;
  }

  static float interp(const float begin, const float end, const float t, const Func func = linear)
  {
    return (end-begin) * func(t) + begin;
  }
}
#endif // __EASING_H__
