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
  static float expoOut(float begin, float end, float t, float d = 1.0f)
  {
    return (end-begin) * (-powf(2, -10 * t / d) + 1) + begin;
  }
}
#endif // __EASING_H__
