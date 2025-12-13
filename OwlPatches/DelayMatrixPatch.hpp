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

*/
#pragma once

#include "MonochromeScreenPatch.h"
#include "StereoDelayProcessor.h"
#include "DelayWithFreeze.h"
#include "DcBlockingFilter.h"
#include "BiquadFilter.h"

#include "vessl/vessl.h"

// for building param names
#include <cstring>

#ifndef ARM_CORTEX
inline char * stpcpy(char *s1, const char *s2) { return strcat(s1, s2); }  // NOLINT(clang-diagnostic-deprecated-declarations)
#endif

using Smoother = vessl::smoother<float>;
using Limiter = vessl::limiter<float>;
using DcBlockFilter = vessl::filter<float, vessl::filtering::dcblock>;
using LowPassFilter = vessl::filter<float, vessl::filtering::biquad<1>::lowPass>;
using GateOscil = vessl::oscil<vessl::waves::clock<>>;
using SineOscil = vessl::oscil<vessl::waves::sine<>>;
using RandomGenerator = vessl::noiseGenerator<float, vessl::noise::white>;

//using DelayLine = StereoDelayProcessor<InterpolatingCircularFloatBuffer<LINEAR_INTERPOLATION>>;
//using DelayLine = StereoDelayWithFreezeProcessor<FastCrossFadingCircularFloatBuffer>;
// @todo replace with using DelayWithFreeze directly.
using DelayLine = StereoDelayWithFreeze<float>;

struct DelayMatrixParamIds
{
  PatchParameterId time;
  PatchParameterId spread;
  PatchParameterId feedback;
  PatchParameterId dryWet;
  PatchParameterId skew;
  PatchParameterId lfoOut;
  PatchParameterId rndOut;
  PatchParameterId modIndex;
};

template<int DELAY_LINE_COUNT>
class DelayMatrixPatch : public MonochromeScreenPatch, public vessl::clockable
{
protected:
  static constexpr float MIN_TIME_SECONDS = 0.002f;
  static constexpr float MAX_TIME_SECONDS = 0.25f;
  static constexpr float MIN_CUTOFF = 120.0f;
  static constexpr float MAX_CUTOFF = 22000.0f;
  // Spread calculator: https://www.desmos.com/calculator/xnzudjo949
  static constexpr float MIN_SPREAD = 0.25f;
  static constexpr float MID_SPREAD = 1.0f;
  static constexpr float MAX_SPREAD = 4.0f;
  static constexpr float MAX_MOD_AMT = 0.5f;
  static constexpr int   MAX_SKEW_SAMPLES = 48;

  static constexpr int CLOCK_MULT[] = { 32, 24, 16, 12, 8, 6, 4 };
  static constexpr int CLOCK_MULT_COUNT = sizeof(CLOCK_MULT) / sizeof(int);

  static constexpr int SPREAD_DIVMULT[] = { -4, -3, -2, 1, 2, 3, 4 };
  static constexpr int SPREAD_DIVMULT_COUNT = sizeof(SPREAD_DIVMULT) / sizeof(float);

  struct DelayLineParamIds
  {
    PatchParameterId input;    // amount of input fed into the delay
    PatchParameterId cutoff;   // cutoff for the filter
    PatchParameterId feedback[DELAY_LINE_COUNT];     // amount of wet signal sent to other delays
  };

  struct DelayLineData
  {
    float skew; // in samples
    int delayLength;
    int gateResetCounter;
    int timeUpdateInterval;
    int timeUpdateCount;
    Smoother time;
    Smoother input;
    Smoother cutoff;
    Limiter limitLeft;
    Limiter limitRight;
    // @todo replace with vessl arrays
    AudioBuffer* sigIn;
    AudioBuffer* sigOut;
    DcBlockFilter dcBlockLeft;
    DcBlockFilter dcBlockRight;
    LowPassFilter lowPassLeft;
    LowPassFilter lowPassRight;
    GateOscil gate;
    Smoother feedback[DELAY_LINE_COUNT];
    
    DelayLineData()
    : skew(0), delayLength(0), gateResetCounter(0), timeUpdateInterval(0), timeUpdateCount(0)
    , sigIn(nullptr), sigOut(nullptr)
    , dcBlockLeft(1), dcBlockRight(1)
    , lowPassLeft(1, 1, vessl::filtering::q::butterworth<float>())
    , lowPassRight(1, 1, vessl::filtering::q::butterworth<float>())
    {}
  };

  enum TapDelayLength : uint16_t  // NOLINT(performance-enum-size)
  {
    Quarter = 32 * 8 * 3 * 3,

    Long = Quarter * 16,
    Double = Quarter * 8,
    Whole = Quarter * 4,
    Half = Quarter * 2,
    One8 = Quarter / 2,
    One16 = Quarter / 4,
    One32 = Quarter / 8,
    One64 = Quarter / 16,
    One128 = Quarter / 32,
    One256 = Quarter / 64,
    One512 = Quarter / 128,
    One1028 = Quarter / 256,

    DoubleT = Long / 3,
    WholeT = Double / 3,
    HalfT = Whole / 3,
    QuarterT = Half / 3,
    One8T = Quarter / 3,
    One16T = One8 / 3,
    One32T = One16 / 3,
    One64T = One32 / 3,
    One128T = One64 / 3,
    One256T = One128 / 3,
    One512T = One256 / 3,
    One1028T = One512 / 3,

    WholeTT = DoubleT / 3,
    HalfTT = WholeT / 3,
    QuarterTT = HalfT / 3,
    One8TT = QuarterT / 3,
    One16TT = One8T / 3,
    One32TT = One16T / 3,
    One64TT = One32T / 3,
    One128TT = One64T / 3,
    One256TT = One128T / 3,
    One512TT = One256T / 3,
    One1028TT = One512T / 3,
  };

  DelayMatrixParamIds patchParams;
    
  int clockMultIndex;
  int spreadDivMultIndex;
  int samplesSinceLastTap;

  uint16_t clocked;

  enum : uint16_t  // NOLINT(performance-enum-size)
  {
    FreezeOff,
    FreezeEnter,
    FreezeOn,
    FreezeExit,
  } freezeState;
  
  float timeRaw;
  float rndGen;
  float modAmount;
  
  Smoother time;
  Smoother spread;
  Smoother skew;
  Smoother feedback;
  Smoother dryWet;

  DcBlockFilter inputFilterLeft;
  DcBlockFilter inputFilterRight;
  
  SineOscil       lfo;
  RandomGenerator rnd;

  // @todo replace with vessl array
  AudioBuffer* scratch;
  DelayLine* delays[DELAY_LINE_COUNT];
  DelayLineParamIds delayParamIds[DELAY_LINE_COUNT];
  DelayLineData delayData[DELAY_LINE_COUNT];

public:
  DelayMatrixPatch() : clockable(getSampleRate(), 1, MAX_TIME_SECONDS * getSampleRate() * CLOCK_MULT[CLOCK_MULT_COUNT - 1])
    , patchParams({ PARAMETER_A, PARAMETER_C, PARAMETER_B, PARAMETER_D, PARAMETER_E, PARAMETER_F, PARAMETER_G, PARAMETER_H })
    , clockMultIndex((CLOCK_MULT_COUNT - 1) / 2)
    , spreadDivMultIndex((SPREAD_DIVMULT_COUNT - 1) / 2)
    , samplesSinceLastTap(clockable::periodMax)
    , clocked(false)
    , freezeState(FreezeOff)
    , timeRaw(MIN_TIME_SECONDS * getSampleRate())
    , time(0.9f, timeRaw)
    , inputFilterLeft(getSampleRate())
    , inputFilterRight(getSampleRate())
    , lfo(getBlockRate(), 1.0f)
    , rnd(getBlockRate())
  {
    registerParameter(patchParams.time, "Time");
    registerParameter(patchParams.feedback, "Feedback");
    registerParameter(patchParams.spread, "Spread");
    registerParameter(patchParams.skew, "Skew");
    registerParameter(patchParams.dryWet, "Dry/Wet");
    registerParameter(patchParams.lfoOut, "LFO>");
    registerParameter(patchParams.rndOut, "RND>");
    registerParameter(patchParams.modIndex, "Mod");
    // 0.5f is "off" because turning left sends smooth noise to delay time, and turning right sends sine lfo
    setParameterValue(patchParams.modIndex, 0.5f);

    const int blockSize = getBlockSize();
    scratch = AudioBuffer::create(2, blockSize);

    const int maxTimeSamples = MAX_TIME_SECONDS * getSampleRate();
    char pname[16];
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      data.time.degree() = 0.f;
      data.time << MIN_TIME_SECONDS * getSampleRate();
      // want all lines to update immediately at startup
      data.timeUpdateCount = 9999;
      // calculate the longest this particular delay will ever need to get
      data.delayLength = maxTimeSamples + maxTimeSamples * MAX_SPREAD * i + maxTimeSamples * MAX_MOD_AMT + MAX_SKEW_SAMPLES;
      data.dcBlockLeft.setSampleRate(getSampleRate());
      data.dcBlockRight.setSampleRate(getSampleRate());
      data.lowPassLeft.setSampleRate(getSampleRate());
      data.lowPassRight.setSampleRate(getSampleRate());
      data.gate.setSampleRate(getSampleRate());
      data.gate.waveform.pulseWidth = 0.1f;
      data.gateResetCounter = 0;
      data.sigIn = AudioBuffer::create(2, blockSize);
      data.sigOut = AudioBuffer::create(2, blockSize);
      data.limitLeft.preGain() = vessl::gain::fromScale(1.125f);
      data.limitRight.preGain() = vessl::gain::fromScale(1.125f);

      DelayLineParamIds& params = delayParamIds[i];
      params.input = static_cast<PatchParameterId>(PARAMETER_AA + i);
      char* p = stpcpy(pname, "Gain ");
      stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.input, pname);
      setParameterValue(params.input, 0.99f);

      params.cutoff = static_cast<PatchParameterId>(PARAMETER_AE + i);
      p = stpcpy(pname, "Color ");
      stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.cutoff, pname);
      setParameterValue(params.cutoff, 0.99f);

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        params.feedback[f] = static_cast<PatchParameterId>(PARAMETER_BA + f * 4 + i);
        p = stpcpy(pname, "Fdbk ");
        p = stpcpy(p, msg_itoa(f + 1, 10));
        p = stpcpy(p, "->");
        stpcpy(p, msg_itoa(i + 1, 10));
        registerParameter(params.feedback[f], pname);
        // initialize the matrix so it sounds like 3 delays in parallel
        // when the global feedback param is turned up
        if (i == f) { setParameterValue(params.feedback[f], 0.99f); }
        else { setParameterValue(params.feedback[f], 0.5f); }
      }
    }

    for (int i = 0; i < DELAY_LINE_COUNT; ++i) { delays[i] = DelayLine::create(delayData[i].delayLength, blockSize, getSampleRate()); }
  }

  ~DelayMatrixPatch() override  // NOLINT(portability-template-virtual-member-function)
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine::destroy(delays[i]);
      AudioBuffer::destroy(delayData[i].sigIn);
      AudioBuffer::destroy(delayData[i].sigOut);
    }
    
    AudioBuffer::destroy(scratch);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override  // NOLINT(portability-template-virtual-member-function)
  {
    if (bid == BUTTON_1)
    {
      if (value == Patch::ON)
      {
        clock(samples);
        samplesSinceLastTap = 0;
        const int clockMult = CLOCK_MULT[clockMultIndex];
        const int spreadDivMult = SPREAD_DIVMULT[spreadDivMultIndex];
        const int tapFirst = Quarter / clockMult;
        for (int i = 0; i < DELAY_LINE_COUNT; ++i)
        {
          const int spreadInc = spreadDivMult < 0 ? tapFirst / -spreadDivMult : tapFirst * spreadDivMult;
          const int tapLength = (tapFirst + spreadInc * i);
          int quarter = Quarter;
          int resetAt = 1;
          int tap = 0;
          while (tap < quarter)
          {
            tap += tapLength;
            while (tap > quarter)
            {
              quarter += Quarter;
              resetAt += 1;
            }
            if (tap == quarter)
            {
              break;
            }
          }

          DelayLineData& data = delayData[i];
          if (++data.gateResetCounter >= resetAt)
          {
            data.gate.reset();
            data.gateResetCounter = 0;
          }
        }
      }
    }

    if (bid == BUTTON_2 && value == Patch::ON)
    {
      freezeState = freezeState == FreezeOff ? FreezeEnter : FreezeExit;
      for (int i = 0; i < DELAY_LINE_COUNT; ++i)
      {
        delays[i]->setFreeze(freezeState == FreezeEnter);
      }
    }
  }

  void processAudio(AudioBuffer& audio) override  // NOLINT(portability-template-virtual-member-function)
  {
#ifdef PROFILE
    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "blk ");
    debugCpy = stpcpy(debugCpy, msg_itoa(audio.getSize(), 10));
    const float processStart = getElapsedBlockTime();
#endif

    tick(audio.getSize());
    clocked = samplesSinceLastTap < clockable::periodMax;

    float timeParam = getParameterValue(patchParams.time);
    float spreadParam = getParameterValue(patchParams.spread);

    // clocked
    if (clocked)
    {
      clockMultIndex = (CLOCK_MULT_COUNT - 1) / 2;
      if (timeParam >= 0.53f)
      {
        clockMultIndex = vessl::easing::lerp(clockMultIndex, CLOCK_MULT_COUNT, (timeParam - 0.53f) * 2.12f);
      }
      else if (timeParam <= 0.47f)
      {
        clockMultIndex = vessl::easing::lerp(clockMultIndex, 0, (0.47f - timeParam) * 2.12f);
      }
      // equivalent to multiplying the BPM
      timeRaw = getPeriod() / CLOCK_MULT[clockMultIndex];

      spreadDivMultIndex = (SPREAD_DIVMULT_COUNT - 1) / 2;
      if (spreadParam >= 0.53f)
      {
        spreadDivMultIndex = vessl::easing::lerp(spreadDivMultIndex, SPREAD_DIVMULT_COUNT, (spreadParam - 0.53f) * 2.12f);
      }
      else if (spreadParam <= 0.47f)
      {
        spreadDivMultIndex = vessl::easing::lerp(spreadDivMultIndex, 0, (0.47f - spreadParam) * 2.12f);
      }
      int sdm = SPREAD_DIVMULT[spreadDivMultIndex];
      spread << (sdm < 0 ? -1.0f / sdm : sdm);

      samplesSinceLastTap += audio.getSize();
    }
    // not clocked
    else
    {
      timeRaw = vessl::math::constrain(vessl::easing::lerp(MIN_TIME_SECONDS, MAX_TIME_SECONDS, timeParam / 0.99f), MIN_TIME_SECONDS, MAX_TIME_SECONDS) * getSampleRate();

      if (spreadParam <= 0.5f)
      {
        spread << (vessl::easing::lerp(MIN_SPREAD, MID_SPREAD, spreadParam*2));
      }
      else
      {
        spread << (vessl::math::constrain(vessl::easing::lerp(MID_SPREAD, MAX_SPREAD, (spreadParam - 0.5f)*2.03f), MID_SPREAD, MAX_SPREAD));
      }
    }

    // increase smoothing duration when time parameter has not changed much since the last block
    // to help with the drift that tends to occur due to input noise or slightly jittered clock
    time.degree() = (vessl::math::abs(timeRaw - time.value().readAnalog()) < 16 ? 0.999f : 0.9f);
    time << static_cast<int>(timeRaw);

    feedback << getParameterValue(patchParams.feedback);
    dryWet << getParameterValue(patchParams.dryWet);
    skew << getParameterValue(patchParams.skew);
    
    float modFreq = getSampleRate() / time.value() * (1.0f / 32.f);
    
    lfo.fHz() = modFreq;
    float lfoGen = lfo.generate();

    rnd.rate() = modFreq;
    rndGen = rnd.generate<vessl::easing::smoothstep>();

    modAmount = 0;
    float modParam = getParameterValue(patchParams.modIndex);
    if (modParam >= 0.53f)
    {
      modAmount = lfoGen * vessl::math::constrain(vessl::easing::lerp(0.f, MAX_MOD_AMT, (modParam - 0.53f)*2.12f), 0.f, MAX_MOD_AMT);
    }
    else if (modParam <= 0.47f)
    {
      float modMax = vessl::math::constrain(vessl::easing::lerp(0.f, MAX_MOD_AMT, (0.47f - modParam)*2.12f), 0.f, MAX_MOD_AMT);
      modAmount = vessl::easing::lerp(-modMax, modMax, rndGen);
    }

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      DelayLineParamIds& params = delayParamIds[i];

      float invert = i % 2 ? 1.0f : -1.0f;
      float timeVal = time.value().readAnalog();
      float spreadVal = spread.value().readAnalog();
      float targetTime = timeVal + spreadVal * i * timeVal;
      float timeDelta = vessl::math::abs(targetTime - data.time.value().readAnalog());
      data.timeUpdateInterval = 8 + static_cast<int>(timeDelta) / (64*32);
      if (data.timeUpdateCount++ >= data.timeUpdateInterval)
      {
        data.time.degree() = 0.9f - vessl::math::constrain(timeDelta / 2048.0f, 0.0f, 0.9f);
        data.time << targetTime;
        data.timeUpdateCount = 0;
      }
      data.skew = MAX_SKEW_SAMPLES * invert * skew.value();
      data.input << getParameterValue(params.input);
      data.cutoff << vessl::easing::interp<vessl::easing::expo::in>(MIN_CUTOFF, MAX_CUTOFF, getParameterValue(params.cutoff));

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        data.feedback[f] << feedback.value() * (getParameterValue(params.feedback[f])*2.0f - 0.99f);
      }
    }
    
    vessl::array<float> audioLeft(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    vessl::array<float> audioRight(audio.getSamples(RIGHT_CHANNEL), audio.getSize());
    
    audioLeft >> inputFilterLeft >> audioLeft;
    audioRight >> inputFilterRight >> audioRight;

#ifdef PROFILE
    const float inputStart = getElapsedBlockTime();
#endif
    if (freezeState != FreezeOn)
    {


      // setup delay inputs with last blocks results
      float skewValue(skew.value());
      const float cross = skewValue < 0.5f ? 0.0f : (skewValue - 0.5f)*0.15f;
      for (int i = 0; i < DELAY_LINE_COUNT; ++i)
      {
        DelayLineData& data = delayData[i];
        AudioBuffer& input = *data.sigIn;

        int inSize = input.getSize();
        vessl::array<float> inLeft(input.getSamples(LEFT_CHANNEL), inSize);
        vessl::array<float> inRight(input.getSamples(RIGHT_CHANNEL), inSize);

        // faster than using block operations
        float inputScale = data.input.value().readAnalog();
        for (int s = 0; s < inSize; ++s)
        {
          inLeft[s] = audioLeft[s] * inputScale;
          inRight[s] = audioRight[s] * inputScale;
        }

        // add feedback from the matrix
        for (int f = 0; f < DELAY_LINE_COUNT; ++f)
        {
          // much faster to copy in a loop like this applying feedback
          // than to copy through scratch with block operations
          AudioBuffer& recv = *delayData[f].sigOut;
          vessl::array<float> recvLeft(recv.getSamples(LEFT_CHANNEL), recv.getSize());
          vessl::array<float> recvRight(recv.getSamples(RIGHT_CHANNEL), recv.getSize());
          float fbk = data.feedback[f].value() * (1.0f - cross);
          float xbk = data.feedback[f].value() * cross;
          for (int s = 0; s < inSize; ++s)
          {
            float rl = recvLeft[s];
            float rr = recvRight[s];
            inLeft[s] += rl * fbk + rr * xbk;
            inRight[s] += rr * fbk + rl * xbk;
          }
        }

        // remove dc offset
        inLeft >> data.dcBlockLeft >> data.limitLeft >> inLeft;
        inRight >> data.dcBlockRight >> data.limitRight >> inRight;

        if (freezeState == FreezeEnter)
        {
          float scale = 1.0f;
          float step = 1.0f / inSize;
          for (int s = 0; s < inSize; ++s)
          {
            inLeft[s] *= scale;
            inRight[s] *= scale;
            scale -= step;
          }
        }
        else if (freezeState == FreezeExit)
        {
          float scale = 0.0f;
          float step = 1.0f / inSize;
          for (int s = 0; s < inSize; ++s)
          {
            inLeft[s] *= scale;
            inRight[s] *= scale;
            scale += step;
          }
        }

        // very slow and I think harsher than the limiters
        //input.getSamples(LEFT_CHANNEL).tanh();
        //input.getSamples(RIGHT_CHANNEL).tanh();
      }
    }
#ifdef PROFILE
    const float inputTime = getElapsedBlockTime() - inputStart;
    debugCpy = stpcpy(debugCpy, " input ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(inputTime * 1000), 10));
#endif

    uint16_t delayGate = 0;

#ifdef PROFILE
    const float genStart = getElapsedBlockTime();
#endif
    // process all delays
    scratch->clear();
    const int outSize = scratch->getSize();
    const float modValue = modAmount * time.value();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine* delay = delays[i];
      DelayLineData& data = delayData[i];
      GateOscil& gate = data.gate;
      AudioBuffer& input = *data.sigIn;
      AudioBuffer& output = *data.sigOut;
      
      vessl::array<float> inL(input.getSamples(LEFT_CHANNEL), input.getSize());
      vessl::array<float> inR(input.getSamples(RIGHT_CHANNEL), input.getSize());
      vessl::array<float> outL(output.getSamples(LEFT_CHANNEL), output.getSize());
      vessl::array<float> outR(output.getSamples(RIGHT_CHANNEL), output.getSize());

      const float delaySamples = data.time.value() + modValue;
      if (freezeState == FreezeOn)
      {
        // how far back we can go depends on how big the frozen section is, we don't want to push past the size of the buffer
        const float maxPosition = vessl::math::min(delaySamples * 8, static_cast<float>(data.delayLength));
        const float normPosition = (1.0f - feedback.value());
        delay->setDelay(delaySamples, delaySamples);
        delay->setPosition((maxPosition - delaySamples + data.skew)*normPosition, (maxPosition - delaySamples - data.skew)*normPosition);
      }
      else
      {
        delay->setDelay(delaySamples + data.skew, delaySamples - data.skew);
      }

      delay->process<vessl::duration::mode::fade>(input, input);

      // filter output
      float fltCut = static_cast<float>(data.cutoff.value());
      data.lowPassLeft.fHz() = fltCut;
      data.lowPassRight.fHz() = fltCut;

      inL >> data.lowPassLeft >> outL;
      inR >> data.lowPassRight >> outR;

      float inputScale = data.input.value();
      
      if (freezeState == FreezeOn)
      {
        output.multiply(inputScale);
      }

      // accumulate wet delay signals
      scratch->add(output);

      // when clocked remove delay time modulation so that the gate output
      // stays in sync with the clock, keeping it true to the musical durations displayed on screen.
      const float gfreq = getSampleRate() / (clocked ? (delaySamples - modValue) : delaySamples);
      gate.fHz() = gfreq;
      for (int s = 0; s < outSize; ++s)
      {
        delayGate |= (gate.generate()*inputScale > 0.1f) ? 1 : 0;
      }
    }
#ifdef PROFILE
    const float genTime = getElapsedBlockTime() - genStart;
    debugCpy = stpcpy(debugCpy, " gen ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(genTime * 1000), 10));
#endif

    if (freezeState == FreezeEnter)
    {
      freezeState = FreezeOn;
    }
    else if (freezeState == FreezeExit)
    {
      freezeState = FreezeOff;
    }

    float wet(dryWet.value());
    float dry = 1.0f - wet;
    scratch->multiply(wet);
    audio.multiply(dry);
    audio.add(*scratch);

    setParameterValue(patchParams.lfoOut, vessl::math::constrain(lfoGen*0.5f + 0.5f, 0.f, 1.f));
    setParameterValue(patchParams.rndOut, vessl::math::constrain(rndGen, 0.f, 1.f));
    setButton(PUSHBUTTON, delayGate);
    setButton(BUTTON_2, freezeState == FreezeOn ? 1 : 0);
    // this is the second gate output on the Witch
    setButton(BUTTON_6, freezeState == FreezeOn ? 1 : 0);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - processStart - genTime - inputTime;
    debugCpy = stpcpy(debugCpy, " proc ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debugMsg);
#endif
  }

  void processScreen(MonochromeScreenBuffer& screen) override  // NOLINT(portability-template-virtual-member-function)
  {
    screen.setCursor(0, 10);
    screen.print("Clock Ratio: ");
    screen.print(clockMultIndex);
    screen.setCursor(0, 20);
    screen.print("Spread Ratio: ");
    screen.print(spreadDivMultIndex);
    screen.setCursor(0, 30);
    screen.print("Tap: ");
    screen.print(static_cast<int>(getPeriod()));
    screen.setCursor(0, 40);
    screen.print("Dly: ");
    screen.print(static_cast<int>(time.value().readAnalog()));
    screen.print(freezeState != FreezeOff ? " F:X" : " F:O");
    screen.setCursor(0, 48);
    screen.print("MODF: ");
    screen.print(lfo.fHz().readAnalog());
    screen.print(" RND: ");
    screen.print(rndGen);
  }

};
