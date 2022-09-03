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

    KNUM = 3
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
  KnotOscillator(float sampleRate) : stepRate(TWO_PI/sampleRate)
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

  void setMorph(float phaseM)
  {
    morph = -0.5f*cos(phaseM) + 0.5f;
  }

  CartesianFloat generate(float fm)
  {
    float ppm = fm;
    float qpm = fm;
    float zpm = fm;

    float pt = phaseP + ppm;
    float qt = phaseQ + qpm;
    float zt = phaseZ + zpm;

    x2[TORUS] = sinf(qt);
    y3[TORUS] = cosf(qt);

    float ox = interp(x1, KNUM, morph)*sinf(qt) + interp(x2, KNUM, morph)*cosf(pt + interp(x3, KNUM, morph));
    float oy = interp(y1, KNUM, morph)*cosf(qt + interp(y2, KNUM, morph)) + interp(y3, KNUM, morph)*cosf(pt);
    float oz = interp(z1, KNUM, morph)*sinf(3 * zt) + interp(z2, KNUM, morph)*sinf(pt);

    stepPhase(phaseZ, phaseInc);
    stepPhase(phaseQ, phaseInc*knotQ);
    stepPhase(phaseP, phaseInc*knotP);

    return CartesianFloat(ox, oy, oz);
  }

private:
  float interp(float* buffer, size_t bufferSize, float normIdx)
  {
    const float fracIdx = (bufferSize - 1) * normIdx;
    const int i = (int)fracIdx;
    const int j = (i + 1) % bufferSize;
    const float lerp = fracIdx - i;
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
