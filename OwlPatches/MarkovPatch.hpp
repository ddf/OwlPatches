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
#include "VoltsPerOctave.h"
#include "AdsrEnvelope.h"
#include "MarkovChain.hpp"
#include <string.h>

class MarkovPatch : public Patch 
{
  MarkovChain* markov;
  uint16_t listening;
  VoltsPerOctave voct;
  AdsrEnvelope<true>* envelope;

  StereoDcBlockingFilter* dcBlockingFilter;
  AudioBuffer* genBuffer;
  uint16_t resetInSamples;

  SmoothFloat speed;
  SmoothFloat decay;

  float lastLearnLeft, lastLearnRight;
  float lastGenLeft, lastGenRight;

  static const PatchButtonId inToggleListen = BUTTON_1;
  static const PatchButtonId inToggleGenerate = BUTTON_2;

  static const PatchParameterId inWordSize  = PARAMETER_A;
  static const PatchParameterId inDecay     = PARAMETER_B;
  static const PatchParameterId inDryWet    = PARAMETER_D;

  static const PatchParameterId inSpeed     = PARAMETER_G;

  const int minWordSizeSamples;
  const int maxWordSizeSamples;
  const int minDecaySeconds = 0.001f;
  const int maxDecaySeconds = 1.0f;

public: 
  MarkovPatch()
    : listening(OFF), resetInSamples(0), lastLearnLeft(0), lastLearnRight(0)
    , genBuffer(0), lastGenLeft(0), lastGenRight(0), voct(-0.5f, 4)
    , minWordSizeSamples((getSampleRate()*0.008f)), maxWordSizeSamples(getSampleRate()*0.25f)
  {
    markov = MarkovChain::create();

    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);
    genBuffer = AudioBuffer::create(2, getBlockSize());
    envelope = AdsrEnvelope<true>::create(getSampleRate());
    envelope->setAttack(minDecaySeconds);
    envelope->setRelease(minDecaySeconds);

    voct.setTune(-4);
    registerParameter(inWordSize, "Word Size");
    registerParameter(inDryWet, "Dry/Wet");
    registerParameter(inDecay, "Decay");
    registerParameter(inSpeed, "Speed");
  }

  ~MarkovPatch()
  {
    MarkovChain::destroy(markov);
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
    AudioBuffer::destroy(genBuffer);
    AdsrEnvelope<true>::destroy(envelope);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == inToggleListen && value == ON)
    {
      listening = listening == ON ? OFF : ON;
      // when we turn listening off we follow the last sample we learned with zero.
      if (!listening)
      {
        lastLearnLeft = 0;
        lastLearnRight = 0;
        markov->learn(0);
      }
    }
    else if (bid == inToggleGenerate)
    {
      bool gateOpen = value == ON;
      if (gateOpen)
      {
        resetInSamples = samples;
      }
      envelope->gate(gateOpen, samples);
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int inSize = audio.getSize();
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray genLeft = genBuffer->getSamples(0);
    FloatArray genRight = genBuffer->getSamples(1);

    dcBlockingFilter->process(audio, audio);

    if (listening)
    {
      markov->learn(inLeft);

      //markov->setLastLearn(lastLearnRight);
      //markov->learn(right);
      //lastLearnRight = right[right.getSize() - 1];
    }

    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "mem size ");
    debugCpy = stpcpy(debugCpy, msg_itoa(markov->getMemorySize(), 10));
    debugCpy = stpcpy(debugCpy, " avg len ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(markov->getAverageChainLength(), 10));
    debugCpy = stpcpy(debugCpy, " spd ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(speed, 10));
    debugMessage(debugMsg);

    speed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    decay = minDecaySeconds + getParameterValue(inDecay)*(maxDecaySeconds - minDecaySeconds);
    envelope->setRelease(decay);

    int wordSize = minWordSizeSamples + getParameterValue(inWordSize) * (maxWordSizeSamples - minWordSizeSamples);
    markov->setWordSize(wordSize);

    for (int i = 0; i < inSize; ++i)
    {
      genLeft[i] = markov->generate() * envelope->generate();
      if (resetInSamples && --resetInSamples == 0)
      {
        markov->resetGenerate();
      }
    }

    float dryWet = getParameterValue(inDryWet);
    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inLeft.multiply(dryAmt);
    genLeft.multiply(wetAmt);
    inLeft.add(genLeft);
    inLeft.copyTo(inRight);

    setButton(inToggleListen, listening);
  }
  
};
