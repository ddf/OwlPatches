#pragma once

#include "vessl/vessl.h"

using count_t = uint32_t;

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

using GlitchSampleType = vessl::frame::stereo::analog_t;
using BufferType = vessl::array<GlitchSampleType>;
using BitCrush = vessl::bitcrush<GlitchSampleType, 24>;
using Freeze = vessl::freeze<GlitchSampleType>;
using EnvelopeFollower = vessl::follow<float>;
using Array = vessl::array<float>;

template<uint32_t FREEZE_BUFFER_SIZE>
class Glitch : public vessl::unitProcessor<GlitchSampleType>, public vessl::clockable, protected vessl::plist<6>
{
public:
  using param = vessl::parameter;
  const parameters& getParameters() const override { return *this; }
 
private:
  struct
  {
    vessl::analog_p repeats;
    vessl::analog_p crush;
    vessl::analog_p glitch;
    vessl::analog_p shape;
    vessl::binary_p freeze;
    vessl::binary_p glitchEnabled;
  } params;
  BufferType freezeBuffer;
  Freeze freezeProc;
    
  float sampleRate;
  float glitchLfo;
  float glitchRand;
  count_t freezeSettingsIdx;
  count_t glitchSettingsIdx;
  count_t freezeCounter;
  count_t glitchCounter;
  count_t samplesSinceLastTap;
  
  BitCrush crushProc;
  
  BufferType processBuffer;

  Array followerWindow;
  EnvelopeFollower envelopeFollower;
  Array inputEnvelope;
  
public:
  Glitch(float sampleRate, vessl::size_t blockSize) 
  : clockable(sampleRate, static_cast<uint32_t>(blockSize), FREEZE_BUFFER_SIZE)
  , freezeBuffer(new GlitchSampleType[FREEZE_BUFFER_SIZE], FREEZE_BUFFER_SIZE)
  , freezeProc(freezeBuffer, sampleRate)
  , sampleRate(sampleRate)
  , glitchLfo(0), glitchRand(0), freezeSettingsIdx(0), glitchSettingsIdx(0)
  , freezeCounter(0), glitchCounter(0)
  , samplesSinceLastTap(FREEZE_BUFFER_SIZE)
  , crushProc(sampleRate, sampleRate)
  , processBuffer(new GlitchSampleType[blockSize], blockSize)
  , followerWindow(new float[blockSize*8], blockSize*8)  // NOLINT(bugprone-implicit-widening-of-multiplication-result)
  , envelopeFollower(followerWindow, sampleRate, 0.001f)
  , inputEnvelope(new float[blockSize], blockSize)
  {
  }
  
  ~Glitch() override
  {
    delete[] inputEnvelope.getData();
    delete[] followerWindow.getData();
    delete[] freezeBuffer.getData();
    delete[] processBuffer.getData();
  }

  using clockable::clock;

  param repeats() const { return params.repeats({"repeats", 'r', vessl::analog_p::type });  }
  param crush() const { return params.crush({"crush", 'c', vessl::analog_p::type }); }
  param glitch() const { return params.glitch({ "glitch", 'g', vessl::analog_p::type }); }
  param glitching() const { return params.glitchEnabled({ "glich enabled", 'e', vessl::binary_p::type }); }
  param shape() const { return params.shape({ "shape", 's', vessl::analog_p::type }); }
  param freeze() const { return params.freeze({ "freeze", 'f', vessl::binary_p::type }); }
  float freezePhase() const { return freezeProc.phase(); }
  float envelope() const { return inputEnvelope[0]; }
  float rand() const { return glitchRand; }

  void process(vessl::array<GlitchSampleType> input, vessl::array<GlitchSampleType> output) override
  {
    vessl::size_t size = input.getSize();
    clockable::tick(size);

    float smoothFreeze = repeats();
    for (freezeSettingsIdx = 0; freezeSettingsIdx < FREEZE_SETTINGS_COUNT - 1; freezeSettingsIdx++)
    {
      if (smoothFreeze >= FREEZE_SETTINGS[freezeSettingsIdx].paramThresh
        && smoothFreeze < FREEZE_SETTINGS[freezeSettingsIdx+1].paramThresh)
      {
        break;
      }
    }
    
    float newFreezeLength = freezeSize(freezeSettingsIdx);
    float newReadSpeed = freezeSpeed(freezeSettingsIdx);

    // smooth size and speed changes when not clocked
    bool clocked = samplesSinceLastTap < FREEZE_BUFFER_SIZE;
    if (!clocked)
    {
      if (freezeSettingsIdx < FREEZE_SETTINGS_COUNT - 1)
      {
        float p0 = FREEZE_SETTINGS[freezeSettingsIdx].paramThresh;
        float p1 = FREEZE_SETTINGS[freezeSettingsIdx+1].paramThresh;
        float t = (smoothFreeze - p0) / (p1 - p0);
        float d1 = freezeSize(freezeSettingsIdx + 1);
        newFreezeLength = newFreezeLength + (d1 - newFreezeLength)*t;
        newReadSpeed = newReadSpeed + (freezeSpeed(freezeSettingsIdx + 1) - newReadSpeed)*t;
      }
    }

    freezeProc.size() = newFreezeLength;
    freezeProc.rate() = newReadSpeed;
    freezeProc.enabled() = freeze().readBinary();

    float sr = sampleRate;
    float crushParam = crush();
    float bits = crushParam > 0.001f ? (16.f - crushParam*12.0f) : 24;
    float rate = crushParam > 0.001f ? sr * 0.25f + crushParam*(100 - sr * 0.25f) : sr;
    crushProc.depth() = bits;
    crushProc.rate() = rate;

    auto inputReader = input.getReader();
    auto iew = inputEnvelope.getWriter();
    while(inputReader)
    {
      iew << inputReader.read().toMono().value();
    }
    envelopeFollower.process(inputEnvelope, inputEnvelope);

    // can't use output as a process buffer because we need the dry input again for the shape stage.
    if (clocked)
    {
      freezeProc.process<vessl::duration::mode::fade>(input, processBuffer);
    }
    else
    {
      freezeProc.process<vessl::duration::mode::slew>(input, processBuffer);
    }
    
    crushProc.process(processBuffer, processBuffer);

    float glitchParam = glitch();
    glitchSettingsIdx = static_cast<int>(glitchParam * GLITCH_SETTINGS_COUNT);
    float glitchSpeed = 1.0f / glitchSize(glitchSettingsIdx);
    float glitchProb = glitchParam < 0.0001f ? 0 : 0.1f + 0.9f*glitchParam;
    for (count_t i = 0; i < size; ++i)
    {
      if (stepGlitchLfo(glitchSpeed))
      {
        glitchRand = vessl::random::range<float>(0.f, 1.f);
        params.glitchEnabled.value = glitchRand < glitchProb;
      }

      if (params.glitchEnabled.value)
      {
        vessl::size_t d = i+1;
        GlitchSampleType f = freezeProc.getBuffer().read(d);
        GlitchSampleType& pf = processBuffer[i];
        pf.left() = glitch(pf.left(), f.left());
        pf.right() = glitch(pf.right(), f.right());
      }
    }

    float shapeParam = shape();
    float shapeWet = shapeParam;
    float shapeDry = 1.0f - shapeWet;
    float fSize = static_cast<float>(size);
    inputReader.reset();
    for (count_t i = 0; i < size; ++i)
    {
      const float shapeScale = inputEnvelope[i]*fSize*(10.0f + 90.0f*shapeParam);
      const float dryIdx = static_cast<float>(i);
      // treat the process buffer like a wave table and use the dry input as phase, modulated by the envelope follower,
      // using shapeParam both for dry/wet mix and scaling of the envelope value.
      GlitchSampleType in = inputReader.read();
      const float readL = shapeDry*dryIdx + shapeWet*vessl::math::constrain(shapeScale*in.left(), -fSize, fSize);
      const float readR = shapeDry*dryIdx + shapeWet*vessl::math::constrain(shapeScale*in.right(), -fSize, fSize);
      output[i] = GlitchSampleType(interpolatedReadAt(processBuffer, readL).left(), interpolatedReadAt(processBuffer, readR).right());
    }

    if (samplesSinceLastTap < FREEZE_BUFFER_SIZE)
    {
      samplesSinceLastTap += size;
    }
  }

protected:
  param elementAt(vessl::size_t index) const override
  {
    param p[plsz] = { repeats(), crush(), glitch(), glitching(), shape(), freeze() };
    return p[index];
  }
  void tock(vessl::size_t sampleDelay) override
  {
    samplesSinceLastTap = 0;
      
    // reset readLfo based on the counter for our current setting
    if (++freezeCounter >= FREEZE_SETTINGS[freezeSettingsIdx].readResetCount)
    {
      freezeProc.reset();
      freezeCounter = 0;
    }

    // we use one instead of zero because our logic in process
    // is checking for the flip from 1 to 0 to generate a new random value.
    if (++glitchCounter >= GLITCH_SETTINGS[glitchSettingsIdx].lfoResetCount)
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

private:
  bool stepGlitchLfo(const float speed)
  {
    glitchLfo = glitchLfo + speed;
    if (glitchLfo >= 1)
    {
      glitchLfo -= 1;
      return true;
    }
    if (glitchLfo < 0)
    {
      glitchLfo += 1;
      return true;
    }
    return false;
  }

  float freezeSize(const count_t idx) const
  {
    return getPeriod() * FREEZE_SETTINGS[idx].clockRatio;
  }

  static float freezeSpeed(const count_t idx)
  {
    return FREEZE_SETTINGS[idx].playbackSpeed;
  }

  float glitchSize(const count_t idx) const
  {
    return getPeriod() * GLITCH_SETTINGS[idx].clockRatio;
  }

  static float glitch(const float a, const float b)
  {
    const int glitched = static_cast<int>(a*24) ^ static_cast<int>(b*24);
    return static_cast<float>(glitched) / 24;
  }
  
  static GlitchSampleType interpolatedReadAt(vessl::array<GlitchSampleType> buffer, float index)
  {
    // index can be negative, we ensure it is positive.
    index += static_cast<float>(buffer.getSize());
    count_t idx = static_cast<count_t>(index);
    const GlitchSampleType& low = buffer[idx%buffer.getSize()];
    const GlitchSampleType& high = buffer[(idx + 1)%buffer.getSize()];
    float frac = index - static_cast<float>(idx);
    return low + frac * (high - low);
  }
  
  GlitchSampleType process(const GlitchSampleType& in) override { return in; }
};