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
#include "DelayProcessor.h"
#include "FeedbackProcessor.h"

#define DELAY_LINE_COUNT 2

static const float MIN_DELAY_SECONDS = 0.002f;
static const float MAX_DELAY_SECONDS = 1.0f;

typedef FeedbackSignalProcessor<CrossFadingDelayProcessor> DelayLine;

struct DelayParamIds
{
  PatchParameterId time; // length of the delay
  PatchParameterId feedback; // feedback amount
  PatchParameterId input; // amount of input fed into the delay
  PatchParameterId send; // amount of wet signal sent to the next delay
};

struct DelayParams
{
  SmoothFloat time;
  SmoothFloat feedback;
  SmoothFloat input;
  SmoothFloat send;
};

class DelayMatrixPatch : public Patch
{

  DelayLine* delays[DELAY_LINE_COUNT];
  DelayParamIds delayParamIds[DELAY_LINE_COUNT];
  DelayParams delayParamValues[DELAY_LINE_COUNT];

  FloatArray input;
  FloatArray recv;
  FloatArray accum;

public:
  DelayMatrixPatch()
  {
    const int blockSize = getBlockSize();
    const int delayLen = getSampleRate() * MAX_DELAY_SECONDS + 1;
    PatchParameterId firstParam = PARAMETER_A;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delays[i] = DelayLine::create(blockSize, CrossFadingCircularFloatBuffer::create(delayLen, blockSize));

      PatchParameterId timeParam = firstParam;
      PatchParameterId feedParam = (PatchParameterId)(firstParam + 1);
      PatchParameterId inputParam = (PatchParameterId)(firstParam + 2);
      PatchParameterId sendParam = (PatchParameterId)(firstParam + 3);
      registerParameter(timeParam, "Time");
      registerParameter(feedParam, "Fdbk");
      registerParameter(inputParam, "Input");
      registerParameter(sendParam, "Send");
      delayParamIds[i] = { timeParam, feedParam, inputParam, sendParam };
      firstParam = (PatchParameterId)(firstParam + 4);
    }

    input = FloatArray::create(blockSize);
    recv = FloatArray::create(blockSize);
    accum = FloatArray::create(blockSize);
  }

  ~DelayMatrixPatch()
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLine::destroy(delays[i]);
    }

    FloatArray::destroy(input);
    FloatArray::destroy(recv);
    FloatArray::destroy(accum);
  }

  void processAudio(AudioBuffer& audio) override
  {
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      delayParamValues[i].time = getParameterValue(delayParamIds[i].time);
      delayParamValues[i].feedback = getParameterValue(delayParamIds[i].feedback);
      delayParamValues[i].input = getParameterValue(delayParamIds[i].input);
      delayParamValues[i].send = getParameterValue(delayParamIds[i].send);
    }

    FloatArray leftAudio = audio.getSamples(0);

    accum.clear();
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayParams& params = delayParamValues[i];
      DelayLine* delay = delays[i];

      input.copyFrom(leftAudio);
      input.multiply(params.input);
      input.add(recv);

      float delaySamples = Interpolator::linear(MIN_DELAY_SECONDS, MAX_DELAY_SECONDS, params.time) * getSampleRate();
      delay->setDelay(delaySamples);
      delay->setFeedback(params.feedback);
      delay->process(input, recv);

      // accumulate wet delay signals
      accum.add(recv);

      // attenuate for the next delay with the send amount
      recv.multiply(params.send);
    }

    accum.multiply(0.5f);
    leftAudio.multiply(0.5f);
    leftAudio.add(accum);
  }

};
