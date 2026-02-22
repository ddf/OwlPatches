#pragma once

#include "KnotOscillator.h"
#include "CartesianTransform.h"
#include "Noise.hpp"
#include "vessl/vessl.h"

template<typename T = vessl::analog_t>
class Knoscillator : public vessl::unitGenerator<vessl::frame::channels<T, 2>>
  , protected vessl::plist<22>
{
public:
  using SampleType = vessl::frame::channels<T, 2>;
  
private:
  using SineOscillator = vessl::oscil<vessl::waves::sine<>>;
  using SmoothFloat = vessl::smoother<>;
  using size_t = vessl::size_t;
  using param = vessl::parameter;
  using analog_p = vessl::analog_p;
  
  static constexpr size_t noiseDim = 128;
  static constexpr float  noiseStep = 4.0f / noiseDim;
  static constexpr float  TWO_PI = vessl::math::twoPi<float>();
  static constexpr float  rotateBaseFreq = 1.0f / 16.0f;
  static constexpr float  zoomFar = 60.0f;
  static constexpr float  zoomNear = 6.0f;
  
  using NoiseTable = vessl::wavetable<float, noiseDim*noiseDim>;

  SineOscillator kpm;
  KnotOscillator knoscil;
  Rotation3D     rotator;
  SmoothFloat    zoom;

  float stepRate;
  float phaseS;
  float rotateX;
  float rotateY;
  float rotateZ;
  
  struct
  {
    // inputs
    analog_p freqInHz;
    analog_p fmRatio;
    analog_p fmIndex;
    analog_p rotRatioX;
    analog_p rotRatioY;
    analog_p rotRatioZ;
    analog_p rotModX;
    analog_p rotModY;
    analog_p rotModZ;
    analog_p zoom;
    analog_p squiggleAmt;
    analog_p noiseAmt;
    
    // outputs
    analog_p rotationX;
    analog_p rotationY;
    analog_p rotationZ;
  } params;
  
  NoiseTable noiseTable;

public:
  explicit Knoscillator(float sampleRate)
    : kpm(sampleRate, 1.02f), knoscil(sampleRate)
    , zoom(0.9f, zoomNear)
    , stepRate(TWO_PI / sampleRate), phaseS(0), rotateX(0), rotateY(0), rotateZ(0)
  {
    knoscil.knotP() = 2;
    knoscil.knotQ() = 1;
    
    params.fmRatio.value = 2;
    params.zoom.value = 1;
    
    for (size_t x = 0; x < noiseDim; ++x)
    {
      for (size_t y = 0; y < noiseDim; ++y)
      {
        size_t i = x * noiseDim + y;
        noiseTable.set(i, perlin2d(x*noiseStep, y*noiseStep, 1, 4) * 2 - 1);
      }
    }
  }
  
  param knotTypeA() const { return knoscil.knotTypeA(); }
  param knotTypeB() const { return knoscil.knotTypeB(); }
  param knotMorph() const { return knoscil.knotMorph(); }
  param knotP() const { return knoscil.knotP(); }
  param knotQ() const { return knoscil.knotQ(); }
  param knotModP() const { return knoscil.knotModP(); }
  param knotModQ() const { return knoscil.knotModQ(); }
  
  // in Hz
  param frequency() const { return params.freqInHz({ "frequency", 'f', analog_p::type }); }
  param fmRatio() const   { return params.fmRatio({"fm ratio", 'R', analog_p::type }); }
  param fmIndex() const   { return params.fmIndex({"fm index", 'r', analog_p::type}); }
  param rotRatioX() const { return params.rotRatioX({"rotation ratio X", 'X', analog_p::type}); }
  param rotRatioY() const { return params.rotRatioY({"rotation ratio Y", 'Y', analog_p::type}); }
  param rotRatioZ() const { return params.rotRatioZ({"rotation ratio Z", 'Z', analog_p::type}); }
  param rotModX() const   { return params.rotModX({"rotation mod X", 'x', analog_p::type }); }
  param rotModY() const   { return params.rotModY({"rotation mod Y", 'y', analog_p::type }); }
  param rotModZ() const   { return params.rotModZ({"rotation mod Z", 'z', analog_p::type}); }
  param cameraZoom() const{ return params.zoom({"camera zoom", 'C', analog_p::type}); }
  param squiggle() const  { return params.squiggleAmt({"squiggle amount", 'S', analog_p::type}); }
  param noise() const     { return params.noiseAmt({"noise amount", 'N', analog_p::type}); }
  
  param rotationX() const { return params.rotationX({"rotation X", 'i', analog_p::type}); }
  param rotationY() const { return params.rotationY({"rotation Y", 'j', analog_p::type}); }
  param rotationZ() const { return params.rotationZ({"rotation Z", 'k', analog_p::type}); }

  [[nodiscard]] const parameters& getParameters() const override { return *this; }

  SampleType generate() override
  {
    SampleType out;
    zoom  = zoomFar + (zoomNear - zoomFar)*params.zoom.value;

    float sVol = params.squiggleAmt.value * 0.25f;

    float rxm = params.rotModX.value*TWO_PI;
    float rxf = params.rotRatioX.value;
    float rym = params.rotModY.value*TWO_PI;
    float ryf = params.rotRatioY.value;
    float rzm = params.rotModZ.value*TWO_PI;
    float rzf = params.rotRatioZ.value;

    float nVol = params.noiseAmt.value * 0.5f;
    
    float freq = params.freqInHz.value;
    // phase modulate in sync with the current frequency
    float fmRatio = params.fmRatio.value;
    float fmIndex = params.fmIndex.value;
    kpm.fHz() = freq * fmRatio;
    float fm = kpm.generate()*fmIndex;
    
    knoscil.frequency() = freq;
    knoscil.phaseMod()  = fm;

    CartesianFloat coord = knoscil.generate();
    rotator.setEuler(rotateX + rxm, rotateY + rym, rotateZ + rzm);
    coord = rotator.process(coord);

    float st = phaseS + fm*vessl::math::twoPi<float>();
    float nz = nVol * noise(coord.x, coord.y);
    coord.x += vessl::math::cos(st)*sVol + coord.x * nz;
    coord.y += vessl::math::sin(st)*sVol + coord.y * nz;
    coord.z += coord.z * nz;

    float projection = 1.0f / (coord.z + zoom.value);
    out.left()  = coord.x * projection;
    out.right() = coord.y * projection;

    const float step = freq * stepRate;
    float knotP = knoscil.knotP().readAnalog();
    float knotQ = knoscil.knotQ().readAnalog();
    stepPhase(phaseS, step * 4 * (knotP + knotQ));
    stepPhase(rotateX, stepRate * rotateBaseFreq * rxf);
    stepPhase(rotateY, stepRate * rotateBaseFreq * ryf);
    stepPhase(rotateZ, stepRate * rotateBaseFreq * rzf);
  
    params.rotationX.value = vessl::math::sin(rotateX + rxm);
    params.rotationY.value = vessl::math::cos(rotateY + rym);
    params.rotationZ.value = vessl::math::sin(rotateZ + rzm);
  
    return out;
  }
  
  static Knoscillator* create(float sampleRate)
  {
    return new Knoscillator(sampleRate);
  }
  
  static void destroy(const Knoscillator* knoscillator)
  {
    delete knoscillator;
  }
  
protected:
  param elementAt(vessl::size_t index) const override
  {
    param p[plsz] = {
      knotTypeA(), knotTypeB(), knotMorph(), knotP(), knotQ(), knotModP(), knotModQ(),
      frequency(), fmRatio(), fmIndex(), rotRatioX(), rotRatioY(), rotRatioZ(),
      rotModX(), rotModY(), rotModZ(), cameraZoom(), squiggle(), noise(),
      rotationX(), rotationY(), rotationZ()
    };
    return p[index];
  }
  
private:
  [[nodiscard]] float noise(float x, float y) const
  {
    size_t nx = static_cast<size_t>(vessl::math::abs(x) / noiseStep) % noiseDim;
    size_t ny = static_cast<size_t>(vessl::math::abs(y) / noiseStep) % noiseDim;
    size_t ni = nx * noiseDim + ny;
    return noiseTable.get(ni);
  }

  static bool stepPhase(float& phase, const float step)
  {
    phase += step;
    if (phase > TWO_PI)
    {
      phase -= TWO_PI;
      return true;
    }
    return false;
  }
};
