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

#include "KnoscillatorPatch.hpp"
#include "MonochromeScreenPatch.h"
#include "CircularBuffer.h"

typedef KnoscillatorPatch<MonochromeScreenPatch> BasePatch;

static const KnoscillatorParameterIds knoscillatorGeniusParams =
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

  .outRotateX = PARAMETER_F,
  .outRotateY = PARAMETER_G,

  .inFreezeP = BUTTON_1,
  .inFreezeQ = BUTTON_2,
  .outRotateComplete = PUSHBUTTON
};

class KnoscillatorGeniusPatch : public BasePatch
{
  CircularFloatBuffer* left;
  CircularFloatBuffer* right;

public:
  KnoscillatorGeniusPatch()
    : BasePatch(knoscillatorGeniusParams)
  {
    left = CircularFloatBuffer::create(getBlockSize() * 2);
    right = CircularFloatBuffer::create(getBlockSize() * 2);
  }
  
  ~KnoscillatorGeniusPatch()
  {
    CircularFloatBuffer::destroy(left);
    CircularFloatBuffer::destroy(right);
  }

  void processAudio(AudioBuffer& audio) override
  {
    BasePatch::processAudio(audio);
    left->write(audio.getSamples(0), audio.getSize());
    right->write(audio.getSamples(1), audio.getSize());
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int displayHeight = screen.getHeight() - 18;
    const int cy = displayHeight / 2;
    const int cx = screen.getWidth() / 2;
    const int sz = displayHeight / 2;

    int count = min(left->getReadCapacity(), (size_t)getBlockSize());
    while (count--)
    {
      int x = cx + left->read()*sz;
      int y = cy + right->read()*sz;
      screen.setPixel(x, y, WHITE);
    }
  }
};
