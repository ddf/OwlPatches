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

#include "DelayWithFreeze.h"
#include "vessl/vessl.h"

using Array = vessl::array<float>;
using Smoother = vessl::smoother<float>;
using Limiter = vessl::limiter<float>;
using DcBlockFilter = vessl::filter<float, vessl::filtering::dcblock>;
using LowPassFilter = vessl::filter<float, vessl::filtering::biquad<1>::lowPass>;
using GateOscil = vessl::oscil<vessl::waves::clock<>>;
using SineOscil = vessl::oscil<vessl::waves::sine<>>;
using RandomGenerator = vessl::noiseGenerator<float, vessl::noise::white>;
using DelayLine = DelayWithFreeze<float>;


// not a public unit processor because we only process stereo input in-place.
template<int DELAY_LINE_COUNT>
class DelayMatrix : vessl::unitProcessor<float>, vessl::clockable, protected vessl::plist<10>
{
public:
  enum FreezeState : uint8_t  // NOLINT(performance-enum-size)
  {
    FreezeOff = 0,
    FreezeEnter,
    FreezeOn,
    FreezeExit,
  };
  using param    = vessl::parameter;
  using desc     = param::desc;
  using analog_p = vessl::analog_p;
  using binary_p = vessl::binary_p;
  using freeze_p = vessl::param<FreezeState>;
  
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
  static constexpr uint8_t CLOCK_MULT_COUNT = sizeof(CLOCK_MULT) / sizeof(int);

  static constexpr int SPREAD_DIVMULT[] = { -4, -3, -2, 1, 2, 3, 4 };
  static constexpr uint8_t SPREAD_DIVMULT_COUNT = sizeof(SPREAD_DIVMULT) / sizeof(float);

  struct DelayLineData
  {
    float skew; // in samples
    int   delayLength;
    int   gateResetCounter;
    int   timeUpdateCount;
    
    Smoother time;
    Smoother input;
    Smoother cutoff;
    
    Array inputLeft;
    Array inputRight;
    Array outputLeft;
    Array outputRight;
    Limiter       limitLeft;
    Limiter       limitRight;
    DcBlockFilter dcBlockLeft;
    DcBlockFilter dcBlockRight;
    LowPassFilter lowPassLeft;
    LowPassFilter lowPassRight;
    GateOscil     gate;
    Smoother      feedback[DELAY_LINE_COUNT];
    
    Array output() { return { outputLeft.getData(), outputLeft.getSize()*2 }; }
    
    DelayLineData()
    : skew(0), delayLength(0), gateResetCounter(0), timeUpdateCount(0)
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
  
private:
  // parameters that are per-delay
  struct DLP
  {
    analog_p input;
    analog_p cutoff;
    analog_p feedback[DELAY_LINE_COUNT];
  };
  
  struct
  {
    analog_p time;
    analog_p spread;
    analog_p feedback;
    analog_p dryWet;
    analog_p skew;
    analog_p lfoOut;
    analog_p rndOut;
    analog_p modIndex;
    binary_p gateOut;
    freeze_p freezeState;
  } params;
    
  uint8_t clocked;
  uint8_t clockMultIndex;
  uint8_t spreadDivMultIndex;
  
  uint32_t samplesSinceLastTap;
  float timeRaw;
  float rndGen;
  float modAmount;
  float sampleRate;

  Smoother sTime;
  Smoother sSpread;
  Smoother sSkew;
  Smoother sFeedback;
  Smoother sDryWet;

  DcBlockFilter inputFilterLeft;
  DcBlockFilter inputFilterRight;
  
  SineOscil       uLfo;
  RandomGenerator uRnd;
  
  // used in processAudio to accumulate the wet signal
  Array outputWet;
  // buffer allocated to provide input and output buffers for each delay.
  Array processBuffer;
  // buffer allocated for delay line internal use.
  Array delayBuffer;
  
  struct StereoDelay
  {
    DelayLine left, right;
    
    StereoDelay(Array buffLeft, Array buffRight, float sampleRate)
      : left(buffLeft, sampleRate), right(buffRight, sampleRate)
    {
    }
  };
  
  StereoDelay* delays[DELAY_LINE_COUNT];
  DelayLineData delayData[DELAY_LINE_COUNT];
  DLP delayParams[DELAY_LINE_COUNT];

public:
  DelayMatrix(float sampleRate, vessl::size_t blockSize) 
    : clockable(sampleRate, 1, MAX_TIME_SECONDS * sampleRate * CLOCK_MULT[CLOCK_MULT_COUNT - 1])  // NOLINT(clang-diagnostic-float-conversion)
    , clocked(false)
    , clockMultIndex((CLOCK_MULT_COUNT - 1) / 2)
    , spreadDivMultIndex((SPREAD_DIVMULT_COUNT - 1) / 2)
    , samplesSinceLastTap(clockable::periodMax)
    , timeRaw(MIN_TIME_SECONDS * sampleRate)
    , rndGen(0), modAmount(0)
    , sampleRate(sampleRate)
    , sTime(0.9f, timeRaw)
    , inputFilterLeft(sampleRate)
    , inputFilterRight(sampleRate)
    , uLfo(sampleRate / static_cast<float>(blockSize), 1.0f)
    , uRnd(sampleRate / static_cast<float>(blockSize))
  {
    vessl::size_t outputSize = blockSize*2;
    outputWet = Array(new float[outputSize], outputSize);

    vessl::size_t processSize = blockSize * 4 * DELAY_LINE_COUNT;
    processBuffer = Array(new float[processSize], processSize);

    float maxTimeSamples = MAX_TIME_SECONDS * sampleRate;
    vessl::size_t delayBufferSize = 0;
    float* pbuff = processBuffer.getData();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      data.time.degree = 0.f;
      data.time = MIN_TIME_SECONDS * sampleRate;
      // want all lines to update immediately at startup
      data.timeUpdateCount = 9999;
      // calculate the longest this particular delay will ever need to get
      data.delayLength = static_cast<int>(maxTimeSamples 
                                        + maxTimeSamples * MAX_SPREAD * static_cast<float>(i) 
                                        + maxTimeSamples * MAX_MOD_AMT 
                                        + static_cast<float>(MAX_SKEW_SAMPLES));
      data.dcBlockLeft.setSampleRate(sampleRate);
      data.dcBlockRight.setSampleRate(sampleRate);
      data.lowPassLeft.setSampleRate(sampleRate);
      data.lowPassRight.setSampleRate(sampleRate);
      data.gate.setSampleRate(sampleRate);
      data.gate.waveform.pulseWidth = 0.1f;
      data.gateResetCounter = 0;
      data.limitLeft.preGain() = vessl::gain::fromScale(1.125f);
      data.limitRight.preGain() = vessl::gain::fromScale(1.125f);

      data.inputLeft = Array(pbuff + blockSize * 0, blockSize);
      data.inputRight = Array(pbuff + blockSize * 1, blockSize);
      data.outputLeft = Array(pbuff + blockSize * 2, blockSize);
      data.outputRight = Array(pbuff + blockSize * 3, blockSize);

      pbuff += blockSize * 4;
      delayBufferSize += data.delayLength * 2;
    }

    delayBuffer = Array(new float[delayBufferSize], delayBufferSize);
    float* dbuff = delayBuffer.getData();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      vessl::size_t buffSize = delayData[i].delayLength;
      Array dbl(dbuff, buffSize);
      Array dbr(dbuff + buffSize, buffSize);
      delays[i] = new StereoDelay(dbl, dbr, sampleRate);
      dbuff += buffSize * 2;
    }
  }

  ~DelayMatrix() override  // NOLINT(portability-template-virtual-member-function)
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delete delays[i];
    }
    
    delete[] delayBuffer.getData();
    delete[] processBuffer.getData();
    delete[] outputWet.getData();
  }
  
  param time() const { return params.time({ "time", 't', analog_p::type }); }
  param spread() const { return params.spread({ "spread", 's', analog_p::type }); }
  param feedback() const { return params.feedback({ "feedback", 'f', analog_p::type }); }
  param dryWet() const { return params.dryWet({ "dry/wet", 'w', analog_p::type }); }
  param skew() const { return params.skew({ "skew", 'k', analog_p::type }); }
  param mod() const { return params.modIndex({ "mod", 'm', analog_p::type }); }
  
  DLP& delay(vessl::size_t index) { return delayParams[index]; }
  
  param lfo() const { return params.lfoOut({ "lfo>", 'l', analog_p::type }); }
  param rnd() const { return params.rndOut({ "rand>", 'r', analog_p::type }); }
  param gate() const { return params.gateOut({ "gate", 'g', binary_p::type }); }
  param freeze() const { return params.freezeState({ "freeze state", 'z', freeze_p::type }); }
  

  // set the tempo via tapping
  void tap(uint16_t sampleDelay)
  {
    clock(sampleDelay);
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

  void toggleFreeze()
  {
    params.freezeState.value = params.freezeState.value == FreezeOff ? FreezeEnter : FreezeExit;
    bool freezeEnabled = params.freezeState.value == FreezeEnter;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delays[i]->left.freezeEnabled() = freezeEnabled;
      delays[i]->right.freezeEnabled() = freezeEnabled;
    }
  }
  
  void processStereo(Array audioLeft, Array audioRight)
  {
#ifdef PROFILE
    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "blk ");
    debugCpy = stpcpy(debugCpy, msg_itoa(audio.getSize(), 10));
    const float processStart = getElapsedBlockTime();
#endif

    vessl::size_t blockSize = audioLeft.getSize();
    tick(blockSize);
    clocked = samplesSinceLastTap < clockable::periodMax;

    float timeParam = params.time.value;
    float spreadParam = params.spread.value;

    // clocked
    if (clocked)
    {
      clockMultIndex = (CLOCK_MULT_COUNT - 1) / 2;
      if (timeParam >= 0.53f)
      {
        clockMultIndex = static_cast<uint8_t>(vessl::easing::lerp(static_cast<float>(clockMultIndex), static_cast<float>(CLOCK_MULT_COUNT), (timeParam - 0.53f) * 2.12f));
      }
      else if (timeParam <= 0.47f)
      {
        clockMultIndex = static_cast<uint8_t>(vessl::easing::lerp(static_cast<float>(clockMultIndex), 0.f, (0.47f - timeParam) * 2.12f));
      }
      // equivalent to multiplying the BPM
      timeRaw = getPeriod() / static_cast<float>(CLOCK_MULT[clockMultIndex]);

      spreadDivMultIndex = (SPREAD_DIVMULT_COUNT - 1) / 2;
      if (spreadParam >= 0.53f)
      {
        spreadDivMultIndex = static_cast<uint8_t>(vessl::easing::lerp(static_cast<float>(spreadDivMultIndex), static_cast<float>(SPREAD_DIVMULT_COUNT), (spreadParam - 0.53f) * 2.12f));
      }
      else if (spreadParam <= 0.47f)
      {
        spreadDivMultIndex = static_cast<uint8_t>(vessl::easing::lerp(static_cast<float>(spreadDivMultIndex), 0.f, (0.47f - spreadParam) * 2.12f));
      }
      float sdm = static_cast<float>(SPREAD_DIVMULT[spreadDivMultIndex]);
      sSpread = (sdm < 0 ? -1.0f / sdm : sdm);

      samplesSinceLastTap += blockSize;
    }
    // not clocked
    else
    {
      timeRaw = vessl::math::constrain(vessl::easing::lerp(MIN_TIME_SECONDS, MAX_TIME_SECONDS, timeParam / 0.99f), MIN_TIME_SECONDS, MAX_TIME_SECONDS) * sampleRate;

      if (spreadParam <= 0.5f)
      {
        sSpread = vessl::easing::lerp(MIN_SPREAD, MID_SPREAD, spreadParam*2);
      }
      else
      {
        sSpread = vessl::math::constrain(vessl::easing::lerp(MID_SPREAD, MAX_SPREAD, (spreadParam - 0.5f)*2.03f), MID_SPREAD, MAX_SPREAD);
      }
    }

    // increase smoothing duration when time parameter has not changed much since the last block
    // to help with the drift that tends to occur due to input noise or slightly jittered clock
    sTime.degree = vessl::math::abs(timeRaw - sTime.value) < 16 ? 0.999f : 0.9f;
    sTime = vessl::math::floor(timeRaw);

    sFeedback = params.feedback.value;
    sDryWet = params.dryWet.value;
    sSkew = params.skew.value;
    
    float modFreq = sampleRate / sTime.value * (1.0f / 32.f);
    
    uLfo.fHz() = modFreq;
    float lfoGen = uLfo.generate();

    uRnd.rate() = modFreq;
    rndGen = uRnd.generate<vessl::easing::smoothstep>();

    modAmount = 0;
    float modParam = params.modIndex.value;
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
      DLP& dps = delayParams[i];

      float invert = i % 2 ? 1.0f : -1.0f;
      float timeVal = sTime.value;
      float spreadVal = sSpread.value;
      float targetTime = timeVal + spreadVal * static_cast<float>(i) * timeVal;
      float timeDelta = vessl::math::abs(targetTime - data.time.value);
      int timeUpdateInterval = 8 + static_cast<int>(timeDelta) / (64*32);
      if (data.timeUpdateCount++ >= timeUpdateInterval)
      {
        data.time.degree = 0.9f - vessl::math::constrain(timeDelta / 2048.0f, 0.0f, 0.9f);
        data.time = targetTime;
        data.timeUpdateCount = 0;
      }
      data.skew = MAX_SKEW_SAMPLES * invert * sSkew.value;
      data.input = dps.input.value;
      data.cutoff = vessl::easing::interp<vessl::easing::expo::in>(MIN_CUTOFF, MAX_CUTOFF, dps.cutoff.value);

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        data.feedback[f] = sFeedback.value * (dps.feedback[f].value*2.0f - 0.99f);
      }
    }
    
    audioLeft >> inputFilterLeft >> audioLeft;
    audioRight >> inputFilterRight >> audioRight;

#ifdef PROFILE
    const float inputStart = getElapsedBlockTime();
#endif
    if (params.freezeState.value != FreezeOn)
    {
      // setup delay inputs with last blocks results
      float skewValue = sSkew.value;
      float cross = skewValue < 0.5f ? 0.0f : (skewValue - 0.5f)*0.15f;
      vessl::size_t inSize = blockSize;
      for (int i = 0; i < DELAY_LINE_COUNT; ++i)
      {
        DelayLineData& data = delayData[i];
        
        float inputScale = data.input.value;
        // ReSharper disable once CppExpressionWithoutSideEffects
        audioLeft.scale(inputScale, data.inputLeft);
        // ReSharper disable once CppExpressionWithoutSideEffects
        audioRight.scale(inputScale, data.inputRight);

        // add feedback from the matrix
        for (int f = 0; f < DELAY_LINE_COUNT; ++f)
        {
          // much faster to copy in a loop like this applying feedback
          // than to copy through scratch with block operations
          Array recvLeft  = delayData[f].outputLeft;
          Array recvRight = delayData[f].outputRight;
          float fbk = data.feedback[f].value * (1.0f - cross);
          float xbk = data.feedback[f].value * cross;
          for (vessl::size_t s = 0; s < inSize; ++s)
          {
            float rl = recvLeft[s];
            float rr = recvRight[s];
            data.inputLeft[s]  += rl * fbk + rr * xbk;
            data.inputRight[s] += rr * fbk + rl * xbk;
          }
        }

        // remove dc offset and limit
        data.inputLeft >> data.dcBlockLeft >> data.limitLeft >> data.inputLeft;
        data.inputRight >> data.dcBlockRight >> data.limitRight >> data.inputRight;

        if (params.freezeState.value == FreezeEnter)
        {
          float scale = 1.0f;
          float step = 1.0f / static_cast<float>(inSize);
          for (vessl::size_t s = 0; s < inSize; ++s)
          {
            data.inputLeft[s] *= scale;
            data.inputRight[s] *= scale;
            scale -= step;
          }
        }
        else if (params.freezeState.value == FreezeExit)
        {
          float scale = 0.0f;
          float step = 1.0f / static_cast<float>(inSize);
          for (vessl::size_t s = 0; s < inSize; ++s)
          {
            data.inputLeft[s] *= scale;
            data.inputRight[s] *= scale;
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
    outputWet.fill(0);
    vessl::size_t outSize = blockSize;
    float modValue = modAmount * sTime.value;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      StereoDelay* delay = delays[i];
      DelayLineData& data = delayData[i];
      GateOscil& gate = data.gate;

      const float delaySamples = data.time.value + modValue;
      if (params.freezeState.value == FreezeOn)
      {
        // how far back we can go depends on how big the frozen section is, we don't want to push past the size of the buffer
        float maxPosition = vessl::math::min(delaySamples * 8, static_cast<float>(data.delayLength));
        float normPosition = (1.0f - sFeedback.value);
        delay->left.freezeSize()  = delay->left.time()  = delaySamples;
        delay->right.freezeSize() = delay->right.time() = delaySamples;
        
        float posL = (maxPosition - delaySamples + data.skew)*normPosition;
        float posR = (maxPosition - delaySamples - data.skew)*normPosition;
        delay->left.freezePosition()  = posL;
        delay->right.freezePosition() = posR;
      }
      else
      {
        delay->left.freezeSize()  = delay->left.time()  = delaySamples + data.skew;
        delay->right.freezeSize() = delay->right.time() = delaySamples - data.skew;
      }

      delay->left.template process<vessl::duration::mode::fade>(data.inputLeft, data.inputLeft);
      delay->right.template process<vessl::duration::mode::fade>(data.inputRight, data.inputRight);

      // filter output
      float fltCut = static_cast<float>(data.cutoff.value);
      data.lowPassLeft.fHz()  = fltCut;
      data.lowPassRight.fHz() = fltCut;

      data.inputLeft  >> data.lowPassLeft  >> data.outputLeft;
      data.inputRight >> data.lowPassRight >> data.outputRight;

      float inputScale = data.input.value;
      
      if (params.freezeState.value == FreezeOn)
      {
        data.output().scale(inputScale);
      }

      // accumulate wet delay signals
      outputWet.add(data.output());

      // when clocked remove delay time modulation so that the gate output
      // stays in sync with the clock, keeping it true to the musical durations displayed on screen.
      float gfreq = sampleRate / (clocked ? (delaySamples - modValue) : delaySamples);
      gate.fHz() = gfreq;
      for (vessl::size_t s = 0; s < outSize; ++s)
      {
        delayGate |= (gate.generate()*inputScale > 0.1f) ? 1 : 0;
      }
    }
#ifdef PROFILE
    const float genTime = getElapsedBlockTime() - genStart;
    debugCpy = stpcpy(debugCpy, " gen ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(genTime * 1000), 10));
#endif

    if (params.freezeState.value == FreezeEnter)
    {
      params.freezeState.value = FreezeOn;
    }
    else if (params.freezeState.value == FreezeExit)
    {
      params.freezeState.value = FreezeOff;
    }

    float wet = sDryWet.value;
    float dry = 1.0f - wet;
    outputWet.scale(wet);
    
    audioLeft.scale(dry);
    audioRight.scale(dry);
    
    audioLeft.add(Array(outputWet.getData(), outSize));
    audioRight.add(Array(outputWet.getData()+outSize, outSize));

    params.lfoOut.value = lfoGen;
    params.rndOut.value = rndGen;
    params.gateOut.value = delayGate;

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - processStart - genTime - inputTime;
    debugCpy = stpcpy(debugCpy, " proc ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debugMsg);
#endif
  }
  
  const DelayLineData& getDelayData(int i) const { return delayData[i]; }
  float freezePosition(int i) const { return delays[i]->left.freezePosition().readAnalog(); }
  
  bool isClocked() const { return clocked; }
  using clockable::getBpm;
  int clockMult() const { return CLOCK_MULT[clockMultIndex]; }
  int spreadMult() const { return SPREAD_DIVMULT[spreadDivMultIndex]; }
  float modValue() const { return modAmount; }
  
  const parameters& getParameters() const override { return *this; }
  
protected:
  param elementAt(vessl::size_t index) const override
  {
    param p[plsz] = { time(), spread(), feedback(), dryWet(), skew(), mod(), lfo(), rnd(), gate(), freeze() };
    return p[index];
  }
  
private:
  float process(const float& in) override { return in; }

  // void processScreen(MonochromeScreenBuffer& screen) override  // NOLINT(portability-template-virtual-member-function)
  // {
  //   screen.setCursor(0, 10);
  //   screen.print("Clock Ratio: ");
  //   screen.print(clockMultIndex);
  //   screen.setCursor(0, 20);
  //   screen.print("Spread Ratio: ");
  //   screen.print(spreadDivMultIndex);
  //   screen.setCursor(0, 30);
  //   screen.print("Tap: ");
  //   screen.print(static_cast<int>(getPeriod()));
  //   screen.setCursor(0, 40);
  //   screen.print("Dly: ");
  //   screen.print(static_cast<int>(time.value));
  //   screen.print(freezeState != FreezeOff ? " F:X" : " F:O");
  //   screen.setCursor(0, 48);
  //   screen.print("MODF: ");
  //   screen.print(lfo.fHz().readAnalog());
  //   screen.print(" RND: ");
  //   screen.print(rndGen);
  // }

};
