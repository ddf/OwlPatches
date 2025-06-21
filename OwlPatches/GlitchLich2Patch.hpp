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

#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "TapTempo.hpp"
#include "BitCrusher.hpp"

typedef CircularBuffer<float> RecordBuffer;
typedef BitCrusher<24> BitCrush;

static const size_t RECORD_BUFFER_SIZE = (1 << 17);
typedef TapTempo<RECORD_BUFFER_SIZE> Clock;

// these are expressed as multiples of the clock
// and used to determine how long the frozen section of audio should be.
static const int FREEZE_RATIOS_COUNT = 9;
static const float freezeRatios[FREEZE_RATIOS_COUNT] = {
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              2.0 / 3,
              1.0,
              3.0 / 2,
              2.0,
              3.0,
              4.0
};

// these are the speeds at which the frozen audio should be played back.
// negative numbers mean the frozen audio should be played in reverse.
static const int PLAYBACK_SPEEDS_COUNT = 18;
static const float playbackSpeeds[PLAYBACK_SPEEDS_COUNT] = {
             -4.0,
             -3.0,
             -2.0,
             -3.0 / 2,
             -1.0,
             -2.0 / 3,
             -1.0 / 2,
             -1.0 / 3,
             -1.0 / 4,
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              2.0 / 3,
              1.0,
              3.0 / 2,
              2.0,
              3.0,
              4.0
};

// these are counters that indicate how many clock ticks should occur
// before resetting the read LFO when not frozen, in order to keep it in sync with the clock.
// it is a matrix because the period of the LFO, relative to the clock,
// is the speed divided by the freeze ratio, where the counter is the lowest common denominator
static const uint32_t freezeCounters[FREEZE_RATIOS_COUNT][PLAYBACK_SPEEDS_COUNT] = {
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
  // the period of the LFO, relative to the clock, is the speed divided by the freeze ratio,
  // where the counter is the lowest common denominator
  int readResetCount;
};

static const FreezeSettings freezeSettings[] = {
//  { 1.0 / 4, 1.0, 1 },
//  { 1.0 / 3, 1.0, 1 },
//  { 1.0 / 2, 1.0, 1 },
//  { 2.0 / 3, 1.0, 2 },
  { 1.0,     1.0, 1 },
  { 4.0 / 3, 1.0, 3 },
  { 2.0,     1.0, 2 },
  { 3.0 / 2, 1.0, 3 },
  { 4.0,     1.0, 4 },
  { 6.0,     1.0, 6 },
  { 8.0,     1.0, 8 },
//  { 9.0,     1.0, 9 },
  { 12.0,    1.0, 12 },
  { 16.0,    1.0, 16 },
};
static const int freezeSettingsCount = sizeof(freezeSettings) / sizeof(FreezeSettings);

static const int DROP_RATIOS_COUNT = 11;
static const float dropRatios[DROP_RATIOS_COUNT] = {
           8,
           6,
           4,
           3,
           2,
           1,
           1.0 / 2,
           1.0 / 3,
           1.0 / 4,
           1.0 / 6,
           1.0 / 8
};

static const uint32_t dropCounters[DROP_RATIOS_COUNT] = {
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

class GlitchLich2Patch : public Patch
{
  const PatchParameterId inSize = PARAMETER_A;
  const PatchParameterId inSpeed = PARAMETER_B;
  const PatchParameterId inDrop = PARAMETER_C;
  const PatchParameterId inCrush = PARAMETER_D;
  const PatchParameterId outRamp = PARAMETER_F;
  const PatchParameterId outRand = PARAMETER_G;

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
    : dcFilter(0), bufferL(0), bufferR(0), crushL(0), crushR(0)
    , clock(getSampleRate() * 60 / 120), samplesSinceLastTap(RECORD_BUFFER_SIZE)
    , freeze(false), freezeIdx(0), dropRatio(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferL = RecordBuffer::create(RECORD_BUFFER_SIZE);
    bufferR = RecordBuffer::create(RECORD_BUFFER_SIZE);
    crushL = BitCrush::create(getSampleRate(), getSampleRate());
    crushR = BitCrush::create(getSampleRate(), getSampleRate());

    readLfo = 0;
    readSpeed = 1;
    dropLfo = 0;

    registerParameter(inSize, "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDrop, "Drop");
    registerParameter(inCrush, "Crush");
    registerParameter(outRamp, "Ramp>");
    registerParameter(outRand, "Rand>");

    setParameterValue(inSize, 0.5f);
    setParameterValue(inSpeed, 0.75f);
    setParameterValue(inDrop, 0);
    setParameterValue(inCrush, 0);
  }

  ~GlitchLich2Patch()
  {
    StereoDcBlockingFilter::destroy(dcFilter);
    RecordBuffer::destroy(bufferL);
    RecordBuffer::destroy(bufferR);
    BitCrush::destroy(crushL);
    BitCrush::destroy(crushR);
  }


  float stepReadLFO(float speed)
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

  bool stepDropLFO(float speed)
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

  inline float interpolatedReadAt(RecordBuffer* buffer, float index)
  {
    // index can be negative, we ensure it is positive.
    // readAt will wrap value of the argument it receives.
    index += RECORD_BUFFER_SIZE;
    size_t idx = (size_t)(index);
    float low = buffer->readAt(idx);
    float high = buffer->readAt(idx + 1);
    float frac = index - idx;
    return high + frac * (low - high);
  }

  float freezeDuration(int idx)
  {
    float dur = clock.getPeriod() * freezeSettings[idx].clockRatio;
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  float freezeSpeed(int idx)
  {
    return freezeSettings[idx].playbackSpeed;
  }

  float dropDuration(int ratio)
  {
    float dur = clock.getPeriod() * dropRatios[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();

    clock.clock(size);

    // button 2 is for tap tempo now
    const bool mangle = false; // isButtonPressed(BUTTON_2);

    const float smoothFreeze = getParameterValue(inSize) * freezeSettingsCount;
    const int freezeIdx = (int)(smoothFreeze);

    float newFreezeLength = freezeDuration(freezeIdx) * (RECORD_BUFFER_SIZE - 1);
    float newReadSpeed = freezeSpeed(freezeIdx) / newFreezeLength;

    // smooth size and speed changes when not clocked
    const bool clocked = samplesSinceLastTap < RECORD_BUFFER_SIZE;
    if (!clocked)
    {
      if (freezeIdx < FREEZE_RATIOS_COUNT - 1)
      {
        float x1 = smoothFreeze - freezeIdx;
        float x0 = 1.0f - x1;
        newFreezeLength = newFreezeLength * x0 + (freezeDuration(freezeIdx + 1)*(RECORD_BUFFER_SIZE - 1))*x1;
        newReadSpeed = newReadSpeed * x0 + (freezeSpeed(freezeIdx + 1) / newFreezeLength)*x1;
      }
    }

    const float sr = getSampleRate();
    const float crush = getParameterValue(inCrush);
    const float bits = crush > 0.001f ? (8.f - crush * 6) : 24;
    const float rate = crush > 0.001f ? sr * 0.25f + getParameterValue(inCrush)*(100 - sr * 0.25f) : sr;
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

    for (int i = 0; i < size; ++i)
    {
      float x1 = (float)i / size;
      float x0 = 1.0f - x1;
      if (freeze)
      {
        float read0 = readEndIdx - freezeLength + readLfo * freezeLength;
        float read1 = readEndIdx - newFreezeLength + readLfo * newFreezeLength;
        left[i]  = interpolatedReadAt(bufferL, read0)*x0
                 + interpolatedReadAt(bufferL, read1)*x1;
        right[i] = interpolatedReadAt(bufferR, read0)*x0
                 + interpolatedReadAt(bufferR, read1)*x1;
      }
      stepReadLFO(readSpeed*x0 + newReadSpeed * x1);
    }

    freezeLength = newFreezeLength;
    readSpeed = newReadSpeed;

    crushL->process(left, left);
    crushR->process(right, right);

    float dropParam = getParameterValue(inDrop);
    dropRatio = (int)(dropParam * DROP_RATIOS_COUNT);
    float dropSpeed = 1.0f / (dropDuration(dropRatio) * (RECORD_BUFFER_SIZE - 1));
    float dropProb = dropParam < 0.0001f ? 0 : 0.1f + 0.9*dropParam;
    for (int i = 0; i < size; ++i)
    {
      if (stepDropLFO(dropSpeed))
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

    setParameterValue(outRamp, readLfo);
    setParameterValue(outRand, dropRand);
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
        readLfo = 0;
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

      // reset readLfo based on the counter for our combined ratios
      if (on && !freeze && ++freezeCounter >= freezeSettings[freezeIdx].readResetCount)
      {
        readLfo = 0;
        freezeCounter = 0;
      }

      // we use one instead of zero because our logic in process
      // is checking for the flip from 1 to 0 to generate a new random value.
      if (on && ++dropCounter >= dropCounters[dropRatio])
      {
        dropLfo = 1;
        dropCounter = 0;
      }
    }
  }

};
