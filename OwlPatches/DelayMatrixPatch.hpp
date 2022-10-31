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

#include "MonochromeScreenPatch.h"
#include "DcBlockingFilter.h"
#include "BiquadFilter.h"
#include "StereoDelayProcessor.h"
#include "TapTempo.h"
#include "SineOscillator.h"

// daisysp includes
#include "Dynamics/limiter.h"
#include "Utility/smooth_random.h"

// for building param names
#include <string.h>

using daisysp::Limiter;
using daisysp::SmoothRandomGenerator;

#define DELAY_LINE_COUNT 4

static const float MIN_TIME_SECONDS = 0.002f;
static const float MAX_TIME_SECONDS = 0.2f;
static const float MIN_SPREAD = 0.25f;
static const float MID_SPREAD = 1.0f;
static const float MAX_SPREAD = 4.0f;
// Spread calculator: https://www.desmos.com/calculator/xnzudjo949
static const float MAX_DELAY_LEN = MAX_TIME_SECONDS + MAX_SPREAD * (DELAY_LINE_COUNT - 1)*MAX_TIME_SECONDS;
static const float MAX_MOD_AMT = 0.5f;

static const float CLOCK_RATIOS[] = {
  1.0f / 4.0f,
  1.0f / 2.0f,
  3.0f / 4.0f,
  1.0f,
  1.5f,
  2.0f,
  4.0f
};
static const int CLOCK_RATIOS_COUNT = sizeof(CLOCK_RATIOS) / sizeof(float);

static const float SPREAD_RATIOS[] = {
  1.0f / 4.0f,
  1.0f / 2.0f,
  3.0f / 4.0f,
  1.0f,
  2.0f,
  3.0f,
  4.0f
};
static const int SPREAD_RATIOS_COUNT = sizeof(SPREAD_RATIOS) / sizeof(float);

typedef StereoCrossFadingDelayProcessor DelayLine;

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

struct DelayLineParamIds
{
  PatchParameterId input;    // amount of input fed into the delay
  PatchParameterId cutoff;   // cutoff for the filter
  PatchParameterId feedback[DELAY_LINE_COUNT];     // amount of wet signal sent to other delays
};

struct DelayLineData
{
  SmoothFloat time;
  SmoothFloat input;
  SmoothFloat skew; // in samples
  SmoothFloat cutoff;
  SmoothFloat feedback[DELAY_LINE_COUNT];
  StereoDcBlockingFilter* dcBlock;
  StereoBiquadFilter* filter;
  Limiter limitLeft;
  Limiter limitRight;
  AudioBuffer* sigIn;
  AudioBuffer* sigOut;
};

class DelayMatrixPatch : public MonochromeScreenPatch
{
  const DelayMatrixParamIds patchParams;

  SmoothFloat time;
  SmoothFloat spread;
  SmoothFloat skew;
  SmoothFloat feedback;
  SmoothFloat dryWet;

  StereoDcBlockingFilter* inputFilter;
  DelayLine* delays[DELAY_LINE_COUNT];
  DelayLineParamIds delayParamIds[DELAY_LINE_COUNT];
  DelayLineData delayData[DELAY_LINE_COUNT];

  int clockTriggerLimit;
  int clockRatioIndex;
  int spreadRatioIndex;
  TapTempo tapTempo;
  int samplesSinceLastTap;

  SineOscillator* lfo;
  SmoothRandomGenerator rnd;
  float rndGen;

  AudioBuffer* scratch;
  AudioBuffer* accum;

public:
  DelayMatrixPatch()
    : patchParams({ PARAMETER_A, PARAMETER_C, PARAMETER_B, PARAMETER_D, PARAMETER_E, PARAMETER_F, PARAMETER_G, PARAMETER_H })
    , time(0.9f, MIN_TIME_SECONDS*getSampleRate())
    , clockTriggerLimit(MAX_DELAY_LEN*getSampleRate()/4), clockRatioIndex((CLOCK_RATIOS_COUNT-1)/2)
    , tapTempo(getSampleRate(), clockTriggerLimit), samplesSinceLastTap(clockTriggerLimit), spreadRatioIndex((SPREAD_RATIOS_COUNT-1)/2)
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
    const int delayLen = getSampleRate() * MAX_DELAY_LEN * 1.5f;
    char pname[16]; char* p;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delays[i] = DelayLine::create(delayLen, blockSize);

      DelayLineData& data = delayData[i];
      data.dcBlock = StereoDcBlockingFilter::create();
      data.filter = StereoBiquadFilter::create(getSampleRate());
      data.sigIn = AudioBuffer::create(2, blockSize);
      data.sigOut = AudioBuffer::create(2, blockSize);
      data.limitLeft.Init();
      data.limitRight.Init();

      DelayLineParamIds& params = delayParamIds[i];
      params.input = (PatchParameterId)(PARAMETER_AA + i);
      p = stpcpy(pname, "Gain "); p = stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.input, pname);
      setParameterValue(params.input, 1.0f);
      
      params.cutoff = (PatchParameterId)(PARAMETER_AE + i);
      p = stpcpy(pname, "Color "); p = stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.cutoff, pname);
      setParameterValue(params.cutoff, 1);
      
      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        params.feedback[f] = (PatchParameterId)(PARAMETER_BA + f * 4 + i);
        p = stpcpy(pname, "Fdbk ");
        p = stpcpy(p, msg_itoa(f + 1, 10));
        p = stpcpy(p, "->");
        p = stpcpy(p, msg_itoa(i + 1, 10));
        registerParameter(params.feedback[f], pname);
        // initialize the matrix so it sounds like 3 delays in parallel
        // when the global feedback param is turned up
        if (i == f)
        {
          setParameterValue(params.feedback[f], 1);
        }
        else
        {
          setParameterValue(params.feedback[f], 0.5f);
        }
      }
    }

    accum = AudioBuffer::create(2, blockSize);
    scratch = AudioBuffer::create(2, blockSize);

    inputFilter = StereoDcBlockingFilter::create();
    lfo = SineOscillator::create(getBlockRate());
    rnd.Init(getBlockRate());
  }

  ~DelayMatrixPatch()
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine::destroy(delays[i]);
      AudioBuffer::destroy(delayData[i].sigIn);
      AudioBuffer::destroy(delayData[i].sigOut);
      StereoDcBlockingFilter::destroy(delayData[i].dcBlock);
      StereoBiquadFilter::destroy(delayData[i].filter);
    }

    AudioBuffer::destroy(accum);
    AudioBuffer::destroy(scratch);

    StereoDcBlockingFilter::destroy(inputFilter);
    SineOscillator::destroy(lfo);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1)
    {
      bool on = value == Patch::ON;
      tapTempo.trigger(on, samples);

      if (on)
      {
        samplesSinceLastTap = 0;
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    tapTempo.clock(audio.getSize());

    float timeParam = getParameterValue(patchParams.time);
    float spreadParam = getParameterValue(patchParams.spread);

    // clocked
    if (samplesSinceLastTap < clockTriggerLimit)
    {      
      clockRatioIndex = (CLOCK_RATIOS_COUNT - 1) / 2;
      if (timeParam >= 0.53f)
      {
        clockRatioIndex = Interpolator::linear(clockRatioIndex, CLOCK_RATIOS_COUNT, (timeParam - 0.53f) * 2.12f);
      }
      else if (timeParam <= 0.47f)
      {
        clockRatioIndex = Interpolator::linear(clockRatioIndex, 0, (0.47f - timeParam) * 2.12f);
      }
      time = tapTempo.getPeriodInSamples() * CLOCK_RATIOS[clockRatioIndex];

      spreadRatioIndex = (SPREAD_RATIOS_COUNT - 1) / 2;
      if (spreadParam >= 0.53f)
      {
        spreadRatioIndex = Interpolator::linear(spreadRatioIndex, SPREAD_RATIOS_COUNT, (spreadParam - 0.53f) * 2.12f);
      }
      else if (spreadParam <= 0.47f)
      {
        spreadRatioIndex = Interpolator::linear(spreadRatioIndex, 0, (0.47f - spreadParam) * 2.12f);
      }
      spread = SPREAD_RATIOS[spreadRatioIndex];

      samplesSinceLastTap += audio.getSize();
    }
    // not clocked
    else
    {
      time = Interpolator::linear(MIN_TIME_SECONDS, MAX_TIME_SECONDS, timeParam*1.01f) * getSampleRate();

      if (spreadParam <= 0.5f)
      {
        spread = Interpolator::linear(MIN_SPREAD, MID_SPREAD, spreadParam*2);
      }
      else
      {
        spread = Interpolator::linear(MID_SPREAD, MAX_SPREAD, (spreadParam - 0.5f)*1.02f);
      }
    }

    feedback = getParameterValue(patchParams.feedback)*0.9f;
    dryWet = getParameterValue(patchParams.dryWet);
    skew = getParameterValue(patchParams.skew);
    
    float modFreq = getSampleRate() / time * 0.0625f;
    
    lfo->setFrequency(modFreq);
    float lfoGen = lfo->generate();

    rnd.SetFreq(modFreq);
    // this random generator seems to only ever produce values
    // in the range [-1,0], even though the code claims to
    // (and looks like it should) produce values in the range [-1,1]
    rndGen = (rnd.Process()+0.5f)*2.0f;

    float modValue = 0;
    float modParam = getParameterValue(patchParams.modIndex);
    if (modParam >= 0.53f)
    {
      modValue = lfoGen * Interpolator::linear(0, MAX_MOD_AMT, (modParam - 0.53f)*2.12f);
    }
    else if (modParam <= 0.47f)
    {
      modValue = rndGen * Interpolator::linear(0, MAX_MOD_AMT, (0.47f - modParam)*2.12f);
    }

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      DelayLineParamIds& params = delayParamIds[i];

      float invert = i % 2 ? 1.0f : -1.0f;
      data.time = time + spread * i * time + modValue * time;
      data.input = getParameterValue(params.input)*0.9f;
      data.skew = skew * 24 * invert;
      data.cutoff = Interpolator::linear(400, 18000, getParameterValue(params.cutoff));

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        data.feedback[f] = feedback * Interpolator::linear(-0.99f, 1.0f, getParameterValue(params.feedback[f]));
      }
    }

    inputFilter->process(audio, audio);

    FloatArray audioLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray audioRight = audio.getSamples(RIGHT_CHANNEL);
    // setup delay inputs with last blocks results
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine* delay = delays[i];
      DelayLineData& data = delayData[i];
      AudioBuffer& input = *data.sigIn;
      StereoDcBlockingFilter& filter = *data.dcBlock;

      int inSize = input.getSize();
      FloatArray inLeft = input.getSamples(LEFT_CHANNEL);
      FloatArray inRight = input.getSamples(RIGHT_CHANNEL);

      input.clear();

      // add feedback from the matrix
      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        AudioBuffer& recv = *delayData[f].sigOut;
        scratch->copyFrom(recv);
        scratch->multiply(data.feedback[f]);
        input.add(*scratch);
      }

      // remove dc offset
      filter.process(input, input);

      for (int s = 0; s < inSize; ++s)
      {
        inLeft[s]  += audioLeft[s] * data.input;
        inRight[s] += audioRight[s] * data.input;
      }

      // limit the feedback signal
      data.limitLeft.ProcessBlock(inLeft, inSize, 1.125f);
      data.limitRight.ProcessBlock(inRight, inSize, 1.125f);

      // very slow and I think harsher than the limiters
      //input.getSamples(LEFT_CHANNEL).tanh();
      //input.getSamples(RIGHT_CHANNEL).tanh();
    }

    // process all delays
    accum->clear();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine* delay = delays[i];
      DelayLineData& data = delayData[i];
      StereoBiquadFilter& filter = *data.filter;
      AudioBuffer& input = *data.sigIn;
      AudioBuffer& output = *data.sigOut;

      float delaySamples = data.time;
      delay->setDelay(delaySamples + data.skew, delaySamples - data.skew);
      delay->process(input, output);

      // filter output
      filter.setLowPass(data.cutoff, FilterStage::BUTTERWORTH_Q);
      filter.process(output, output);

      // accumulate wet delay signals
      accum->add(output);
    }

    const float wet = dryWet;
    const float dry = 1.0f - dryWet;
    accum->multiply(wet);
    audio.multiply(dry);
    audio.add(*accum);

    setParameterValue(patchParams.lfoOut, clamp(lfoGen*0.5f + 0.5f, 0.f, 1.f));
    setParameterValue(patchParams.rndOut, clamp(rndGen*0.5f + 0.5f, 0.f, 1.f));
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 10);
    screen.print("Clock Ratio: ");
    screen.print(clockRatioIndex);
    screen.setCursor(0, 20);
    screen.print("Spread Ratio: ");
    screen.print(spreadRatioIndex);
    screen.setCursor(0, 30);
    screen.print("Tap Smp: ");
    screen.print((int)tapTempo.getPeriodInSamples());
    screen.print(tapTempo.isOn() ? " X" : " O");
    screen.setCursor(0, 40);
    screen.print("Dly Smp: ");
    screen.print(time.getValue());
    screen.setCursor(0, 48);
    screen.print("LFO: ");
    screen.print(lfo->getFrequency());
    screen.print("RND: ");
    screen.print(rndGen);
  }

};
