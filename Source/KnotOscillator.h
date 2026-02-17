#pragma once

#include "CartesianFloat.h"
#include "vessl/vessl.h"

class KnotOscillator : public vessl::unitGenerator<CartesianFloat>
                     , protected vessl::plist<9>
{
public:
  enum class KnotType : uint8_t
  {
    TFOIL = 0,
    LISSA = 1,
    TORUS = 2,

    COUNT = 3 // note: update interp method if more knots are added
  };
  
  static constexpr int KNOT_TYPE_COUNT = static_cast<int>(KnotType::COUNT);
  
  using param     = vessl::parameter;
  using desc      = param::desc;
  using digital_p = vessl::digital_p;
  using analog_p  = vessl::analog_p;
  using knot_p    = vessl::param<KnotType>;
  
private:
  static constexpr float TWO_PI = vessl::math::twoPi<float>();
  
  struct
  {
    knot_p knotTypeA;
    knot_p knotTypeB;
    analog_p knotMorph;
    analog_p knotP;
    analog_p knotQ;
    analog_p frequency;
    analog_p phaseMod;
    analog_p knotModP;
    analog_p knotModQ;
  } params;

  float x1[KNOT_TYPE_COUNT], x2[KNOT_TYPE_COUNT], x3[KNOT_TYPE_COUNT];
  float y1[KNOT_TYPE_COUNT], y2[KNOT_TYPE_COUNT], y3[KNOT_TYPE_COUNT];
  float z1[KNOT_TYPE_COUNT], z2[KNOT_TYPE_COUNT];
  
  float phaseP;
  float phaseQ;
  float phaseZ;
  float stepRate;

public:
  explicit KnotOscillator(float sampleRate)
    : params()
    , phaseP(0), phaseQ(0), phaseZ(0)
    , stepRate(TWO_PI / sampleRate)
  {
    params.knotP.value = 1;
    params.knotQ.value = 1;
    params.knotTypeA.value = KnotType::TFOIL;
    params.knotTypeB.value = KnotType::LISSA;
    params.frequency.value = 1;
    
    static constexpr int TFOIL = static_cast<int>(KnotType::TFOIL);
    x1[TFOIL] = 1;
    x2[TFOIL] = 2;
    x3[TFOIL] = 3 * M_PI / 2;
    y1[TFOIL] = 1;
    y2[TFOIL] = 0;
    y3[TFOIL] = -2;
    z1[TFOIL] = 1;
    z2[TFOIL] = 0;

    static constexpr int TORUS = static_cast<int>(KnotType::TORUS);
    x1[TORUS] = 2;
    x2[TORUS] = 0; /*sin(qt)*/
    x3[TORUS] = 0;
    y1[TORUS] = 1;
    y2[TORUS] = 0;
    y3[TORUS] = 0; /*cos(qt)*/
    z1[TORUS] = 0;
    z2[TORUS] = 1;

    static constexpr int LISSA = static_cast<int>(KnotType::LISSA);
    x1[LISSA] = 0;
    x2[LISSA] = 2;
    x3[LISSA] = TWO_PI;
    y1[LISSA] = 2;
    y2[LISSA] = M_PI * 3;
    y3[LISSA] = 0;
    z1[LISSA] = 0;
    z2[LISSA] = 1;
  }

  param knotTypeA() const { return params.knotTypeA({ "knot type a", 'A', knot_p::type }); }
  param knotTypeB() const { return params.knotTypeB({ "knot type b", 'B', knot_p::type }); }
  // [0,1] sets morph amount from knot type a to knot type b
  param knotMorph() const { return params.knotMorph({ "knot morph", 'm', analog_p::type }); }
  param knotP() const { return params.knotP({ "knot P", 'P', analog_p::type }); }
  param knotQ() const { return params.knotQ({ "knot Q", 'Q', analog_p::type }); }
  // frequency modulation of just the P part of the knot
  param knotModP() const { return params.knotModP({ "mod P amount", 'p', analog_p::type }); }
  // frequency modulation of just the Q part of the knot
  param knotModQ() const { return params.knotModQ({ "mod Q amount", 'q', analog_p::type }); }
  
  param frequency() const { return params.frequency({ "frequency", 'F', analog_p::type }); }
  param phaseMod() const { return params.phaseMod({ "phase mod", 'f', analog_p::type }); }

  [[nodiscard]] const parameters& getParameters() const override { return *this; }
  
  CartesianFloat generate() override
  {
    return generate<true>();
  }

  template<bool smooth_pq = true>
  CartesianFloat generate()
  {
    // calculate coefficients based on knot type and morph settings
    int i = static_cast<int>(params.knotTypeA.value);
    int j = static_cast<int>(params.knotTypeB.value);
    float lerp = vessl::math::constrain(params.knotMorph.value, 0.f, 1.f);

    float cx1 = interp(x1, i, j, lerp);
    float cx3 = interp(x3, i, j, lerp);
    float cy1 = interp(y1, i, j, lerp);
    float cy2 = interp(y2, i, j, lerp);
    float cz1 = interp(z1, i, j, lerp);
    float cz2 = interp(z2, i, j, lerp);

    float fm = params.phaseMod.value*TWO_PI;
    float kp = vessl::math::floor(params.knotP.value);
    float kq = vessl::math::floor(params.knotQ.value);

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
      const float pd = params.knotP.value - kp;
      const float qd = params.knotQ.value - kq;
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

    float phaseInc = params.frequency.value*stepRate;
    stepPhase(phaseP, phaseInc*(1+params.knotModP.value));
    stepPhase(phaseQ, phaseInc*(1+params.knotModQ.value));
    stepPhase(phaseZ, phaseInc);

    return a;
  }
  
protected:
  [[nodiscard]] param elementAt(vessl::size_t index) const override
  {
    param p[plsz] = { knotTypeA(), knotTypeB(), knotMorph(), knotP(), knotQ(), knotModP(), knotModQ(), frequency(), phaseMod() };
    return p[index];
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
