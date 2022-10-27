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

#include "Patch.h"
#include "DcBlockingFilter.h"
#include "BiquadFilter.h"
#include "StereoDelayProcessor.h"
#include <string.h>

#define DELAY_LINE_COUNT 4

static const float MIN_TIME_SECONDS = 0.002f;
static const float MAX_TIME_SECONDS = 0.25f;
static const float MIN_SPREAD = 0.25f;
static const float MAX_SPREAD = 2.0f;
// Spread calculator: https://www.desmos.com/calculator/xnzudjo949
static const float MAX_DELAY_LEN = MAX_TIME_SECONDS + MAX_SPREAD * (DELAY_LINE_COUNT - 1)*MAX_TIME_SECONDS;

typedef StereoCrossFadingDelayProcessor DelayLine;

struct DelayMatrixParamIds
{
  PatchParameterId time;
  PatchParameterId spread;
  PatchParameterId feedback;
  PatchParameterId dryWet;
  PatchParameterId skew;
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
  AudioBuffer* sigIn;
  AudioBuffer* sigOut;
};

class DelayMatrixPatch : public Patch
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

  AudioBuffer* scratch;
  AudioBuffer* accum;

public:
  DelayMatrixPatch()
    : patchParams({ PARAMETER_A, PARAMETER_C, PARAMETER_B, PARAMETER_D, PARAMETER_E })
  {
    registerParameter(patchParams.time, "Time");
    registerParameter(patchParams.spread, "Spread");
    registerParameter(patchParams.feedback, "Feedback");
    registerParameter(patchParams.skew, "Skew");
    registerParameter(patchParams.dryWet, "Dry/Wet");

    const int blockSize = getBlockSize();
    const int delayLen = getSampleRate() * MAX_DELAY_LEN + 1;
    char pname[16]; char* p;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delays[i] = DelayLine::create(delayLen, blockSize);

      DelayLineData& data = delayData[i];
      data.dcBlock = StereoDcBlockingFilter::create();
      data.filter = StereoBiquadFilter::create(getSampleRate());
      data.sigIn = AudioBuffer::create(2, blockSize);
      data.sigOut = AudioBuffer::create(2, blockSize);

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
  }

  void processAudio(AudioBuffer& audio) override
  {
    time = Interpolator::linear(MIN_TIME_SECONDS, MAX_TIME_SECONDS, getParameterValue(patchParams.time)) * getSampleRate();
    feedback = getParameterValue(patchParams.feedback)*0.9f;
    spread = Interpolator::linear(MIN_SPREAD, MAX_SPREAD, getParameterValue(patchParams.spread));
    dryWet = getParameterValue(patchParams.dryWet);
    skew = getParameterValue(patchParams.skew);

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      DelayLineParamIds& params = delayParamIds[i];

      data.time  = time + spread * i * time;
      data.input = getParameterValue(params.input)*0.9f;
      data.skew = i % 2 == 0 ? 24 * skew : -24 * skew;
      data.cutoff = Interpolator::linear(400, 18000, getParameterValue(params.cutoff));

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        data.feedback[f] = feedback * Interpolator::linear(-0.99f, 1.0f, getParameterValue(params.feedback[f]));
      }
    }

    inputFilter->process(audio, audio);

    // setup delay inputs with last blocks results
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine* delay = delays[i];
      DelayLineData& data = delayData[i];
      AudioBuffer& input = *data.sigIn;
      StereoDcBlockingFilter& filter = *data.dcBlock;

      input.copyFrom(audio);
      input.multiply(data.input);

      // add feedback from the matrix
      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        AudioBuffer& recv = *delayData[f].sigOut;
        scratch->copyFrom(recv);
        scratch->multiply(data.feedback[f]);
        input.add(*scratch);
      }

      filter.process(input, input);

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
  }

};
