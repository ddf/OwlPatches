/**

AUTHOR:
    (c) 2021-2025 Damien Quartz

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
    A clockable freeze / bitcrush / glitch effect.

    Parameter A controls the length of the freeze buffer.

    Parameter B controls the amount of bitcrush,
    which is a mix of bit reduction and rate reduction.

    Parameter C controls a glitch effect,
    which mangles the result of frozen, bitcrushed input
    at regular intervals based on the clock,
    increasing in frequency as the parameter is increased.

    Parameter D controls the mix of a wave shaping effect that
    reinterprets the result of the glitch effect as if it is a
    wave table, using the dry input as phase, modulated by
    an envelope follower that tracks the dry input signal level.

    Button 1 and Gate A enable freeze.
    Button 2 is for tap tempo and Gate B for external clock.
    Gate Out is the internal clock for the freeze loop,
    which is influenced by Parameter A even when freeze is not activated.

    CV Out 1 is the envelope follower sampled at block rate.
    CV Out 2 is the random value used to determine when the engage glitch.

*/
#pragma once

#include <ios>

#include "DcBlockingFilter.h"
#include "Patch.h"
#include "PatchParameterDescription.h"
#include "Glitch.h"
#include "AudioBufferSourceSink.h"

constexpr FloatPatchParameterDescription IN_REPEATS = { "Repeats", 0, 1, 0.5f, 0.0f, 0.01f };
constexpr FloatPatchParameterDescription IN_SHAPE = { "Shape", 0, 1, 0.0f };
constexpr FloatPatchParameterDescription IN_CRUSH = { "Crush", 0, 1, 0.0f };
constexpr FloatPatchParameterDescription IN_GLITCH = { "Glitch", 0, 1, 0 };
constexpr FloatPatchParameterDescription IN_MIX = {"Mix", 0, 1, 0 };

constexpr OutputParameterDescription OUT_ENV = { "Env", PARAMETER_F };
constexpr OutputParameterDescription OUT_RAND = { "Rand", PARAMETER_G };
constexpr uint32_t GlitchBufferSize = 1 << 17;

class GlitchLich2Patch final : public Patch  // NOLINT(cppcoreguidelines-special-member-functions)
{
  FloatParameter pinRepeats;
  FloatParameter pinGlitch;
  FloatParameter pinShape;
  FloatParameter pinCrush;
  FloatParameter pinMix;
  OutputParameter poutEnv;
  OutputParameter poutRand;

  StereoDcBlockingFilter* dcFilter;
  Glitch<GlitchBufferSize>* glitch;
  vessl::array<GlitchSampleType> processBuffer;

public:
  GlitchLich2Patch()
    : Patch(), poutEnv(this, OUT_ENV), poutRand(this, OUT_RAND)
    , processBuffer(new GlitchSampleType[getBlockSize()], getBlockSize())
  {
    // order of registration determines parameter assignment, starting from PARAMETER_A
    pinRepeats = IN_REPEATS.registerParameter(this);
    pinCrush = IN_CRUSH.registerParameter(this);
    pinGlitch = IN_GLITCH.registerParameter(this);
    pinShape = IN_SHAPE.registerParameter(this);
    pinMix = IN_MIX.registerParameter(this);

    dcFilter = StereoDcBlockingFilter::create(0.995f);
    glitch = new Glitch<GlitchBufferSize>(getSampleRate(), getBlockSize());
  }

  ~GlitchLich2Patch() override
  {
    StereoDcBlockingFilter::destroy(dcFilter);
    delete[] processBuffer.getData();
    delete glitch;
  }

  void processAudio(AudioBuffer& audio) override
  {
    glitch->repeats() = pinRepeats.getValue();
    glitch->crush() = pinCrush.getValue();
    glitch->glitch() = pinGlitch.getValue();
    glitch->shape() = pinShape.getValue();

    dcFilter->process(audio, audio);

    AudioBufferReader<2> reader(audio);
    auto pbw = processBuffer.getWriter();
    while (reader)
    {
      pbw << reader.read();
    }
    
    glitch->process(processBuffer, processBuffer);

    AudioBufferWriter<2> writer(audio);
    auto pbr = processBuffer.getReader();
    while (pbr)
    {
      writer.write(pbr.read());
    }
    
    poutEnv.setValue(glitch->envelope());
    poutRand.setValue(glitch->rand());
    setButton(PUSHBUTTON, glitch->freezePhase() < 0.5f);
  }


  void buttonChanged(const PatchButtonId bid, const uint16_t value, const uint16_t samples) override
  {
    if (bid == BUTTON_1)
    {
      if (value == ON)
      {
        glitch->freeze() = true;
      }
      else
      {
        glitch->freeze() = false;
      }
    }

    if (bid == BUTTON_2 && value == ON)
    {
      glitch->clock(samples);
    }
  }

};
