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
#pragma once

#include "Patch.h"
#include "MidiMessage.h"
#include "VoltsPerOctave.h"
#include "SmoothValue.h"
#include "vessicle/Knoscillator.h"

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
  PatchParameterId inFMRatio;
  PatchParameterId inZoom;

  PatchParameterId outRotateX;
  PatchParameterId outRotateY;
  PatchParameterId outRotateZ;

  PatchButtonId inFreezeP;
  PatchButtonId inFreezeQ;
  PatchButtonId outRotateXGate;
  PatchButtonId outRotateYGate;
  PatchButtonId outRotateZGate;
};

template<typename PatchClass = Patch>
class KnoscillatorPatch : public PatchClass
{
  using KnoscilGen = Knoscillator<>;
protected:
  KnoscillatorParameterIds params;
  VoltsPerOctave hz;

private:
  KnoscilGen* knoscil;

  int midinote;
  SmoothFloat tune;
  SmoothFloat knotP;
  SmoothFloat knotQ;
  SmoothFloat morph;
  SmoothFloat fmRatio;
  
  float rotateOffX;
  float rotateOffY;
  float rotateOffZ;
  float rotateOffSmooth;

public:
  using PatchClass::registerParameter;
  using PatchClass::getParameterValue;
  using PatchClass::setParameterValue;
  using PatchClass::getSampleRate;
  using PatchClass::isButtonPressed;
  using PatchClass::setButton;
  using PatchClass::getBlockSize;

  explicit KnoscillatorPatch(const KnoscillatorParameterIds& paramIds)
    : PatchClass()
    , params(paramIds), hz(true), midinote(0), tune(0.5f, -6.0f), knotP(0.9f, 2), knotQ(0.9f, 1)
    , morph(0.9f, 0), fmRatio(0.9f, 2.0f)
    , rotateOffX(0), rotateOffY(0), rotateOffZ(0)
    , rotateOffSmooth(4.0f * vessl::math::pi<float>() * 2 / getSampleRate())
  {
    knoscil = Knoscillator<>::create(getSampleRate());

    registerParameter(params.inPitch, "Pitch");
    registerParameter(params.inMorph, "Morph");
    registerParameter(params.inKnotP, "Knot P");
    registerParameter(params.inKnotQ, "Knot Q");
    registerParameter(params.outRotateX, "X-Rot>");
    registerParameter(params.outRotateY, "Y-Rot>");
    if (params.outRotateZ != -1)
    {
      registerParameter(params.outRotateZ, "Z-Rot>");
    }

    setParameterValue(params.inPitch, 0);
    setParameterValue(params.inMorph, 0);
    setParameterValue(params.inKnotP, (knotP - 1) / 16.0f);
    setParameterValue(params.inKnotQ, (knotQ - 1) / 16.0f);
    setParameterValue(params.outRotateX, 0);
    setParameterValue(params.outRotateY, 0);

    registerParameter(params.inKnotS, "Knot S");
    registerParameter(params.inDetuneP, "Detune P");
    registerParameter(params.inDetuneQ, "Detune Q");
    registerParameter(params.inDetuneS, "Detune S");
    registerParameter(params.inRotateX, "X-Rot");
    registerParameter(params.inRotateY, "Y-Rot");
    registerParameter(params.inRotateZ, "Z-Rot");
    registerParameter(params.inNoiseAmp, "Noise");
    registerParameter(params.inFMRatio, "FM Ratio");
    registerParameter(params.inZoom, "Zoom");

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
    setParameterValue(params.inFMRatio, 0.33f);
    setParameterValue(params.inZoom, 1);
  }

  virtual ~KnoscillatorPatch()
  {
    KnoscilGen::destroy(knoscil);
  }

  void processMidi(MidiMessage msg)
  {
    if (msg.isNoteOn())
    {
      midinote = msg.getNote();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    // adjust so that midinote 60 (C4) actually generates a C4 when the pitch param is zero.
    tune = (midinote - 69 + getParameterValue(params.inPitch) * 73) / 12.0f;
    hz.setTune(tune);

    morph = getParameterValue(params.inMorph);

    float fmParam = getParameterValue(params.inFMRatio);
    if (fmParam < 0.34f)
    {
      fmRatio = fmParam < 0.32f ? 1.0f + fmParam / 0.32f : 2.0f;
    }
    else if (fmParam < 0.67f)
    {
      fmRatio = fmParam < 0.65 ? 2 + (fmParam - 0.34f) / (0.65f - 0.34f) : 3;
    }
    else
    {
      fmRatio = fmParam < 0.96f ? 3 + (fmParam - 0.67f) / (0.96f - 0.67f) : 4;
    }
    
    float zoom  = getParameterValue(params.inZoom);

    knotP = 1.0f + getParameterValue(params.inKnotP) * 16;
    knotQ = 1.0f + getParameterValue(params.inKnotQ) * 16;

    float sVol = getParameterValue(params.inKnotS);

    bool freezeP = isButtonPressed(params.inFreezeP);
    bool freezeQ = isButtonPressed(params.inFreezeQ);

    float dtp = freezeP ? -1 : getParameterValue(params.inDetuneP);
    float dtq = freezeQ ? -1 : getParameterValue(params.inDetuneQ);
    // float dts = getParameterValue(params.inDetuneS);

    float rxt = getParameterValue(params.inRotateX);
    float rxf = rxt == 0 ? getParameterValue(params.inRotateXRate)*16 : 0;
    float ryt = getParameterValue(params.inRotateY);
    float ryf = ryt == 0 ? getParameterValue(params.inRotateYRate)*16 : 0;
    float rzt = getParameterValue(params.inRotateZ);
    float rzf = rzt == 0 ? getParameterValue(params.inRotateZRate)*16 : 0;

    float nVol = getParameterValue(params.inNoiseAmp);
    
    float knotTypeSmooth = -0.5f*vessl::math::cos(morph*vessl::math::pi<float>()) + 0.5f;
    // calculate coefficients based on the morph setting
    float fracIdx = static_cast<float>(KnotOscillator::KNOT_TYPE_COUNT - 1) * knotTypeSmooth;
    int typeA = static_cast<int>(fracIdx);
    knoscil->knotTypeA() = typeA;
    knoscil->knotTypeB() = (typeA + 1) % KnotOscillator::KNOT_TYPE_COUNT;
    knoscil->knotMorph() = fracIdx - static_cast<float>(typeA);
    
    knoscil->knotP() = knotP.getValue();
    knoscil->knotQ() = knotQ.getValue();
    knoscil->knotModP() = dtp;
    knoscil->knotModQ() = dtq;
    knoscil->fmRatio() = fmRatio.getValue();
    knoscil->rotRatioX() = rxf;
    knoscil->rotRatioY() = ryf;
    knoscil->rotRatioZ() = rzf;
    knoscil->cameraZoom() = zoom;
    knoscil->squiggle() = sVol;
    knoscil->noise() = nVol;

    for (int s = 0; s < getBlockSize(); ++s)
    {
      const float freq = hz.getFrequency(left[s]);
      knoscil->frequency() = freq;
      knoscil->fmIndex() = right[s];
      knoscil->rotModX() = rotateOffX;
      knoscil->rotModY() = rotateOffY;
      knoscil->rotModZ() = rotateOffZ;
      
      KnoscilGen::SampleType frame = knoscil->generate();
      left[s] = frame.left();
      right[s] = frame.right();

      rotateOffX += (rxt - rotateOffX) * rotateOffSmooth;
      rotateOffY += (ryt - rotateOffY) * rotateOffSmooth;
      rotateOffZ += (rzt - rotateOffZ) * rotateOffSmooth;
    }
    
    float rx = knoscil->rotationX().readAnalog()*0.5f + 0.5f;
    float ry = knoscil->rotationY().readAnalog()*0.5f + 0.5f;
    float rz = knoscil->rotationZ().readAnalog()*0.5f + 0.5f;

    setParameterValue(params.outRotateX, vessl::math::constrain(rx, 0.f, 1.f));
    setParameterValue(params.outRotateY, vessl::math::constrain(ry, 0.f, 1.f));
    if (params.outRotateZ != -1)
    {
      setParameterValue(params.outRotateZ, vessl::math::constrain(rz, 0.f, 1.f));
    }
    
    uint16_t gateX = rx < 0.25f;
    uint16_t gateY = ry < 0.25f;
    uint16_t gateZ = rz < 0.25f;

    if (params.outRotateXGate == params.outRotateYGate && params.outRotateXGate == params.outRotateZGate)
    {
      setButton(params.outRotateXGate, gateX | gateY | gateZ);
    }
    else
    {
      setButton(params.outRotateXGate, gateX);
      setButton(params.outRotateYGate, gateY);
      setButton(params.outRotateZGate, gateZ);
    }
  }
};
