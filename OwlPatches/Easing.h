#ifndef __EASING_H__
#define __EASING_H__
// good ole robert penner's easing equations
// t is time
// b is beginning value
// c is change in value
// d is total duration
// s is sigmoid, I think.

#include "basicmaths.h"
#include "EqualLoudnessCurves.h"

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

  static float expoIn(const float t)
  {
    return t == 0.0f ? 0 : powf(2.0f, 10*t - 10);
  }
  
  static float expoOut(const float t)
  {
    return t == 1.0f ? 1.0f : 1.0f - powf(2.0f, -10*t);
  }

  static float expoInOut(const float t)
  {
    return t == 0.0f ? 0.0f
                     : t == 1.0f ? 1.0f
                                 : t < 0.5f ? powf(2, 20*t-10)*0.5f
                                            : 2.0f - powf(2, -20*t+10)*0.5f;
  }

  static float interp(const float begin, const float end, const float t, const Func func = linear)
  {
    return (end-begin) * func(t) + begin;
  }
}
#endif // __EASING_H__
