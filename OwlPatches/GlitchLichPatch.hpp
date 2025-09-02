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
#include "vessl/vessl.h"

using BitCrush = vessl::bitcrush<float, 24>;

static const uint32_t TRIGGER_LIMIT = (1 << 17);

// these are expressed multiples of the clock
// and used to determine how long the frozen section of audio should be.
static const int FREEZE_RATIOS_COUNT = 9;
static const float FREEZE_RATIOS[FREEZE_RATIOS_COUNT] = { 
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
static const float PLAYBACK_SPEEDS[PLAYBACK_SPEEDS_COUNT] = { 
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

static const int DROP_RATIOS_COUNT = 11;
static const float DROP_RATIOS[DROP_RATIOS_COUNT] = { 
           8,
           6,
           4,
           3,
           2,
           1, 
           1.0/2, 
           1.0/3, 
           1.0/4, 
           1.0/6, 
           1.0/8
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

class GlitchLichPatch : public Patch
{
  const PatchParameterId inSize = PARAMETER_A;
  const PatchParameterId inSpeed = PARAMETER_B;
  const PatchParameterId inDrop = PARAMETER_C;
  const PatchParameterId inCrush = PARAMETER_D;
  const PatchParameterId outRamp = PARAMETER_F;
  const PatchParameterId outRand = PARAMETER_G;

  StereoDcBlockingFilter* dcFilter;
  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  BitCrush crushL;
  BitCrush crushR;
  TapTempo<TRIGGER_LIMIT> tempo;
  uint32_t samplesSinceLastTap;
  int freezeRatio;
  int playbackSpeed;
  float freezeLength;
  bool freeze;
  int freezeWriteCount;
  int readStartIdx;
  float readLfo;
  float readSpeed;
  float dropLfo;
  int dropRatio;
  bool dropSamples;
  float dropRand;

public:
  GlitchLichPatch()
    : dcFilter(0), bufferL(0), bufferR(0), crushL(getSampleRate(), getSampleRate()), crushR(getSampleRate(), getSampleRate())
    , tempo(getSampleRate()*60/120), samplesSinceLastTap(TRIGGER_LIMIT)
    , freeze(false) , freezeRatio(0), playbackSpeed(0), dropRatio(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferL = CircularBuffer<float>::create(TRIGGER_LIMIT);
    bufferR = CircularBuffer<float>::create(TRIGGER_LIMIT);

    readLfo = 0;
    readSpeed = 1;
    dropLfo = 0;

    registerParameter(inSize,  "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDrop,  "Drop");
    registerParameter(inCrush, "Crush");
    registerParameter(outRamp, "Ramp>");
    registerParameter(outRand, "Rand>");

    setParameterValue(inSize, 0.5f);
    setParameterValue(inSpeed, 0.75f);
    setParameterValue(inDrop, 0);
    setParameterValue(inCrush, 0);
  }

  ~GlitchLichPatch()
  {
    StereoDcBlockingFilter::destroy(dcFilter);
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
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

  inline float interpolatedReadAt(CircularBuffer<float>* buffer, float index) 
  {
    size_t idx = (size_t)index;
    float low = buffer->readAt(idx);
    float high = buffer->readAt(idx + 1);
    float frac = index - idx;
    return high + frac * (low - high);
  }

  float freezeDuration(int ratio)
  {
    float dur = tempo.getPeriod() * FREEZE_RATIOS[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  float dropDuration(int ratio)
  {
    float dur = tempo.getPeriod() * DROP_RATIOS[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();

    tempo.clock(size);

    // button 2 is for tap tempo now
    bool mangle = false; // isButtonPressed(BUTTON_2);

    float smoothSize = getParameterValue(inSize) * FREEZE_RATIOS_COUNT;
    float smoothSpeed = getParameterValue(inSpeed) * PLAYBACK_SPEEDS_COUNT;
    freezeRatio   = (int)(smoothSize);
    playbackSpeed = (int)(smoothSpeed);

    float newFreezeLength = freezeDuration(freezeRatio) * (TRIGGER_LIMIT - 1);
    float newReadSpeed    = PLAYBACK_SPEEDS[playbackSpeed] / newFreezeLength;

    // smooth size and speed changes when not clocked
    bool clocked = samplesSinceLastTap < TRIGGER_LIMIT;
    if (!clocked)
    {
      if (freezeRatio < FREEZE_RATIOS_COUNT - 1)
      {
        float x1 = smoothSize - freezeRatio;
        float x0 = 1.0f - x1;
        newFreezeLength = newFreezeLength * x0 + (freezeDuration(freezeRatio + 1)*(TRIGGER_LIMIT - 1))*x1;
      }

      if (playbackSpeed < PLAYBACK_SPEEDS_COUNT - 1)
      {
        float x1 = smoothSpeed - playbackSpeed;
        float x0 = 1.0f - x1;
        newReadSpeed = newReadSpeed * x0 + (PLAYBACK_SPEEDS[playbackSpeed + 1] / newFreezeLength)*x1;
      }
    }

    float sr = getSampleRate();
    float crush = getParameterValue(inCrush);
    float bits = crush > 0.001f ? (8.f - crush * 6) 
                                : 24;
    float rate = crush > 0.001f ? sr*0.25f + getParameterValue(inCrush)*(100 - sr*0.25f) 
                                : sr;
    crushL.depth() << bits;
    crushL.rate() << rate;
    crushL.mangle() << mangle;
    crushR.depth() << bits;
    crushR.rate() << rate;
    crushR.mangle() << mangle;

    dcFilter->process(audio, audio);

    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    for (int i = 0; i < size; ++i)
    {
      if (freeze && freezeWriteCount == TRIGGER_LIMIT)
        break;

      bufferL->write(left[i]);
      bufferR->write(right[i]);

      if (freeze)
        ++freezeWriteCount;
    }

    for (int i = 0; i < size; ++i)
    {
      float x1 = (float)i / size;
      float x0 = 1.0f - x1;
      if (freeze)
      {
        float read0 = readStartIdx + readLfo * freezeLength;
        float read1 = readStartIdx + readLfo * newFreezeLength;
        left[i]  = interpolatedReadAt(bufferL, read0)*x0 
                 + interpolatedReadAt(bufferL, read1)*x1;
        right[i] = interpolatedReadAt(bufferR, read0)*x0 
                 + interpolatedReadAt(bufferR, read1)*x1;
      }
      stepReadLFO(readSpeed*x0 + newReadSpeed*x1);
    }

    freezeLength = newFreezeLength;
    readSpeed = newReadSpeed;

    vessl::array<float> cpl(left.getData(), left.getSize());
    vessl::array<float> cpr(right.getData(), right.getSize());
    crushL.process(cpl, cpl);
    crushR.process(cpr, cpr);

    float dropParam = getParameterValue(inDrop);
    dropRatio = (int)(dropParam * DROP_RATIOS_COUNT);
    float dropSpeed = 1.0f / (dropDuration(dropRatio) * (TRIGGER_LIMIT - 1));
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

    if (samplesSinceLastTap < TRIGGER_LIMIT)
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
        readStartIdx = bufferL->getWriteIndex() - samples;
        if (readStartIdx < 0)
        {
          readStartIdx += TRIGGER_LIMIT;
        }
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
      tempo.trigger(on, samples);

      if (on)
      {
        samplesSinceLastTap = 0;
      }

      // reset readLfo based on the counter for our combined ratios
      if (on && !freeze && ++freezeCounter >= FREEZE_COUNTERS[freezeRatio][playbackSpeed])
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
