  
#include "Patch.h"
#include "VoltsPerOctave.h"

class KnoscillatorLichPatch : public Patch 
{
private:
  StiffFloat semitone;
  StiffFloat p;
  StiffFloat q;
  VoltsPerOctave hz;

  float phaseP;
  float phaseQ;
  float phaseR;
  float phaseS;

  const float TWO_PI;
  const float oneOverSampleRate;

  const PatchParameterId inSemitones;
  const PatchParameterId inMorph;
  const PatchParameterId inKnotP;
  const PatchParameterId inKnotQ;

public:
  KnoscillatorLichPatch()
    : hz(true),
    phaseP(0), phaseQ(0), phaseR(0), phaseS(0),
    inSemitones(PARAMETER_A), inMorph(PARAMETER_B), inKnotP(PARAMETER_C), inKnotQ(PARAMETER_D),
    TWO_PI(M_PI*2), oneOverSampleRate(1.0f / getSampleRate())
  {
    registerParameter(inSemitones, "Semitone");
    registerParameter(inMorph, "Morph");
    registerParameter(inKnotP, "P");
    registerParameter(inKnotQ, "Q");

    setParameterValue(inSemitones, 0);
    setParameterValue(inMorph, 0);
    setParameterValue(inKnotP, 0.2f);
    setParameterValue(inKnotQ, 0.2f);

    semitone.delta = 0.5f;
    p.delta = 1.0f;
    q.delta = 1.0f;
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

    semitone = getParameterValue(inSemitones) * 56 - 56;
    float freq = round(semitone) / 12;
    
    p = 1 + getParameterValue(inKnotP)*16;
    q = 1 + getParameterValue(inKnotQ)*16;

    hz.setTune(freq);
    freq = hz.getFrequency(left[0]);
    float step = freq * oneOverSampleRate;

    float mt = getParameterValue(inMorph)*M_PI;
    float mi = -0.5f*cos(mt) + 0.5f;

    float x[4], y[4], z[4];
    for(int s = 0; s < left.getSize(); ++s)
    {
      float pt = phaseP * TWO_PI;
      float qt = phaseQ * TWO_PI;
      float rt = phaseR * TWO_PI;

      // stubs for rotation, not sure we'll use these
      float xp = 0, yp = 0, zp = 0;

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

      float ox = sample(x, 4, mi);
      float oy = sample(y, 4, mi);
      float oz = sample(z, 4, mi);

      left[s] = ox * oz;
      right[s] = oy * oz;

      phaseR += step;
      if (phaseR > 1) phaseR -= 1;

      phaseQ += step * q;
      if (phaseQ > 1) phaseQ -= 1;

      phaseP += step * s;
      if (phaseP > 1) phaseP -= 1;
    }
  }
};
