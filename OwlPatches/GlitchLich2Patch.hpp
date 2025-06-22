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
    A clockable freeze / stutter / bit crush effect.

    Parameter A controls the length of the freeze buffer.
    Parameter B controls the speed at which it is looped.
    At 12 o'clock, the freeze buffer length is one clock tick.
    Turning CCW divides down to 1/4 of a clock tick
    and CW increases to 4 clock ticks, using musical divisions.
    Speed works the same way with the right-hand side of the knob
    going from 1/4x to 4x and the left-hand side doing the same,
    but in reverse. When not externally clocked, buffer length
    and speed are not snapped to fixed ratios.

    Knob C is a stutter effect inspired by Paratek's P3PB.
    Fully CCW the effect is off and turning CW it will
    randomly drop out audio on musical divisions of the clock
    from /8 to 8x with a higher probably of drop out as the rate
    increases.  CV Out 2 is the random value used by stutter
    to test if drop out should occur.

    Knob D is a bit crush effect that reduces both sample rate
    and bit depth as it is turned from fully CCW to CW.

    The first button and Gate A enable freeze.
    The second button is for tap tempo and Gate B for external clock.
    Gate Out is the internal clock for the freeze loop,
    which is influenced by both the A and B knobs,
    even when freeze is not activated.

    CV Out 1 is a ramp that represents the read head for the freeze loop,
    which rises with forward playback and descends when reversed.

*/
#pragma once

#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "TapTempo.hpp"
#include "BitCrusher.hpp"

typedef CircularBuffer<float> RecordBuffer;
typedef BitCrusher<24> BitCrush;

static constexpr size_t RECORD_BUFFER_SIZE = (1 << 17);
typedef TapTempo<RECORD_BUFFER_SIZE> Clock;

// these are expressed as multiples of the clock
// and used to determine how long the frozen section of audio should be.
static constexpr int FREEZE_RATIOS_COUNT = 9;
static constexpr float FREEZE_RATIOS[FREEZE_RATIOS_COUNT] = {
              1.0f / 4,
              1.0f / 3,
              1.0f / 2,
              2.0f / 3,
              1.0f,
              3.0f / 2,
              2.0f,
              3.0f,
              4.0f
};

// these are the speeds at which the frozen audio should be played back.
// negative numbers mean the frozen audio should be played in reverse.
static constexpr int PLAYBACK_SPEEDS_COUNT = 18;
static constexpr float PLAYBACK_SPEEDS[PLAYBACK_SPEEDS_COUNT] = {
             -4.0f,
             -3.0f,
             -2.0f,
             -3.0f / 2,
             -1.0f,
             -2.0f / 3,
             -1.0f / 2,
             -1.0f / 3,
             -1.0f / 4,
              1.0f / 4,
              1.0f / 3,
              1.0f / 2,
              2.0f / 3,
              1.0f,
              3.0f / 2,
              2.0f,
              3.0f,
              4.0f
};

// these are counters that indicate how many clock ticks should occur
// before resetting the read LFO when not frozen, in order to keep it in sync with the clock.
// it is a matrix because the period of the LFO, relative to the clock,
// is the speed divided by the freeze ratio, where the counter is the lowest common denominator
static const uint32_t FREEZE_COUNTERS[FREEZE_RATIOS_COUNT][PLAYBACK_SPEEDS_COUNT] = {
  // speed: -4  -3  -2  -3/2  -1  -2/3  -1/2  -1/3  -1/4  1/4  1/3  1/2  2/3  1  3/2  2  3  4  |     freeze ratio
           { 1,  1,  1,   1,   1,   3,    1,    3,    1,   1,   3,   1,   3,  1,  1,  1, 1, 1  }, // 1/4
           { 1,  1,  1,   2,   1,   1,    2,    1,    4,   4,   1,   2,   1,  1,  2,  1, 1, 1  }, // 1/3
           { 1,  1,  1,   1,   1,   3,    1,    3,    2,   2,   3,   1,   3,  1,  1,  1, 1, 1  }, // 1/2
           { 1,  2,  1,   4,   2,   1,    4,    2,    8,   8,   2,   4,   1,  2,  4,  1, 2, 1  }, // 2/3
           { 1,  1,  1,   2,   1,   3,    2,    3,    4,   4,   3,   2,   3,  1,  2,  1, 1, 1  }, // 1
           { 3,  1,  3,   1,   3,   9,    3,    9,    6,   6,   9,   3,   9,  3,  1,  3, 1, 3  }, // 3/2
           { 1,  2,  1,   4,   2,   3,    4,    6,    8,   8,   6,   4,   3,  2,  4,  1, 2, 1  }, // 2
           { 3,  1,  3,   2,   3,   9,    6,    9,   12,  12,   9,   6,   9,  3,  2,  3, 1, 3  }, // 3
           { 1,  4,  2,   8,   4,   6,    8,   12,   16,  16,  12,   8,   6,  4,  8,  2, 4, 1  }, // 4
};

struct FreezeSettings
{
  // used to determine how long the frozen section of audio should be.
  float clockRatio;
  // these are the speeds at which the frozen audio should be played back.
  float playbackSpeed;
  // how many clock ticks should occur before resetting the read LFO when not frozen,
  // in order to keep it in sync with the clock.
  size_t readResetCount;
  // param value at which to choose this setting
  float paramThresh;
};

static const FreezeSettings FREEZE_SETTINGS[] = {
  { 2.0f,     4.0f, 1, 0.0f  },
  { 2.0f,     3.0f, 2, 0.02f },
  { 2.0f,     2.0f, 1, 0.06f },
  { 4.0f/3.0f,1.0f, 4, 0.20f },
  { 2.0f,     1.0f, 2, 0.4f  },
  { 3.0f,     1.0f, 3, 0.6f  },
  { 4.0f,     1.0f, 4, 0.7f  },
  { 6.0f,     1.0f, 6, 0.85f },
  { 8.0f,     1.0f, 8, 0.95f },
};
static constexpr int FREEZE_SETTINGS_COUNT = sizeof(FREEZE_SETTINGS) / sizeof(FreezeSettings);

static constexpr int DROP_RATIOS_COUNT = 11;
static constexpr float DROP_RATIOS[DROP_RATIOS_COUNT] = {
           8,
           6,
           4,
           3,
           2,
           1,
           1.0f / 2,
           1.0f / 3,
           1.0f / 4,
           1.0f / 6,
           1.0f / 8
};

static const uint32_t DROP_COUNTERS[DROP_RATIOS_COUNT] = {
           8,
           6,
           4,
           3,
           2,
           1,
           1,
           1,
           1,
           1,
           1
};

constexpr PatchParameterId IN_SIZE = PARAMETER_A;
constexpr PatchParameterId IN_SPEED = PARAMETER_B;
constexpr PatchParameterId IN_DROP = PARAMETER_C;
constexpr PatchParameterId IN_CRUSH = PARAMETER_D;
constexpr PatchParameterId OUT_RAMP = PARAMETER_F;
constexpr PatchParameterId OUT_RAND = PARAMETER_G;

class GlitchLich2Patch final : public Patch
{
  StereoDcBlockingFilter* dcFilter;
  RecordBuffer* bufferL;
  RecordBuffer* bufferR;
  BitCrush* crushL;
  BitCrush* crushR;
  Clock clock;
  size_t samplesSinceLastTap;
  int freezeIdx;
  float freezeLength;
  bool freeze;
  int freezeWriteCount;
  size_t readEndIdx;
  float readLfo;
  float readSpeed;
  float dropLfo;
  int dropRatio;
  bool dropSamples;
  float dropRand;

public:
  GlitchLich2Patch()
  : dcFilter(nullptr), bufferL(nullptr), bufferR(nullptr), crushL(nullptr), crushR(nullptr)
  , clock(static_cast<int32_t>(getSampleRate() * 60.0f / 120.0f))
  , samplesSinceLastTap(RECORD_BUFFER_SIZE)
  , freezeIdx(0), freeze(false), dropRatio(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferL = RecordBuffer::create(RECORD_BUFFER_SIZE);
    bufferR = RecordBuffer::create(RECORD_BUFFER_SIZE);
    crushL = BitCrush::create(getSampleRate(), getSampleRate());
    crushR = BitCrush::create(getSampleRate(), getSampleRate());

    readLfo = 0;
    readSpeed = 1;
    dropLfo = 0;

    registerParameter(IN_SIZE, "Size");
    registerParameter(IN_SPEED, "Speed");
    registerParameter(IN_DROP, "Drop");
    registerParameter(IN_CRUSH, "Crush");
    registerParameter(OUT_RAMP, "Ramp>");
    registerParameter(OUT_RAND, "Rand>");

    setParameterValue(IN_SIZE, 0.5f);
    setParameterValue(IN_SPEED, 0.75f);
    setParameterValue(IN_DROP, 0);
    setParameterValue(IN_CRUSH, 0);
  }

  ~GlitchLich2Patch() override
  {
    StereoDcBlockingFilter::destroy(dcFilter);
    RecordBuffer::destroy(bufferL);
    RecordBuffer::destroy(bufferR);
    BitCrush::destroy(crushL);
    BitCrush::destroy(crushR);
  }


  float stepReadLfo(const float speed)
  {
    readLfo = readLfo + speed;
    if (readLfo >= 1)
    {
      readLfo -= 1;
    }
    else if (readLfo < 0)
    {
      readLfo += 1;
    }
    return readLfo;
  }

  bool stepDropLfo(const float speed)
  {
    dropLfo = dropLfo + speed;
    if (dropLfo >= 1)
    {
      dropLfo -= 1;
      return true;
    }
    else if (dropLfo < 0)
    {
      dropLfo += 1;
      return true;
    }
    return false;
  }

  static float interpolatedReadAt(RecordBuffer* buffer, float index)
  {
    // index can be negative, we ensure it is positive.
    // readAt will wrap value of the argument it receives.
    index += RECORD_BUFFER_SIZE;
    const size_t idx = (size_t)index;
    const float low = buffer->readAt(idx);
    const float high = buffer->readAt(idx + 1);
    const float frac = index - static_cast<float>(idx);
    return high + frac * (low - high);
  }

  float freezeDuration(const int idx)
  {
    float dur = clock.getPeriod() * FREEZE_SETTINGS[idx].clockRatio;
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  static float freezeSpeed(const int idx)
  {
    return FREEZE_SETTINGS[idx].playbackSpeed;
  }

  float dropDuration(const int ratio)
  {
    float dur = clock.getPeriod() * DROP_RATIOS[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();

    clock.clock(size);

    // button 2 is for tap tempo now
    constexpr bool mangle = false; // isButtonPressed(BUTTON_2);

    const float smoothFreeze = getParameterValue(IN_SIZE);
    for (freezeIdx = 0; freezeIdx < FREEZE_SETTINGS_COUNT - 1; freezeIdx++)
    {
      if (smoothFreeze >= FREEZE_SETTINGS[freezeIdx].paramThresh && smoothFreeze < FREEZE_SETTINGS[freezeIdx+1].paramThresh)
        break;
    }
    
    float newFreezeLength = freezeDuration(freezeIdx) * (RECORD_BUFFER_SIZE - 1);
    float newReadSpeed = freezeSpeed(freezeIdx) / newFreezeLength;

    // smooth size and speed changes when not clocked
    const bool clocked = samplesSinceLastTap < RECORD_BUFFER_SIZE;
    if (!clocked)
    {
      if (freezeIdx < FREEZE_RATIOS_COUNT - 1)
      {
        const float x1 = smoothFreeze - static_cast<float>(freezeIdx);
        const float x0 = 1.0f - x1;
        newFreezeLength = newFreezeLength * x0 + (freezeDuration(freezeIdx + 1)*(RECORD_BUFFER_SIZE - 1))*x1;
        newReadSpeed = newReadSpeed * x0 + (freezeSpeed(freezeIdx + 1) / newFreezeLength)*x1;
      }
    }

    const float sr = getSampleRate();
    const float crush = getParameterValue(IN_CRUSH);
    const float bits = crush > 0.001f ? (8.f - crush * 6) : 24;
    const float rate = crush > 0.001f ? sr * 0.25f + getParameterValue(IN_CRUSH)*(100 - sr * 0.25f) : sr;
    crushL->setBitDepth(bits);
    crushL->setBitRate(rate);
    crushL->setMangle(mangle);
    crushR->setBitDepth(bits);
    crushR->setBitRate(rate);
    crushR->setMangle(mangle);

    dcFilter->process(audio, audio);

    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    const int writeSize = freeze ? freezeWriteCount : size;
    for (int i = 0; i < writeSize; ++i)
    {
      bufferL->write(left[i]);
      bufferR->write(right[i]);
    }
    freezeWriteCount = 0;

    const float fSize = static_cast<float>(size);
    const float fEnd = static_cast<float>(readEndIdx);
    for (int i = 0; i < size; ++i)
    {
      const float x1 = static_cast<float>(i) / fSize;
      const float x0 = 1.0f - x1;
      if (freeze)
      {
        const float read0 = fEnd - freezeLength + readLfo * freezeLength;
        const float read1 = fEnd - newFreezeLength + readLfo * newFreezeLength;
        left[i]  = interpolatedReadAt(bufferL, read0)*x0
                 + interpolatedReadAt(bufferL, read1)*x1;
        right[i] = interpolatedReadAt(bufferR, read0)*x0
                 + interpolatedReadAt(bufferR, read1)*x1;
      }
      stepReadLfo(readSpeed*x0 + newReadSpeed * x1);
    }

    freezeLength = newFreezeLength;
    readSpeed = newReadSpeed;

    crushL->process(left, left);
    crushR->process(right, right);

    const float dropParam = getParameterValue(IN_DROP);
    dropRatio = static_cast<int>(dropParam * DROP_RATIOS_COUNT);
    const float dropSpeed = 1.0f / (dropDuration(dropRatio) * (RECORD_BUFFER_SIZE - 1));
    const float dropProb = dropParam < 0.0001f ? 0 : 0.1f + 0.9f*dropParam;
    for (int i = 0; i < size; ++i)
    {
      if (stepDropLfo(dropSpeed))
      {
        dropRand = randf();
        dropSamples = dropRand < dropProb;
      }

      if (dropSamples)
      {
        left[i] = 0;
        right[i] = 0;
      }
    }

    if (samplesSinceLastTap < RECORD_BUFFER_SIZE)
    {
      samplesSinceLastTap += size;
    }

    setParameterValue(OUT_RAMP, smoothFreeze /* readLfo*/);
    setParameterValue(OUT_RAND, FREEZE_SETTINGS[freezeIdx].paramThresh /* dropRand*/);
    setButton(PUSHBUTTON, readLfo < 0.5f);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    static uint32_t freezeCounter = 0;
    static uint32_t dropCounter = 0;

    if (bid == BUTTON_1)
    {
      if (value == ON)
      {
        freeze = true;
        freezeWriteCount = samples;
        readEndIdx = bufferL->getWriteIndex() + samples;
        //readLfo = 0;
      }
      else
      {
        freeze = false;
      }
    }

    if (bid == BUTTON_2)
    {
      bool on = value == ON;
      clock.trigger(on, samples);

      if (on)
      {
        samplesSinceLastTap = 0;
      }
      
      // reset readLfo based on the counter for our current setting
      if (on && ++freezeCounter >= FREEZE_SETTINGS[freezeIdx].readResetCount)
      {
        readLfo = 0;
        freezeCounter = 0;
      }

      // we use one instead of zero because our logic in process
      // is checking for the flip from 1 to 0 to generate a new random value.
      if (on && ++dropCounter >= DROP_COUNTERS[dropRatio])
      {
        dropLfo = 1;
        dropCounter = 0;
      }
    }
  }

};
