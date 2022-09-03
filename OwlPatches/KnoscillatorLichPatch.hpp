/**

AUTHOR:
    (c) 2021 Damien Quartz

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
    Knoscillator Lich is a stereo oscillator that oscillates over a 3D curve (or Knot).
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

class KnoscillatorLichPatch : public Patch 
{
private:
  VoltsPerOctave hz;
  SineOscillator* kpm;
  
  KnotOscillator* knoscil;
  Rotation3D* rotator;

  int midinote;
  int knotP;
  int knotQ;

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
    phaseS(0), phaseM(0), 
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
  }

  ~KnoscillatorLichPatch()
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

    for (int s = 0; s < getBlockSize(); ++s)
    {
      const float freq = hz.getFrequency(left[s]);
      // phase modulate in sync with the current frequency
      kpm->setFrequency(freq * 2);
      const float fm = kpm->generate()*TWO_PI*right[s];

      knoscil->setFrequency(freq);
      knoscil->setPQ(freezeP ? 0 : p + dtp, freezeQ ? 0 : q + dtq);
      knoscil->setMorph(phaseM);

      CartesianFloat coord = knoscil->generate(fm);
      rotator->setEuler(rotateX + rotateOffX, rotateY + rotateOffY, rotateZ + rotateOffZ);
      coord = rotator->process(coord);

      float st = phaseS + (fm*sFM);
      float nz = nVol * noise(coord.x, coord.y);
      coord.x += cosf(st)*sVol + coord.x * nz;
      coord.y += sinf(st)*sVol + coord.y * nz;
      coord.z += coord.z * nz;

      const float camDist = 6.0f;
      float projection = 1.0f / (coord.z + camDist);
      left[s] = coord.x * projection;
      right[s] = coord.y * projection;

      phaseM += morphStep;

      const float step = freq * stepRate;
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
