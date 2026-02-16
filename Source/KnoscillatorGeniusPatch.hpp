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
    The Knot can be morphed between three Knot equations based on the Trefoil Knot,
    Lissajous Curve, and Torus Knot. Each 3D sample is projected to a 2D point
    whose X-Y coordinates are used as the left and right audio outputs. By plotting
    the audio on a scope in X-Y mode, you will be able to see the Knot generating the sound.
    The Knot shape can be changed by adjusting the P, Q, and S coefficients.
    The Knot can be directly rotated or set to rotate at unique rates around 
    the X, Y, and Z axes, creating an ever-changing stereo field.
*/
#pragma once

#include "KnoscillatorPatch.hpp"
#include "MonochromeScreenPatch.h"
#include "CircularBuffer.h"

typedef KnoscillatorPatch<MonochromeScreenPatch> BasePatch;

static const KnoscillatorParameterIds knoscillatorGeniusParams =
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

class KnoscillatorGeniusPatch : public BasePatch
{
  size_t drawCount;
  float  knotPhase;
  CircularFloatBuffer* left;
  CircularFloatBuffer* right;
  MonochromeScreenBuffer backBuffer;

public:
  KnoscillatorGeniusPatch()
    : BasePatch(knoscillatorGeniusParams), drawCount(0), knotPhase(0)
    , backBuffer(128,64)
  {
    const size_t bufferSize = getBlockSize() * 128;
    left = CircularFloatBuffer::create(bufferSize);
    right = CircularFloatBuffer::create(bufferSize);
    backBuffer.setBuffer(new uint8_t[backBuffer.getWidth()*backBuffer.getHeight()/8]);
  }
  
  ~KnoscillatorGeniusPatch()
  {
    CircularFloatBuffer::destroy(left);
    CircularFloatBuffer::destroy(right);
    delete[] backBuffer.getBuffer();
  }

  // returns CPU% as [0,1] value
  float getElapsedTime()
  {
    return getElapsedCycles() / getBlockSize() / 10000.0f;
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();
    FloatArray inLeft = audio.getSamples(0);
    const float phaseStep = 1.0f / getSampleRate();
    for (int i = 0; i < size; ++i)
    {
      const float freq = hz.getFrequency(inLeft[i]);
      knotPhase += freq * phaseStep;
      if (knotPhase >= 1)
      {
        knotPhase -= 1;
        drawCount = left->getReadCapacity() + i;
      }
    }

    BasePatch::processAudio(audio);

    left->write(audio.getSamples(0), audio.getSize());
    right->write(audio.getSamples(1), audio.getSize());
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int displayHeight = screen.getHeight() - 18;
    const int cy = displayHeight / 2;
    const int cx = screen.getWidth() / 2;
    const int sz = screen.getHeight() / 2;

    if (drawCount)
    {
      backBuffer.clear();
      float t1 = getElapsedTime();
      size_t count = min(drawCount, left->getSize() / 2);
      drawCount -= count;
      while (count--)
      {
        int x = cx + left->read()*sz;
        int y = cy + right->read()*sz;
        backBuffer.setPixel(x, y, WHITE);
      }
      float t2 = getElapsedTime();

      //backBuffer.setCursor(0, 8);
      //backBuffer.print(t2 - t1);
    }
    // copy pixels one at a time so we don't overwrite
    // the selected parameters displayed at the bottom of the screen
    for (int x = 0; x < screen.getWidth(); ++x)
    {
      for (int y = 0; y < displayHeight; ++y)
      {
        auto c = screen.getPixel(x, y) | backBuffer.getPixel(x, y);
        screen.setPixel(x, y, c);
      }
    }
  }
};
