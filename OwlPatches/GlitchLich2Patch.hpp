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

#include "Patch.h"
#include "PatchParameterDescription.h"

#include "DcBlockingFilter.h"
#include "CircularBuffer.h"
#include "TapTempo.hpp"
#include "EnvelopeFollower.h"
#include "vessl/vessl.h"

typedef uint32_t count_t;
typedef CircularBuffer<float, count_t> RecordBuffer;
typedef vessl::bitcrush<float, 24> BitCrush;

static constexpr count_t RECORD_BUFFER_SIZE = (1 << 17);
typedef TapTempo<RECORD_BUFFER_SIZE> Clock;

struct FreezeSettings
{
  // used to determine how long the frozen section of audio should be.
  float clockRatio;
  // these are the speeds at which the frozen audio should be played back.
  float playbackSpeed;
  // how many clock ticks should occur before resetting the read LFO when not frozen,
  // in order to keep it in sync with the clock.
  count_t readResetCount;
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
static constexpr count_t FREEZE_SETTINGS_COUNT = sizeof(FREEZE_SETTINGS) / sizeof(FreezeSettings);

struct GlitchSettings
{
  float clockRatio;
  count_t lfoResetCount;
};

static constexpr GlitchSettings GLITCH_SETTINGS[] = {
  { 1.0f / 32, 1 },
  { 1.0f / 24, 1 },
  { 1.0f / 16, 1 },
  { 1.0f / 12, 1 },
  { 1.0f / 8, 1 },
  { 1.0f / 6, 1 },
  { 1.0f / 4, 1 },
  { 1.0f / 3, 1 },
  { 1.0f / 2, 1 },
  { 1, 1 },
};
static constexpr count_t GLITCH_SETTINGS_COUNT = sizeof(GLITCH_SETTINGS) / sizeof(GlitchSettings);

constexpr FloatPatchParameterDescription IN_REPEATS = { "Repeats", 0, 1, 0.5f, 0.0f, 0.01f };
constexpr FloatPatchParameterDescription IN_SHAPE = { "Shape", 0, 1, 0.75f };
constexpr FloatPatchParameterDescription IN_CRUSH = { "Crush", 0, 1, 0.0f };
constexpr FloatPatchParameterDescription IN_GLITCH = { "Glitch", 0, 1, 0 };
constexpr FloatPatchParameterDescription IN_MIX = {"Mix", 0, 1, 0 };

constexpr OutputParameterDescription OUT_ENV = { "Env", PARAMETER_F };
constexpr OutputParameterDescription OUT_RAND = { "Rand", PARAMETER_G };

class GlitchLich2Patch final : public Patch  // NOLINT(cppcoreguidelines-special-member-functions)
{
  FloatParameter pinRepeats;
  FloatParameter pinGlitch;
  FloatParameter pinShape;
  FloatParameter pinCrush;
  FloatParameter pinMix;
  OutputParameter poutEnv;
  OutputParameter poutRand;
  
  count_t freezeIdx;
  count_t freezeWriteCount;
  float freezeLength;
  float readLfo;
  float readSpeed;

  count_t glitchSettingsIdx;
  float glitchLfo;
  float glitchRand;

  count_t readEndIdx;
  count_t freezeCounter;
  count_t glitchCounter;
  count_t samplesSinceLastTap;
  
  StereoDcBlockingFilter* dcFilter;
  EnvelopeFollower* envelopeFollower;
  BitCrush crushLeft;
  BitCrush crushRight;
  
  RecordBuffer* processBuffer[2];
  RecordBuffer* freezeBuffer[2];

  FloatArray inputEnvelope;
  Clock clock;
  
  bool freezeEnabled;
  bool glitchEnabled;

public:
  GlitchLich2Patch()
    : poutEnv(this, OUT_ENV), poutRand(this, OUT_RAND)
    , freezeIdx(0), freezeWriteCount(0), freezeLength(0)
    , readLfo(0), readSpeed(1)
    , glitchSettingsIdx(0), glitchLfo(0), glitchRand(0)
    , readEndIdx(0), freezeCounter(0), glitchCounter(0)
    , samplesSinceLastTap(RECORD_BUFFER_SIZE)
    , crushLeft(getSampleRate(), getSampleRate()), crushRight(getSampleRate(), getSampleRate())
    , clock(static_cast<int32_t>(getSampleRate() * 60.0f / 120.0f))
    , freezeEnabled(false), glitchEnabled(false)
  {
    inputEnvelope = FloatArray::create(getBlockSize());
    processBuffer[LEFT_CHANNEL] = RecordBuffer::create(getBlockSize());
    processBuffer[RIGHT_CHANNEL] = RecordBuffer::create(getBlockSize());
    freezeBuffer[LEFT_CHANNEL] = RecordBuffer::create(RECORD_BUFFER_SIZE);
    freezeBuffer[RIGHT_CHANNEL] = RecordBuffer::create(RECORD_BUFFER_SIZE);

    dcFilter = StereoDcBlockingFilter::create(0.995f);
    envelopeFollower = EnvelopeFollower::create(0.001f, getBlockSize() * 8, getSampleRate());

    // order of registration determines parameter assignment, starting from PARAMETER_A
    pinRepeats = IN_REPEATS.registerParameter(this);
    pinCrush = IN_CRUSH.registerParameter(this);
    pinGlitch = IN_GLITCH.registerParameter(this);
    pinShape = IN_SHAPE.registerParameter(this);
    pinMix = IN_MIX.registerParameter(this);
  }

  ~GlitchLich2Patch() override
  {
    StereoDcBlockingFilter::destroy(dcFilter);
    EnvelopeFollower::destroy(envelopeFollower);
    FloatArray::destroy(inputEnvelope);
    RecordBuffer::destroy(processBuffer[LEFT_CHANNEL]);
    RecordBuffer::destroy(processBuffer[RIGHT_CHANNEL]);
    RecordBuffer::destroy(freezeBuffer[LEFT_CHANNEL]);
    RecordBuffer::destroy(freezeBuffer[RIGHT_CHANNEL]);
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

  bool stepGlitchLfo(const float speed)
  {
    glitchLfo = glitchLfo + speed;
    if (glitchLfo >= 1)
    {
      glitchLfo -= 1;
      return true;
    }
    else if (glitchLfo < 0)
    {
      glitchLfo += 1;
      return true;
    }
    return false;
  }

  static float interpolatedReadAt(RecordBuffer* buffer, float index)
  {
    // index can be negative, we ensure it is positive.
    // readAt will wrap value of the argument it receives.
    index += static_cast<float>(buffer->getSize());
    const count_t idx = static_cast<count_t>(index);
    const float low = buffer->readAt(idx);
    const float high = buffer->readAt(idx + 1);
    const float frac = index - static_cast<float>(idx);
    return low + frac * (high - low);
  }

  float freezeDuration(const count_t idx) const
  {
    float dur = clock.getPeriod() * FREEZE_SETTINGS[idx].clockRatio;
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  static float freezeSpeed(const count_t idx)
  {
    return FREEZE_SETTINGS[idx].playbackSpeed;
  }

  float glitchDuration(const count_t idx) const
  {
    float dur = clock.getPeriod() * GLITCH_SETTINGS[idx].clockRatio;
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  static float glitch(const float a, const float b)
  {
    const int glitched = static_cast<int>(a*24) ^ static_cast<int>(b*24);
    return static_cast<float>(glitched) / 24;
  }

  void processAudio(AudioBuffer& audio) override
  {
    const count_t size = audio.getSize();

    clock.clock(size);

    const float smoothFreeze = pinRepeats.getValue(); // getParameterValue(IN_REPEATS);
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
      if (freezeIdx < FREEZE_SETTINGS_COUNT - 1)
      {
        const float p0 = FREEZE_SETTINGS[freezeIdx].paramThresh;
        const float p1 = FREEZE_SETTINGS[freezeIdx+1].paramThresh;
        const float t = (smoothFreeze - p0) / (p1 - p0);
        const float d1 = freezeDuration(freezeIdx + 1)*(RECORD_BUFFER_SIZE - 1);
        newFreezeLength = newFreezeLength + (d1 - newFreezeLength)*t;
        newReadSpeed = newReadSpeed + (freezeSpeed(freezeIdx + 1) / d1 - newReadSpeed)*t;
      }
    }

    const float sr = getSampleRate();
    const float crushParam = pinCrush.getValue(); // getParameterValue(IN_CRUSH);
    const float bits = crushParam > 0.001f ? (16.f - crushParam*12.0f) : 24;
    const float rate = crushParam > 0.001f ? sr * 0.25f + crushParam*(100 - sr * 0.25f) : sr;
    crushLeft.depth() << bits;
    crushRight.depth() << bits;
    crushLeft.rate() << rate;
    crushRight.rate() << rate;

    dcFilter->process(audio, audio);
    envelopeFollower->process(audio, inputEnvelope);

    FloatArray inputL = audio.getSamples(LEFT_CHANNEL);
    FloatArray inputR = audio.getSamples(RIGHT_CHANNEL);

    // just for readability in the final processing loop
    FloatArray outputL = audio.getSamples(LEFT_CHANNEL);
    FloatArray outputR = audio.getSamples(RIGHT_CHANNEL);

    FloatArray processedL(processBuffer[LEFT_CHANNEL]->getData(), processBuffer[LEFT_CHANNEL]->getSize());
    FloatArray processedR(processBuffer[RIGHT_CHANNEL]->getData(), processBuffer[RIGHT_CHANNEL]->getSize());

    inputL.copyTo(processedL);
    inputR.copyTo(processedR);

    RecordBuffer* freezeBufferL = freezeBuffer[LEFT_CHANNEL];
    RecordBuffer* freezeBufferR = freezeBuffer[RIGHT_CHANNEL];
    
    const count_t writeSize = freezeEnabled ? freezeWriteCount : size;
    for (count_t i = 0; i < writeSize; ++i)
    {
      freezeBufferL->write(inputL[i]);
      freezeBufferR->write(inputR[i]);
    }
    freezeWriteCount = 0;

    const float fSize = static_cast<float>(size);
    const float fEnd = static_cast<float>(readEndIdx);
    for (count_t i = 0; i < size; ++i)
    {
      const float x1 = static_cast<float>(i) / fSize;
      const float x0 = 1.0f - x1;
      if (freezeEnabled)
      {
        const float read0 = fEnd - freezeLength + readLfo * freezeLength;
        const float read1 = fEnd - newFreezeLength + readLfo * newFreezeLength;
        processedL[i] = interpolatedReadAt(freezeBufferL, read0)*x0 + interpolatedReadAt(freezeBufferL, read1)*x1;
        processedR[i] = interpolatedReadAt(freezeBufferR, read0)*x0 + interpolatedReadAt(freezeBufferR, read1)*x1;
      }
      stepReadLfo(readSpeed*x0 + newReadSpeed * x1);
    }
    
    freezeLength = newFreezeLength;
    readSpeed = newReadSpeed;

    vessl::array<float> procL(processedL.getData(), processedL.getSize());
    vessl::array<float> procR(processedR.getData(), processedR.getSize());
    crushLeft.process(procL, procL);
    crushRight.process(procR, procR);

    const float glitchParam = pinGlitch.getValue(); // getParameterValue(IN_GLITCH);
    glitchSettingsIdx = static_cast<int>(glitchParam * GLITCH_SETTINGS_COUNT);
    const float dropSpeed = 1.0f / (glitchDuration(glitchSettingsIdx) * (RECORD_BUFFER_SIZE - 1));
    const float dropProb = glitchParam < 0.0001f ? 0 : 0.1f + 0.9f*glitchParam;
    for (count_t i = 0; i < size; ++i)
    {
      if (stepGlitchLfo(dropSpeed))
      {
        glitchRand = randf();
        glitchEnabled = glitchRand < dropProb;
      }

      if (glitchEnabled)
      {
        const int d = static_cast<int>(i) + 1;
        freezeBufferL->setDelay(d);
        freezeBufferR->setDelay(d);
        processedL[i] = glitch(processedL[i], freezeBufferL->read());
        processedR[i] = glitch(processedR[i], freezeBufferR->read());
      }
    }

    const float shapeParam = pinShape.getValue(); // getParameterValue(IN_SHAPE);
    const float shapeWet = shapeParam;
    const float shapeDry = 1.0f - shapeWet;
    for (count_t i = 0; i < size; ++i)
    {
      const float shapeScale = inputEnvelope[i]*fSize*(10.0f + 90.0f*shapeParam);
      const float dryIdx = static_cast<float>(i);
      // treat the process buffer like a wave table and use the dry input as phase, modulated by the envelope follower,
      // using shapeParam both dry/wet mix and scaling of the envelope value.
      const float readL = shapeDry*dryIdx + shapeWet*clamp(shapeScale*inputL[i], -fSize, fSize);
      const float readR = shapeDry*dryIdx + shapeWet*clamp(shapeScale*inputR[i], -fSize, fSize);
      outputL[i] = interpolatedReadAt(processBuffer[LEFT_CHANNEL], readL);
      outputR[i] = interpolatedReadAt(processBuffer[RIGHT_CHANNEL], readR);
    }

    if (samplesSinceLastTap < RECORD_BUFFER_SIZE)
    {
      samplesSinceLastTap += size;
    }

    poutEnv.setValue(inputEnvelope[0]);
    poutRand.setValue(glitchRand);
    setButton(PUSHBUTTON, readLfo < 0.5f);
  }


  void buttonChanged(const PatchButtonId bid, const uint16_t value, const uint16_t samples) override
  {
    if (bid == BUTTON_1)
    {
      if (value == ON)
      {
        freezeEnabled = true;
        freezeWriteCount = samples;
        readEndIdx = freezeBuffer[LEFT_CHANNEL]->getWriteIndex() + samples;
      }
      else
      {
        freezeEnabled = false;
      }
    }

    if (bid == BUTTON_2)
    {
      const bool on = value == ON;
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
      if (on && ++glitchCounter >= GLITCH_SETTINGS[glitchSettingsIdx].lfoResetCount)
      {
        glitchLfo = 1;
        glitchCounter = 0;
      }

      // decided to remove this because it makes it impossible to get clean repeats, even with crush turned all the way down.
      // may revisit the idea later - might be interesting to do this as something that can blend in.
      // const bool mangle = freezeEnabled && on;
      // crushL->setMangle(mangle);
      // crushR->setMangle(mangle);
    }
  }

};
