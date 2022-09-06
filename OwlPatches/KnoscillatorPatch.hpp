/**
AUTHOR:
    (c) 2022 Damien Quartz

LICENSE:
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


DESCRIPTION:
    Knoscillator is a stereo oscillator that oscillates over a 3D curve (or Knot).
    The Knot can morphed between three Knot equations based on the Trefoil Knot,
    Lissajous Curve, and Torus Knot. Each 3D sample is projected to a 2D point
    whose X-Y coordinates are used as the left and right audio outputs. By plotting
    the audio on a scope in X-Y mode, you will be able to see the Knot generating the sound.
    The Knot shape can be changed by adjusting the P and Q coefficients
    and it rotates around the X and Y axes at speeds relative to P and Q,
    which generates an ever-changing stereo field.

*/

#include "Patch.h"
#include "MidiMessage.h"
#include "VoltsPerOctave.h"
#include "KnotOscillator.h"
#include "SineOscillator.h"
#include "CartesianTransform.h"
#include "SmoothValue.h"
#include "Noise.hpp"

struct KnoscillatorParameterIds
{
  PatchParameterId inPitch;
  PatchParameterId inMorph;
  PatchParameterId inKnotP;
  PatchParameterId inKnotQ;
  PatchParameterId inKnotS;
  PatchParameterId inDetuneP;
  PatchParameterId inDetuneQ;
  PatchParameterId inDetuneS;
  PatchParameterId inRotateX;
  PatchParameterId inRotateY;
  PatchParameterId inRotateZ;
  PatchParameterId inRotateXRate;
  PatchParameterId inRotateYRate;
  PatchParameterId inRotateZRate;
  PatchParameterId inNoiseAmp;

  PatchParameterId outRotateX;
  PatchParameterId outRotateY;

  PatchButtonId inFreezeP;
  PatchButtonId inFreezeQ;
  PatchButtonId outRotateComplete;

};

template<typename PatchClass = Patch>
class KnoscillatorPatch : public PatchClass
{
protected:
  KnoscillatorParameterIds params;

private:
  VoltsPerOctave hz;
  SineOscillator* kpm;

  KnotOscillator* knoscil;
  Rotation3D* rotator;

  int midinote;
  int knotP;
  int knotQ;

  float phaseS;
  float morph;
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

public:
  using PatchClass::registerParameter;
  using PatchClass::getParameterValue;
  using PatchClass::setParameterValue;
  using PatchClass::getSampleRate;
  using PatchClass::isButtonPressed;
  using PatchClass::setButton;
  using PatchClass::getBlockSize;

  KnoscillatorPatch(KnoscillatorParameterIds paramIds) 
    : PatchClass()
    , params(paramIds), hz(true), midinote(0), knotP(1), knotQ(1)
    , gateHigh(0), phaseS(0), morph(0)
    , rotateX(0), rotateY(0), rotateZ(0)
    , rotateOffX(0), rotateOffY(0), rotateOffZ(0)
    , TWO_PI(M_PI * 2), stepRate(TWO_PI / getSampleRate()), gateHighSampleLength(10 * getSampleRate() / 1000)
    , rotateOffSmooth(4.0f * M_PI * 2 / getSampleRate())
  {
    knoscil = KnotOscillator::create(getSampleRate());
    rotator = Rotation3D::create();

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

    registerParameter(params.inPitch, "Pitch");
    registerParameter(params.inMorph, "Morph");
    registerParameter(params.inKnotP, "Knot P");
    registerParameter(params.inKnotQ, "Knot Q");
    registerParameter(params.outRotateX, "X-Rotation>");
    registerParameter(params.outRotateY, "Y-Rotation>");

    setParameterValue(params.inPitch, 0);
    setParameterValue(params.inMorph, 0);
    setParameterValue(params.inKnotP, 2.0f / 16);
    setParameterValue(params.inKnotQ, 1.0f / 16);
    setParameterValue(params.outRotateX, 0);
    setParameterValue(params.outRotateY, 0);

    registerParameter(params.inKnotS, "Knot S");
    registerParameter(params.inDetuneP, "Detune P");
    registerParameter(params.inDetuneQ, "Detune Q");
    registerParameter(params.inDetuneS, "Detune S");
    registerParameter(params.inRotateX, "X-Rotation");
    registerParameter(params.inRotateY, "Y-Rotation");
    registerParameter(params.inRotateZ, "Z-Rotation");
    registerParameter(params.inNoiseAmp, "Noise");

    if (params.inRotateXRate != params.inKnotP)
    {
      registerParameter(params.inRotateXRate, "X-Rot Rate");
      setParameterValue(params.inRotateXRate, 1.0f / 16);
    }

    if (params.inRotateYRate != params.inKnotQ)
    {
      registerParameter(params.inRotateYRate, "Y-Rot Rate");
      setParameterValue(params.inRotateYRate, 1.0f / 16);
    }

    if (params.inRotateZRate != params.inKnotS)
    {
      registerParameter(params.inRotateZRate, "Z-Rot Rate");
      setParameterValue(params.inRotateZRate, 0.0f);
    }

    setParameterValue(params.inKnotS, 0);
    setParameterValue(params.inDetuneP, 0);
    setParameterValue(params.inDetuneQ, 0);
    setParameterValue(params.inDetuneS, 0);
    setParameterValue(params.inRotateX, 0);
    setParameterValue(params.inRotateY, 0);
    setParameterValue(params.inRotateZ, 0);
    setParameterValue(params.inNoiseAmp, 0);
  }

  ~KnoscillatorPatch()
  {
    KnotOscillator::destroy(knoscil);
    SineOscillator::destroy(kpm);
    Rotation3D::destroy(rotator);
    FloatArray::destroy(noiseTable);
  }



  float noise(float x, float y)
  {
    int nx = (int)(fabs(x) / noiseStep) % noiseDim;
    int ny = (int)(fabs(y) / noiseStep) % noiseDim;
    int ni = nx * noiseDim + ny;
    return noiseTable[ni];
  }

  void processMidi(MidiMessage msg)
  {
    if (msg.isNoteOn())
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

    float tune = (midinote + getParameterValue(params.inPitch) * 64 - 64) / 12.0f;
    hz.setTune(tune);

    float morphTarget = getParameterValue(params.inMorph);
    float morphStep = (morphTarget - morph) / getBlockSize();

    float pRaw = 1 + getParameterValue(params.inKnotP) * 16;
    knotP = floor(pRaw);

    float qRaw = 1 + getParameterValue(params.inKnotQ) * 16;
    knotQ = floor(qRaw);

    float sVol = getParameterValue(params.inKnotS) * 0.25f;

    float dtp = getParameterValue(params.inDetuneP);
    float dtq = getParameterValue(params.inDetuneQ);
    float dts = getParameterValue(params.inDetuneS);

    float rxt = getParameterValue(params.inRotateX)*TWO_PI;
    float rxf = rxt == 0 ? getParameterValue(params.inRotateXRate)*16 : 0;
    float ryt = getParameterValue(params.inRotateY)*TWO_PI;
    float ryf = ryt == 0 ? getParameterValue(params.inRotateYRate)*16 : 0;
    float rzt = getParameterValue(params.inRotateZ)*TWO_PI;
    float rzf = rzt == 0 ? getParameterValue(params.inRotateZRate)*16 : 0;

    float nVol = getParameterValue(params.inNoiseAmp)*0.5f;

    bool freezeP = isButtonPressed(params.inFreezeP);
    bool freezeQ = isButtonPressed(params.inFreezeQ);

    for (int s = 0; s < getBlockSize(); ++s)
    {
      const float freq = hz.getFrequency(left[s]);
      // phase modulate in sync with the current frequency
      kpm->setFrequency(freq * 2);
      const float fm = kpm->generate()*TWO_PI*right[s];

      knoscil->setFrequency(freq);
      knoscil->setPQ(freezeP ? 0 : knotP, freezeQ ? 0 : knotQ);
      knoscil->setMorph(morph);

      CartesianFloat coord = knoscil->generate(fm, dtp, dtq);
      rotator->setEuler(rotateX + rotateOffX, rotateY + rotateOffY, rotateZ + rotateOffZ);
      coord = rotator->process(coord);

      float st = phaseS + fm;
      float nz = nVol * noise(coord.x, coord.y);
      coord.x += cosf(st)*sVol + coord.x * nz;
      coord.y += sinf(st)*sVol + coord.y * nz;
      coord.z += coord.z * nz;

      const float camDist = 6.0f;
      float projection = 1.0f / (coord.z + camDist);
      left[s] = coord.x * projection;
      right[s] = coord.y * projection;

      morph += morphStep;

      const float step = freq * stepRate;
      stepPhase(phaseS, step * 4 * (knotP + knotQ + dts));

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
    }

    setParameterValue(params.outRotateX, sinf(rotateX + rotateOffX)*0.5f + 0.5f);
    setParameterValue(params.outRotateY, cosf(rotateY + rotateOffY)*0.5f + 0.5f);
    setButton(params.outRotateComplete, gateHigh != 0);
  }
};
