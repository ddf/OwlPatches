  
#include "Patch.h"
#include "VoltsPerOctave.h"
#include "RampOscillator.h"

class KnoscillatorLichPatch : public Patch 
{
private:
  VoltsPerOctave hz;
  int knotP;
  int knotQ;

  float phaseP;
  float phaseQ;
  float phaseR;
  float phaseS;
  float phaseM;
  float phaseX;
  float phaseY;

  const float TWO_PI;
  const float oneOverSampleRate;
  const float rotateBaseFreq = 1.0f / 128.0f;

  const PatchParameterId inPitch;
  const PatchParameterId inMorph;
  const PatchParameterId inKnotP;
  const PatchParameterId inKnotQ;

public:
  KnoscillatorLichPatch()
    : hz(true), knotP(1), knotQ(1),
    phaseP(0), phaseQ(0), phaseR(0), phaseS(0), phaseM(0), phaseX(0), phaseY(0),
    inPitch(PARAMETER_A), inMorph(PARAMETER_B), inKnotP(PARAMETER_C), inKnotQ(PARAMETER_D),
    TWO_PI(M_PI*2), oneOverSampleRate(1.0f / getSampleRate())
  {
    registerParameter(inPitch, "Pitch");
    registerParameter(inMorph, "Morph");
    registerParameter(inKnotP, "P");
    registerParameter(inKnotQ, "Q");

    setParameterValue(inPitch, 0);
    setParameterValue(inMorph, 0);
    setParameterValue(inKnotP, 0.2f);
    setParameterValue(inKnotQ, 0.2f);
  }

  ~KnoscillatorLichPatch()
  {
  }

  float sample(float* buffer, size_t bufferSize, float normIdx)
  {
    const float fracIdx = (bufferSize - 1) * normIdx;
    const int i = (int)fracIdx;
    const int j = (i + 1) % bufferSize;
    const float lerp = fracIdx - i;
    return buffer[i] + lerp * (buffer[j] - buffer[i]);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    float freq = (getParameterValue(inPitch)*64 - 64) / 12.0f;
    hz.setTune(freq);

    float morphTarget = getParameterValue(inMorph)*M_PI;
    float morphStep = (morphTarget - phaseM) / getBlockSize();

    float pRaw = getParameterValue(inKnotP) * 16;
    float pTarget = floor(pRaw);
    float pDelta = pTarget - knotP;
    float pStep = pDelta / getBlockSize();

    float qRaw = getParameterValue(inKnotQ) * 16;
    float qTarget = floor(qRaw);
    float qDelta = qTarget - knotQ;
    float qStep = qDelta / getBlockSize();

    float p = knotP;
    float q = knotQ;

    float x[4], y[4], z[4];
    for(int s = 0; s < getBlockSize(); ++s)
    {
      freq = hz.getFrequency(left[s]);

      float pt = phaseP * TWO_PI;
      float qt = phaseQ * TWO_PI;
      float rt = phaseR * TWO_PI;

      float xp = phaseX * TWO_PI;
      float yp = phaseY * TWO_PI;
      float zp = 0;

      // trefoil knot
      x[0] = sin(qt + xp) + 2 * sin(pt + xp);
      y[0] = cos(qt + yp) - 2 * cos(pt + yp);
      z[0] = 0.25f * sin(3 * rt + zp);
      // torus knot
      x[1] = cos(qt + xp) * (2.5f + cos(pt + xp));
      y[1] = sin(qt + yp) * (2.5f + cos(pt + yp));
      z[1] = 0.25f * sin(pt + zp);
      // lissajous knot
      x[2] = cos(qt + xp);
      y[2] = cos(pt + yp);
      z[2] = 0.5f*cos(rt + zp);
      // trefoil again for wraparound
      x[3] = -x[0];
      y[3] = -y[0];
      z[3] = z[0];

      phaseM += morphStep;
      float mi = -0.5f*cos(phaseM) + 0.5f;

      float ox = sample(x, 4, mi);
      float oy = sample(y, 4, mi);
      float oz = sample(z, 4, mi);

      left[s] = ox * oz;
      right[s] = oy * oz;

      float step = freq * oneOverSampleRate;
      phaseR += step;
      if (phaseR > 1) phaseR -= 1;

      phaseQ += step * q + step;
      if (phaseQ > 1) phaseQ -= 1;

      phaseP += step * p;
      if (phaseP > 1) phaseP -= 1;

      phaseX += oneOverSampleRate*rotateBaseFreq*pRaw*freq;
      if (phaseX > 1) phaseX -= 1;

      phaseY += oneOverSampleRate*rotateBaseFreq*qRaw*freq;
      if (phaseY > 1) phaseY -= 1;

      p += pStep;
      q += qStep;
    }

    knotP = (int)pTarget;
    knotQ = (int)qTarget;
  }
};
