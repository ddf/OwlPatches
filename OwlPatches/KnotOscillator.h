#ifndef __KnotOscillator_h__
#define __KnotOscillator_h__

#include "CartesianFloat.h"
#include "basicmaths.h"

class KnotOscillator
{
  enum KnotType
  {
    TFOIL = 0,
    LISSA = 1,
    TORUS = 2,

    KNUM = 3 // note: update interp method if more knots are added
  };

  float x1[KNUM], x2[KNUM], x3[KNUM];
  float y1[KNUM], y2[KNUM], y3[KNUM];
  float z1[KNUM], z2[KNUM];

  float knotP;
  float knotQ;
  float phase;
  float phaseInc;
  float morph;

  static constexpr float TWO_PI = M_PI * 2;
  const float stepRate;

public:
  KnotOscillator(float sampleRate) 
    : stepRate(TWO_PI/sampleRate)
    , knotP(1), knotQ(1), phase(0), phaseInc(stepRate), morph(0)
  {
    x1[TFOIL] = 1; x2[TFOIL] = 2; x3[TFOIL] = 3 * M_PI / 2;
    y1[TFOIL] = 1; y2[TFOIL] = 0; y3[TFOIL] = -2;
    z1[TFOIL] = 1; z2[TFOIL] = 0;

    x1[TORUS] = 2; x2[TORUS] = 0; /*sin(qt)*/ x3[TORUS] = 0;
    y1[TORUS] = 1; y2[TORUS] = 0; y3[TORUS] = 0; /*cos(qt)*/
    z1[TORUS] = 0; z2[TORUS] = 1;

    x1[LISSA] = 0; x2[LISSA] = 2; x3[LISSA] = TWO_PI;
    y1[LISSA] = 2; y2[LISSA] = M_PI * 3; y3[LISSA] = 0;
    z1[LISSA] = 0; z2[LISSA] = 1;
  }

  void setFrequency(float freq)
  {
    phaseInc = freq * stepRate;
  }

  void setPQ(float p, float q)
  {
    knotP = p;
    knotQ = q;
  }

  void setMorph(float amt)
  {
    morph = -0.5f*cos(amt*M_PI) + 0.5f;
  }

  CartesianFloat generate(float fm)
  {
    float ppm = fm;
    float qpm = fm;
    float zpm = fm;

    float pt = phase*knotP + ppm;
    float qt = phase*knotQ + qpm;
    float zt = phase + zpm;

    x2[TORUS] = sinf(qt);
    y3[TORUS] = cosf(qt);

    const float fracIdx = (KNUM - 1) * morph;
    const int i = (int)fracIdx;
    const int j = (i + 1) % KNUM;
    const float lerp = fracIdx - i;

    CartesianFloat coord;

    coord.x = interp(x1, i, j, lerp)*sinf(qt) + interp(x2, i, j, lerp)*cosf(pt + interp(x3, i, j, lerp));
    coord.y = interp(y1, i, j, lerp)*cosf(qt + interp(y2, i, j, lerp)) + interp(y3, i, j, lerp)*cosf(pt);
    coord.z = interp(z1, i, j, lerp)*sinf(3 * zt) + interp(z2, i, j, lerp)*sinf(pt);

    phase += phaseInc;
    if (phase >= TWO_PI)
    {
      phase -= TWO_PI;
    }

    return coord;
  }

private:
  inline float interp(float* buffer, int i, int j, float lerp)
  {
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

public:
  static KnotOscillator* create(float sr)
  {
    return new KnotOscillator(sr);
  }

  static void destroy(KnotOscillator* knoscil)
  {
    delete knoscil;
  }
};

#endif // __KnotOscillator_h__
