#ifndef __EQUAL_LOUDNESS_CURVES_H__
#define __EQUAL_LOUDNESS_CURVES_H__

#include "basicmaths.h"

// Functions are ported from https://github.com/audiojs/a-weighting
// But see also: https://en.wikipedia.org/wiki/A-weighting

namespace elc
{
  static float a(float f)
  {
    float f2 = f * f;
    return 1.2588966f * 148840000.f * f2*f2 /
      ((f2 + 424.36) * sqrtf((f2 + 11599.29f) * (f2 + 544496.41f)) * (f2 + 148840000.f));
  }

  static float b(float f) 
  {
    float f2 = f * f;
    return 1.019764760044717f * 148840000.f * f*f2 /
      ((f2 + 424.36f) * sqrtf(f2 + 25122.25f) * (f2 + 148840000.f));
  };

  static float c(float f) 
  {
    float f2 = f * f;
    return 1.0069316688518042f * 148840000.f * f2 /
      ((f2 + 424.36f) * (f2 + 148840000.f));
  };

  static float d(float f)
  {
    float f2 = f * f;
    return (f / 6.8966888496476e-5f) * sqrtf(
      (
      ((1037918.48f - f2)*(1037918.48f - f2) + 1080768.16f*f2) /
        ((9837328.f - f2)*(9837328.f - f2) + 11723776.f * f2)
        ) / ((f2 + 79919.29f) * (f2 + 1345600.f))
    );
  }
}

#endif // __EQUAL_LOUDNESS_CURVES_H__
