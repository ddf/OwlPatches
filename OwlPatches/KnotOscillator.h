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
  float phaseP;
  float phaseQ;
  float phaseZ;
  float phaseInc;
  float morph;

  static constexpr float TWO_PI = M_PI * 2;
  const float stepRate;

public:
  KnotOscillator(float sampleRate) 
    : stepRate(TWO_PI/sampleRate)
    , knotP(1), knotQ(1), morph(0)
    , phaseP(0), phaseQ(0), phaseZ(0), phaseInc(1)
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
    morph = -0.5f*cosf(amt*M_PI) + 0.5f;
  }

  CartesianFloat generate(float fm, float pm, float qm)
  {
    // calculate coefficients based on the morph setting
    const float fracIdx = (KNUM - 1) * morph;
    const int i = (int)fracIdx;
    const int j = (i + 1) % KNUM;
    const float lerp = fracIdx - i;

    const float cx1 = interp(x1, i, j, lerp);
    const float cx3 = interp(x3, i, j, lerp);
    const float cy1 = interp(y1, i, j, lerp);
    const float cy2 = interp(y2, i, j, lerp);
    const float cz1 = interp(z1, i, j, lerp);
    const float cz2 = interp(z2, i, j, lerp);

    // support fractional P and Q values by generating a curve
    // that is a bilinear interpolation of phase-sync'd curves
    // for F(P,Q), F(P+1,Q), F(P,Q+1), F(P+1,Q+1).
    const float kp = (int)knotP;
    const float kq = (int)knotQ;
    const float pd = knotP - kp;
    const float qd = knotQ - kq;

    // the four phases we need for sampling the curves
    // are calculated as multiples of phases running
    // at the same frequency as phaseZ (with phase modulation added).
    // this keeps the four curves properly aligned for blending.
    const float phaseP1 = phaseP * kp + fm;
    const float phaseQ1 = phaseQ * kq + fm;
    const float phaseP2 = phaseP * (kp + 1) + fm;
    const float phaseQ2 = phaseQ * (kq + 1) + fm;

    x2[TORUS] = sinf(phaseQ1);
    y3[TORUS] = cosf(phaseQ1);

    float cx2 = interp(x2, i, j, lerp);
    float cy3 = interp(y3, i, j, lerp);

    CartesianFloat a = sample(phaseP1, phaseQ1, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);
    CartesianFloat b = sample(phaseP2, phaseQ1, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);

    x2[TORUS] = sinf(phaseQ2);
    y3[TORUS] = cosf(phaseQ2);

    cx2 = interp(x2, i, j, lerp);
    cy3 = interp(y3, i, j, lerp);

    CartesianFloat c = sample(phaseP1, phaseQ2, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);
    CartesianFloat d = sample(phaseP2, phaseQ2, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);

    a = a + (b - a)*pd;
    b = c + (d - c)*pd;
    a = a + (b - a)*qd;

    stepPhase(phaseP, phaseInc*(1 + pm));
    stepPhase(phaseQ, phaseInc*(1 + qm));
    stepPhase(phaseZ, phaseInc);

    return a;
  }

private:
  inline CartesianFloat sample(const float pt, const float qt, const float zt,
    const float cx1, const float cx2, const float cx3,
    const float cy1, const float cy2, const float cy3,
    const float cz1, const float cz2)
  {
    CartesianFloat a {
    .x = cx1 * sinf(qt) + cx2 * cosf(pt + cx3),
    .y = cy1 * cosf(qt + cy2) + cy3 * cosf(pt),
    .z = cz1 * sinf(3 * zt) + cz2 * sinf(pt),
    };

    return a;
  }

  inline float interp(float* buffer, int i, int j, float lerp)
  {
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

  void stepPhase(float& phase, const float step)
  {
    phase += step;
    if (phase >= TWO_PI)
    {
      phase -= TWO_PI;
    }
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
