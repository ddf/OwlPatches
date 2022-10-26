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

#define DELAY_LINE_COUNT 3

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
};

struct DelayLineParamIds
{
  PatchParameterId input;    // amount of input fed into the delay
  PatchParameterId skew;     // amount to skew the delay between left and right
  PatchParameterId cutoff;   // cutoff for the filter
  PatchParameterId send;     // amount of wet signal sent to other delays
};

struct DelayLineData
{
  SmoothFloat time;
  SmoothFloat feedback;
  SmoothFloat input;
  SmoothFloat skew; // in samples
  SmoothFloat cutoff;
  SmoothFloat send;
  StereoBiquadFilter* filter;
  AudioBuffer* out;
};

class DelayMatrixPatch : public Patch
{
  const DelayMatrixParamIds patchParams;

  SmoothFloat time;
  SmoothFloat spread;
  SmoothFloat feedback;
  SmoothFloat dryWet;

  StereoDcBlockingFilter* inputFilter;
  DelayLine* delays[DELAY_LINE_COUNT];
  DelayLineParamIds delayParamIds[DELAY_LINE_COUNT];
  DelayLineData delayData[DELAY_LINE_COUNT];

  AudioBuffer* input;
  AudioBuffer* accum;

public:
  DelayMatrixPatch()
    : patchParams({ PARAMETER_A, PARAMETER_C, PARAMETER_B, PARAMETER_D })
  {
    registerParameter(patchParams.time, "Time");
    registerParameter(patchParams.spread, "Spread");
    registerParameter(patchParams.feedback, "Feedback");
    registerParameter(patchParams.dryWet, "Dry/Wet");

    const int blockSize = getBlockSize();
    const int delayLen = getSampleRate() * MAX_DELAY_LEN + 1;
    int firstParam = PARAMETER_E;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delays[i] = DelayLine::create(delayLen, blockSize);
      delayData[i].filter = StereoBiquadFilter::create(getSampleRate());
      delayData[i].out = AudioBuffer::create(2, blockSize);

      PatchParameterId inputParam = (PatchParameterId)(firstParam);
      PatchParameterId rotateParam = (PatchParameterId)(firstParam + 1);
      PatchParameterId cutoffParam = (PatchParameterId)(firstParam + 2);
      PatchParameterId sendParam = (PatchParameterId)(firstParam+3);
      registerParameter(inputParam, "Gain");
      registerParameter(rotateParam, "Skew");
      setParameterValue(rotateParam, 0.5f);
      registerParameter(sendParam, "Radiate");
      registerParameter(cutoffParam, "Color");
      setParameterValue(cutoffParam, 1);
      delayParamIds[i] = { inputParam, rotateParam, cutoffParam, sendParam };
      firstParam += 4;
    }

    input = AudioBuffer::create(2, blockSize);
    accum = AudioBuffer::create(2, blockSize);

    inputFilter = StereoDcBlockingFilter::create();
  }

  ~DelayMatrixPatch()
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine::destroy(delays[i]);
      AudioBuffer::destroy(delayData[i].out);
      StereoBiquadFilter::destroy(delayData[i].filter);
    }

    AudioBuffer::destroy(input);
    AudioBuffer::destroy(accum);

    StereoDcBlockingFilter::destroy(inputFilter);
  }

  void processAudio(AudioBuffer& audio) override
  {
    time = Interpolator::linear(MIN_TIME_SECONDS, MAX_TIME_SECONDS, getParameterValue(patchParams.time)) * getSampleRate();
    feedback = getParameterValue(patchParams.feedback)*0.95f;
    spread = Interpolator::linear(MIN_SPREAD, MAX_SPREAD, getParameterValue(patchParams.spread));
    dryWet = getParameterValue(patchParams.dryWet);

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delayData[i].time  = time + spread * i * time;
      delayData[i].feedback = feedback;
      delayData[i].input = getParameterValue(delayParamIds[i].input);
      delayData[i].send  = getParameterValue(delayParamIds[i].send) * 0.515f * (1.0f - feedback);
      delayData[i].skew = Interpolator::linear(-24, 24, getParameterValue(delayParamIds[i].skew));
      delayData[i].cutoff = Interpolator::linear(400, 18000, getParameterValue(delayParamIds[i].cutoff));
    }

    inputFilter->process(audio, audio);

    accum->clear();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine* delay = delays[i];
      DelayLineData& data = delayData[i];
      StereoBiquadFilter& filter = *data.filter;
      AudioBuffer& out = *data.out;

      input->copyFrom(audio);
      input->multiply(data.input);

      // with send values capped at 0.5 this gives reverby results a lot
      // can get really overloaded, a little distorted, but doesn't turn to screaming feedback
      // with all the parameters at max
      for (int j = 0; j < DELAY_LINE_COUNT - 1; ++j)
      {
        DelayLineData& recv = delayData[(i + j) % DELAY_LINE_COUNT];
        input->add(*recv.out);
      }

      // more weird delay with sometimes reverberation appearing
      //DelayLineData& recv = delayData[(i + 2) % DELAY_LINE_COUNT];
      //input->add(*recv.out);

      float delaySamples = data.time;
      delay->setDelay(delaySamples + data.skew, delaySamples - data.skew);
      delay->setFeedback(data.feedback);
      delay->process(*input, out);

      // filter output
      filter.setLowPass(data.cutoff, FilterStage::BUTTERWORTH_Q);
      filter.process(out, out);

      // accumulate wet delay signals
      accum->add(out);

      // attenuate for other delays with the send amount
      out.multiply(data.send);
    }

    const float wet = dryWet;
    const float dry = 1.0f - dryWet;
    accum->multiply(wet);
    audio.multiply(dry);
    audio.add(*accum);
  }

};
