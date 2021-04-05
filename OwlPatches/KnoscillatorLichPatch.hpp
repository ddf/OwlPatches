  
#include "Patch.h"
#include "VoltsPerOctave.h"
#include "SineOscillator.h"

class KnoscillatorLichPatch : public Patch 
{
private:
  VoltsPerOctave hz;
  SineOscillator* kpm;

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
  float phaseX;
  float phaseY;

  const float TWO_PI;
  const float oneOverSampleRate;
  const float rotateBaseFreq = 1.0f / 16.0f;

  const PatchParameterId inPitch;
  const PatchParameterId inMorph;
  const PatchParameterId inKnotP;
  const PatchParameterId inKnotQ;
  const PatchParameterId outRotateX;
  const PatchParameterId outRotateY;

public:
  KnoscillatorLichPatch()
    : hz(true), knotP(1), knotQ(1),
    phaseP(0), phaseQ(0), phaseZ(0), phaseS(0), phaseM(0), phaseX(0), phaseY(0),
    inPitch(PARAMETER_A), inMorph(PARAMETER_B), inKnotP(PARAMETER_C), inKnotQ(PARAMETER_D),
    outRotateX(PARAMETER_F), outRotateY(PARAMETER_G),
    TWO_PI(M_PI*2), oneOverSampleRate(1.0f / getSampleRate())
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
  }

  ~KnoscillatorLichPatch()
  {
    SineOscillator::destroy(kpm);
  }

  float interp(float* buffer, size_t bufferSize, float normIdx)
  {
    const float fracIdx = (bufferSize - 1) * normIdx;
    const int i = (int)fracIdx;
    const int j = (i + 1) % bufferSize;
    const float lerp = fracIdx - i;
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

  void rotate(float& x, float& y, float &z, float pitch, float yaw, float roll)
  {
    float cosa = cos(roll);
    float sina = sin(roll);

    float cosb = cos(pitch);
    float sinb = sin(pitch);

    float cosc = cos(yaw);
    float sinc = sin(yaw);

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

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    float tune = (getParameterValue(inPitch)*64 - 64) / 12.0f;
    hz.setTune(tune);

    float morphTarget = getParameterValue(inMorph)*M_PI;
    float morphStep = (morphTarget - phaseM) / getBlockSize();

    float pRaw = 1 + getParameterValue(inKnotP) * 15;
    float pTarget = floor(pRaw);
    float pDelta = pTarget - knotP;
    float pStep = pDelta / getBlockSize();

    float qRaw = 1 + getParameterValue(inKnotQ) * 15;
    float qTarget = floor(qRaw);
    float qDelta = qTarget - knotQ;
    float qStep = qDelta / getBlockSize();

    float p = knotP;
    float q = knotQ;

    bool freezeP = isButtonPressed(BUTTON_A);
    bool freezeQ = isButtonPressed(BUTTON_B);

    for(int s = 0; s < getBlockSize(); ++s)
    {
      float freq = hz.getFrequency(left[s]);
      // phase modulate in sync with the current frequency
      kpm->setFrequency(freq*2);
      float pm   = kpm->getNextSample()*right[s];

      float pt = (phaseP+pm) * TWO_PI;
      float qt = (phaseQ+pm) * TWO_PI;
      float zt = phaseZ * TWO_PI;

      float xp = phaseX * TWO_PI;
      float yp = phaseY * TWO_PI;
      float zp = 0;

      x2[TORUS] = sin(qt);
      y3[TORUS] = cos(qt);

      phaseM += morphStep;
      float m = -0.5f*cos(phaseM) + 0.5f;

      float ox = interp(x1, KNUM, m)*sin(qt)                       + interp(x2, KNUM, m)*cos(pt + interp(x3, KNUM, m));
      float oy = interp(y1, KNUM, m)*cos(qt + interp(y2, KNUM, m)) + interp(y3, KNUM, m)*cos(pt);
      float oz = interp(z1, KNUM, m)*sin(3 * zt)                   + interp(z2, KNUM, m)*sin(pt);

      rotate(ox, oy, oz, phaseX*TWO_PI, phaseY*TWO_PI, 0);

      // #TODO squiggle
      //float sqp = (phaseSq + spm.getLastValue())*TWO_PI;
      //float sqa = 0.01f * (s.getLastValue() - 1);
      //ox += sqa * cos(sqp);
      //oy += sqa * sin(sqp);

      const float camDist = 6.0f;
      float projection = 1.0f / (oz + camDist);
      left[s]  = ox * projection;
      right[s] = oy * projection;

      float step = freq * oneOverSampleRate;
      phaseZ += step;
      if (phaseZ > 1) phaseZ -= 1;

      if (!freezeQ)
      {
        phaseQ += step * q;
        if (phaseQ > 1) phaseQ -= 1;
      }

      if (!freezeP)
      {
        phaseP += step * p;
        if (phaseP > 1) phaseP -= 1;
      }

      // #TODO squiggle
      //phaseSq += stepSize * 8 * (p.getLastValue() + q.getLastValue()) * (1 + srt.getLastValue());
      //if (phaseSq > 1)
      //{
      //  phaseSq -= 1;
      //}

      phaseX += oneOverSampleRate * rotateBaseFreq * pRaw;
      if (phaseX > 1)
      {
        phaseX -= 1;
      }

      phaseY += oneOverSampleRate * rotateBaseFreq * qRaw;
      if (phaseY > 1)
      {
        phaseY -= 1;
      }

      p += pStep;
      q += qStep;
    }

    knotP = (int)pTarget;
    knotQ = (int)qTarget;
    
    setParameterValue(outRotateX, sin(phaseX*TWO_PI)*0.5f + 0.5f);
    setParameterValue(outRotateY, cos(phaseY*TWO_PI)*0.5f + 0.5f);
    setButton(PUSHBUTTON, phaseX < 0.1f || phaseY < 0.1f);
  }
};
