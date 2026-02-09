#pragma once

#include "CartesianFloat.h"
#include "vessl/vessl.h"

class KnotOscillator
{
  enum class KnotType : uint8_t
  {
    TFOIL = 0,
    LISSA = 1,
    TORUS = 2,

    COUNT = 3 // note: update interp method if more knots are added
  };
  
  static constexpr int KNOT_TYPE_COUNT = static_cast<int>(KnotType::COUNT);

  float x1[KNOT_TYPE_COUNT], x2[KNOT_TYPE_COUNT], x3[KNOT_TYPE_COUNT];
  float y1[KNOT_TYPE_COUNT], y2[KNOT_TYPE_COUNT], y3[KNOT_TYPE_COUNT];
  float z1[KNOT_TYPE_COUNT], z2[KNOT_TYPE_COUNT];

  float knotP;
  float knotQ;
  float phaseP;
  float phaseQ;
  float phaseZ;
  float phaseInc;
  float morph;

  static constexpr float TWO_PI = vessl::math::twoPi<float>();
  float stepRate;

public:
  explicit KnotOscillator(float sampleRate) 
    : knotP(1), knotQ(1)
    , phaseP(0), phaseQ(0), phaseZ(0), phaseInc(1)
    , morph(0)
    , stepRate(TWO_PI / sampleRate)
  {
    static constexpr int TFOIL = static_cast<int>(KnotType::TFOIL);
    x1[TFOIL] = 1; x2[TFOIL] = 2; x3[TFOIL] = 3 * M_PI / 2;
    y1[TFOIL] = 1; y2[TFOIL] = 0; y3[TFOIL] = -2;
    z1[TFOIL] = 1; z2[TFOIL] = 0;

    static constexpr int TORUS = static_cast<int>(KnotType::TORUS);
    x1[TORUS] = 2; x2[TORUS] = 0; /*sin(qt)*/ x3[TORUS] = 0;
    y1[TORUS] = 1; y2[TORUS] = 0; y3[TORUS] = 0; /*cos(qt)*/
    z1[TORUS] = 0; z2[TORUS] = 1;

    static constexpr int LISSA = static_cast<int>(KnotType::LISSA);
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
    morph = -0.5f*vessl::math::cos(amt*vessl::math::pi<float>()) + 0.5f;
  }

  template<bool smooth_pq = true>
  CartesianFloat generate(float fm, float pm, float qm)
  {
    // calculate coefficients based on the morph setting
    int knotCount = static_cast<int>(KnotType::COUNT);
    float fracIdx = static_cast<float>(knotCount - 1) * morph;
    int i = static_cast<int>(fracIdx);
    int j = (i + 1) % knotCount;
    float lerp = fracIdx - static_cast<float>(i);

    float cx1 = interp(x1, i, j, lerp);
    float cx3 = interp(x3, i, j, lerp);
    float cy1 = interp(y1, i, j, lerp);
    float cy2 = interp(y2, i, j, lerp);
    float cz1 = interp(z1, i, j, lerp);
    float cz2 = interp(z2, i, j, lerp);

    float kp = vessl::math::floor(knotP);
    float kq = vessl::math::floor(knotQ);

    // the four phases we need for sampling the curves
    // are calculated as multiples of phases running
    // at the same frequency as phaseZ (with phase modulation added).
    // this keeps the four curves properly aligned for blending.
    float phaseP1 = phaseP * kp + fm;
    float phaseQ1 = phaseQ * kq + fm;

    x2[static_cast<int>(KnotType::TORUS)] = vessl::math::sin(phaseQ1);
    y3[static_cast<int>(KnotType::TORUS)] = vessl::math::cos(phaseQ1);

    float cx2 = interp(x2, i, j, lerp);
    float cy3 = interp(y3, i, j, lerp);

    CartesianFloat a = sample(phaseP1, phaseQ1, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);

    // support fractional P and Q values by generating a curve
    // that is a bilinear interpolation of phase-sync'd curves
    // for F(P,Q), F(P+1,Q), F(P,Q+1), F(P+1,Q+1).
    if (smooth_pq)
    {
      const float pd = knotP - kp;
      const float qd = knotQ - kq;
      const float phaseP2 = phaseP * (kp + 1) + fm;
      const float phaseQ2 = phaseQ * (kq + 1) + fm;

      CartesianFloat b = sample(phaseP2, phaseQ1, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);

      x2[static_cast<int>(KnotType::TORUS)] = vessl::math::sin(phaseQ2);
      y3[static_cast<int>(KnotType::TORUS)] = vessl::math::cos(phaseQ2);

      cx2 = interp(x2, i, j, lerp);
      cy3 = interp(y3, i, j, lerp);

      CartesianFloat c = sample(phaseP1, phaseQ2, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);
      CartesianFloat d = sample(phaseP2, phaseQ2, phaseZ + fm, cx1, cx2, cx3, cy1, cy2, cy3, cz1, cz2);

      a = a + (b - a) * pd;
      b = c + (d - c) * pd;
      a = a + (b - a) * qd;
    }

    stepPhase(phaseP, phaseInc*(1 + pm));
    stepPhase(phaseQ, phaseInc*(1 + qm));
    stepPhase(phaseZ, phaseInc);

    return a;
  }

private:
  static CartesianFloat sample(const float pt, const float qt, const float zt,
    const float cx1, const float cx2, const float cx3,
    const float cy1, const float cy2, const float cy3,
    const float cz1, const float cz2)
  {
    return CartesianFloat(
      cx1 * vessl::math::sin(qt) + cx2 * vessl::math::cos(pt + cx3),
      cy1 * vessl::math::cos(qt + cy2) + cy3 * vessl::math::cos(pt),
      cz1 * vessl::math::sin(3 * zt) + cz2 * vessl::math::sin(pt)
    );
  }

  static float interp(const float* buffer, int i, int j, float lerp)
  {
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

  static void stepPhase(float& phase, const float step)
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

  static void destroy(const KnotOscillator* knoscil)
  {
    delete knoscil;
  }
};
