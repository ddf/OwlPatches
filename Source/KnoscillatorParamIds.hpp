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
#pragma once

#include "PatchParameterIds.h"

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

#ifdef OWL_GENIUS
static constexpr KnoscillatorParameterIds knoscillatorParamIds =
{
  .inPitch = PARAMETER_H,
  .inMorph = PARAMETER_D,
  .inKnotP = PARAMETER_A,
  .inKnotQ = PARAMETER_B,
  .inKnotS = PARAMETER_C,
  .inDetuneP = PARAMETER_E,
  .inDetuneQ = PARAMETER_F,
  .inDetuneS = PARAMETER_G,

  .inRotateX = PARAMETER_AE,
  .inRotateY = PARAMETER_AF,
  .inRotateZ = PARAMETER_AG,

  .inRotateXRate = PARAMETER_AA,
  .inRotateYRate = PARAMETER_AB,
  .inRotateZRate = PARAMETER_AC,

  .inNoiseAmp = PARAMETER_AD,
  .inFMRatio = PARAMETER_AH,
  .inZoom = PARAMETER_BA,

  .outRotateX = PARAMETER_BB,
  .outRotateY = PARAMETER_BC,
  .outRotateZ = PARAMETER_BD,

  .inFreezeP = BUTTON_1,
  .inFreezeQ = BUTTON_2,

  .outRotateXGate = BUTTON_1,
  .outRotateYGate = BUTTON_2,
  .outRotateZGate = BUTTON_3
};
#else
#ifdef OWL_LICH
static constexpr KnoscillatorParameterIds knoscillatorParamIds = 
{
  .inPitch = PARAMETER_A,
  .inMorph = PARAMETER_B,
  .inKnotP = PARAMETER_C,
  .inKnotQ = PARAMETER_D,
  .inKnotS = PARAMETER_E,
  .inDetuneP = PARAMETER_AA,
  .inDetuneQ = PARAMETER_AB,
  .inDetuneS = PARAMETER_AC,

  .inRotateX = PARAMETER_AE,
  .inRotateY = PARAMETER_AF,
  .inRotateZ = PARAMETER_AG,

  .inRotateXRate = PARAMETER_C, // inKnotP
  .inRotateYRate = PARAMETER_D, // inKnotQ
  .inRotateZRate = PARAMETER_E, // inKnotS

  .inNoiseAmp = PARAMETER_AD,
  .inFMRatio = PARAMETER_H,
  .inZoom = PARAMETER_AH,

  .outRotateX = PARAMETER_F,
  .outRotateY = PARAMETER_G,
  .outRotateZ = (PatchParameterId)(-1),

  .inFreezeP = BUTTON_1,
  .inFreezeQ = BUTTON_2,
  .outRotateXGate = PUSHBUTTON,
  .outRotateYGate = PUSHBUTTON,
  .outRotateZGate = PUSHBUTTON
};
#else
static constexpr KnoscillatorParameterIds knoscillatorParamIds = {};
#endif
#endif
