  
#include "Patch.h"
#include "MidiMessage.h"
#include "VoltsPerOctave.h"
#include "SineOscillator.h"
#include "SmoothValue.h"
#include "Noise.hpp"

class KnoscillatorLichPatch : public Patch 
{
private:
  VoltsPerOctave hz;
  SineOscillator* kpm;

  int midinote;
  int knotP;
  int knotQ;

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

  float phaseP;
  float phaseQ;
  float phaseZ;
  float phaseS;
  float phaseM;
  float rotateX;
  float rotateY;
  float rotateZ;
  float rotateOffX;
  float rotateOffY;
  float rotateOffZ;

  int gateHigh;

  const int noiseDim = 128;
  const float noiseStep = 4.0f / noiseDim;
  FloatArray noiseTable;

  const float TWO_PI;
  const float stepRate;
  const float rotateBaseFreq = 1.0f / 16.0f;
  const float rotateOffSmooth;
  const int   gateHighSampleLength;

  // hardware inputs and outputs
  const PatchParameterId inPitch;
  const PatchParameterId inMorph;
  const PatchParameterId inKnotP;
  const PatchParameterId inKnotQ;
  const PatchParameterId outRotateX;
  const PatchParameterId outRotateY;

  // MIDI inputs
  static const PatchParameterId inSquiggleVol = PARAMETER_AA;
  static const PatchParameterId inSquiggleFM = PARAMETER_AB;
  static const PatchParameterId inDetuneP = PARAMETER_AC;
  static const PatchParameterId inDetuneQ = PARAMETER_AD;
  static const PatchParameterId inDetuneS = PARAMETER_AE;
  static const PatchParameterId inRotateX = PARAMETER_AF;
  static const PatchParameterId inRotateY = PARAMETER_AG;
  static const PatchParameterId inRotateZ = PARAMETER_AH;
  static const PatchParameterId inNoiseAmp = PARAMETER_BA;

public:
  KnoscillatorLichPatch()
    : hz(true), midinote(0), knotP(1), knotQ(1), gateHigh(0),
    phaseP(0), phaseQ(0), phaseZ(0), phaseS(0), phaseM(0), 
    rotateX(0), rotateY(0), rotateZ(0), 
    rotateOffX(0), rotateOffY(0), rotateOffZ(0),
    inPitch(PARAMETER_A), inMorph(PARAMETER_B), inKnotP(PARAMETER_C), inKnotQ(PARAMETER_D),
    outRotateX(PARAMETER_F), outRotateY(PARAMETER_G),
    TWO_PI(M_PI*2), stepRate(TWO_PI / getSampleRate()), gateHighSampleLength(10 * getSampleRate() / 1000),
    rotateOffSmooth(4.0f * M_PI * 2 / getSampleRate())
  {
    registerParameter(inPitch, "Pitch");
    registerParameter(inMorph, "Morph");
    registerParameter(inKnotP, "Knot P");
    registerParameter(inKnotQ, "Knot Q");
    registerParameter(outRotateX, "X-Rotation>");
    registerParameter(outRotateY, "Y-Rotation>");

    setParameterValue(inPitch, 0);
    setParameterValue(inMorph, 0);
    setParameterValue(inKnotP, 2.0f / 16);
    setParameterValue(inKnotQ, 1.0f / 16);
    setParameterValue(outRotateX, 0);
    setParameterValue(outRotateY, 0);

    registerParameter(inSquiggleVol, "Squiggle Volume");
    registerParameter(inSquiggleFM, "Squiggle FM Amount");
    registerParameter(inDetuneP, "Detune P");
    registerParameter(inDetuneQ, "Detune Q");
    registerParameter(inDetuneS, "Detune S");
    registerParameter(inRotateX, "X-Rotation");
    registerParameter(inRotateY, "Y-Rotation");
    registerParameter(inRotateZ, "Z-Rotation");
    registerParameter(inNoiseAmp, "Noise");

    setParameterValue(inSquiggleVol, 0);
    setParameterValue(inSquiggleFM, 0);
    setParameterValue(inDetuneP, 0);
    setParameterValue(inDetuneQ, 0);
    setParameterValue(inDetuneS, 0);
    setParameterValue(inRotateX, 0);
    setParameterValue(inRotateY, 0);
    setParameterValue(inRotateZ, 0);
    setParameterValue(inNoiseAmp, 0);

    x1[TFOIL] = 1; x2[TFOIL] = 2; x3[TFOIL] = 3 * M_PI / 2;
    y1[TFOIL] = 1; y2[TFOIL] = 0; y3[TFOIL] = -2;
    z1[TFOIL] = 1; z2[TFOIL] = 0;

    x1[TORUS] = 2; x2[TORUS] = 0; /*sin(qt)*/ x3[TORUS] = 0;
    y1[TORUS] = 1; y2[TORUS] = 0; y3[TORUS] = 0; /*cos(qt)*/
    z1[TORUS] = 0; z2[TORUS] = 1;

    x1[LISSA] = 0; x2[LISSA] = 2; x3[LISSA] = TWO_PI;
    y1[LISSA] = 2; y2[LISSA] = M_PI * 3; y3[LISSA] = 0;
    z1[LISSA] = 0; z2[LISSA] = 1;

    kpm = SineOscillator::create(getSampleRate());
    kpm->setFrequency(1.02f);

    noiseTable = FloatArray::create(noiseDim*noiseDim);
    for (int x = 0; x < noiseDim; ++x)
    {
      for (int y = 0; y < noiseDim; ++y)
      {
        int i = x * noiseDim + y;
        noiseTable[i] = perlin2d(x*noiseStep, y*noiseStep, 1, 4) * 2 - 1;
      }
    }
  }

  ~KnoscillatorLichPatch()
  {
    SineOscillator::destroy(kpm);
    FloatArray::destroy(noiseTable);
  }

  float interp(float* buffer, size_t bufferSize, float normIdx)
  {
    const float fracIdx = (bufferSize - 1) * normIdx;
    const int i = (int)fracIdx;
    const int j = (i + 1) % bufferSize;
    const float lerp = fracIdx - i;
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

  float noise(float x, float y)
  {
    int nx = (int)(fabs(x) / noiseStep) % noiseDim;
    int ny = (int)(fabs(y) / noiseStep) % noiseDim;
    int ni = nx * noiseDim + ny;
    return noiseTable[ni];
  }

  void rotate(float& x, float& y, float &z, float pitch, float yaw, float roll)
  {
    float cosa = cosf(roll);
    float sina = sinf(roll);

    float cosb = cosf(pitch);
    float sinb = sinf(pitch);

    float cosc = cosf(yaw);
    float sinc = sinf(yaw);

    float Axx = cosa * cosb;
    float Axy = cosa * sinb*sinc - sina * cosc;
    float Axz = cosa * sinb*cosc + sina * sinc;

    float Ayx = sina * cosb;
    float Ayy = sina * sinb*sinc + cosa * cosc;
    float Ayz = sina * sinb*cosc - cosa * sinc;

    float Azx = -sinb;
    float Azy = cosb * sinc;
    float Azz = cosb * cosc;

    float ix = x, iy = y, iz = z;
    x = Axx * ix + Axy * iy + Axz * iz;
    y = Ayx * ix + Ayy * iy + Ayz * iz;
    z = Azx * ix + Azy * iy + Azz * iz;
  }

  void processMidi(MidiMessage msg)
  {
    if (msg.isControlChange())
    {
      const auto id = msg.getControllerNumber() - PATCH_PARAMETER_AA;
      const PatchParameterId pid = PatchParameterId(PARAMETER_AA + id);
      const float pval = msg.getControllerValue() / 127.0f;
      switch (pid)
      {
        case inSquiggleVol: setParameterValue(inSquiggleVol, pval); break;
        case inSquiggleFM: setParameterValue(inSquiggleFM, pval); break;
        case inDetuneP: setParameterValue(inDetuneP, pval); break;
        case inDetuneQ: setParameterValue(inDetuneQ, pval); break;
        case inDetuneS: setParameterValue(inDetuneS, pval); break;
        case inRotateX: setParameterValue(inRotateX, pval); break;
        case inRotateY: setParameterValue(inRotateY, pval); break;
        case inRotateZ: setParameterValue(inRotateZ, pval); break;
        case inNoiseAmp: setParameterValue(inNoiseAmp, pval); break;
      }
    }
    else if (msg.isNoteOn())
    {
      midinote = msg.getNote() - 60;
    }
  }

  bool stepPhase(float& phase, const float step)
  {
    phase += step;
    if (phase > TWO_PI)
    {
      phase -= TWO_PI;
      return true;
    }
    return false;
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    float tune = (midinote + getParameterValue(inPitch)*64 - 64) / 12.0f;
    hz.setTune(tune);

    float morphTarget = getParameterValue(inMorph)*M_PI;
    float morphStep = (morphTarget - phaseM) / getBlockSize();

    float pRaw = 1 + getParameterValue(inKnotP) * 16;
    float pTarget = floor(pRaw);
    float pDelta = pTarget - knotP;
    float pStep = pDelta / getBlockSize();

    float qRaw = 1 + getParameterValue(inKnotQ) * 16;
    float qTarget = floor(qRaw);
    float qDelta = qTarget - knotQ;
    float qStep = qDelta / getBlockSize();

    float p = knotP;
    float q = knotQ;

    float sRaw = getParameterValue(inSquiggleVol) * 16;
    float sVol = sRaw / 100.f;
    float sFM = getParameterValue(inSquiggleFM);

    float dtp = getParameterValue(inDetuneP);
    float dtq = getParameterValue(inDetuneQ);
    float dts = getParameterValue(inDetuneS);

    float rxt = getParameterValue(inRotateX)*TWO_PI;
    float rxf = rxt == 0 ? pRaw : 0;
    float ryt = getParameterValue(inRotateY)*TWO_PI;
    float ryf = ryt == 0 ? qRaw : 0;
    float rzt = getParameterValue(inRotateZ)*TWO_PI;
    float rzf = rzt == 0 ? sRaw : 0;

    float nVol = getParameterValue(inNoiseAmp)*0.5f;

    bool freezeP = isButtonPressed(BUTTON_A);
    bool freezeQ = isButtonPressed(BUTTON_B);

    for(int s = 0; s < getBlockSize(); ++s)
    {
      float freq = hz.getFrequency(left[s]);
      // phase modulate in sync with the current frequency
      kpm->setFrequency(freq*2);
      float pm  = kpm->generate()*TWO_PI;
      float ppm = pm*right[s];
      float qpm = ppm;
      float zpm = ppm;
      float spm = pm * sFM;

      float pt = phaseP+ppm;
      float qt = phaseQ+qpm;
      float zt = phaseZ+zpm;

      x2[TORUS] = sinf(qt);
      y3[TORUS] = cosf(qt);

      phaseM += morphStep;
      float m = -0.5f*cos(phaseM) + 0.5f;

      float ox = interp(x1, KNUM, m)*sinf(qt)                       + interp(x2, KNUM, m)*cosf(pt + interp(x3, KNUM, m));
      float oy = interp(y1, KNUM, m)*cosf(qt + interp(y2, KNUM, m)) + interp(y3, KNUM, m)*cosf(pt);
      float oz = interp(z1, KNUM, m)*sinf(3 * zt)                   + interp(z2, KNUM, m)*sinf(pt);

      rotate(ox, oy, oz, rotateX+rotateOffX, rotateY+rotateOffY, rotateZ+rotateOffZ);

      float st = phaseS + spm;
      //float nx = nVol * perlin2d(fabs(ox), 0, p, 4);
      //float ny = nVol * perlin2d(0, fabs(oy), q, 4);
      float nz = nVol * noise(ox, oy);
      ox += cosf(st)*sVol + ox * nz;
      oy += sinf(st)*sVol + oy * nz;
      oz += oz * nz;

      const float camDist = 6.0f;
      float projection = 1.0f / (oz + camDist);
      left[s]  = ox * projection;
      right[s] = oy * projection;

      const float step = freq * stepRate;
      stepPhase(phaseZ, step);

      if (!freezeQ)
      {
        stepPhase(phaseQ, step * (q + dtq));
      }

      if (!freezeP)
      {
        stepPhase(phaseP, step * (p + dtp));
      }

      stepPhase(phaseS, step * 4 * (p + q + dts));

      if (gateHigh > 0)
      {
        --gateHigh;
      }

      if (stepPhase(rotateX, stepRate * rotateBaseFreq * rxf))
      {
        gateHigh = gateHighSampleLength;
      }

      if (stepPhase(rotateY, stepRate * rotateBaseFreq * ryf))
      {
        gateHigh = gateHighSampleLength;
      }

      if (stepPhase(rotateZ, stepRate * rotateBaseFreq * rzf))
      {
        gateHigh = gateHighSampleLength;
      }

      rotateOffX += (rxt - rotateOffX) * rotateOffSmooth;
      rotateOffY += (ryt - rotateOffY) * rotateOffSmooth;
      rotateOffZ += (rzt - rotateOffZ) * rotateOffSmooth;

      p += pStep;
      q += qStep;
    }

    knotP = (int)pTarget;
    knotQ = (int)qTarget;
    
    setParameterValue(outRotateX, sinf(rotateX+rotateOffX)*0.5f + 0.5f);
    setParameterValue(outRotateY, cosf(rotateY+rotateOffY)*0.5f + 0.5f);
    setButton(PUSHBUTTON, gateHigh != 0);
  }
};
