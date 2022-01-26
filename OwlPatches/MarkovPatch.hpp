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

class DecayEnvelope : public ExponentialAdsrEnvelope
{
  DecayEnvelope(int sr) : ExponentialAdsrEnvelope(sr) {}

public:

  bool isIdle() const { return stage == kIdle; }

  static DecayEnvelope* create(int sr)
  {
    return new DecayEnvelope(sr);
  }

  static void destroy(DecayEnvelope* env)
  {
    delete env;
  }
};

class MarkovPatch : public Patch 
{
  typedef ComplexShortMarkovGenerator MarkovGenerator;

  static const PatchButtonId inToggleListen = BUTTON_1;
  static const PatchButtonId inToggleGenerate = BUTTON_2;
  static const PatchButtonId outWordEnded = PUSHBUTTON;

  static const PatchParameterId inWordSize = PARAMETER_A;
  static const PatchParameterId inDecay = PARAMETER_B;
  static const PatchParameterId inDryWet = PARAMETER_D;

  static const PatchParameterId outDecayEnvelope = PARAMETER_F;

  static const PatchParameterId inSpeed = PARAMETER_G;

  MarkovGenerator* markov;
  uint16_t listening;
  VoltsPerOctave voct;
  DecayEnvelope* listenEnvelope;
  DecayEnvelope* generateEnvelope;

  StereoDcBlockingFilter* dcBlockingFilter;
  AudioBuffer* genBuffer;
  uint16_t resetInSamples;

  SmoothFloat speed;
  SmoothFloat decay;

  float lastLearnLeft, lastLearnRight;
  float lastGenLeft, lastGenRight;

  int wordEndedGate;

  const int wordEndedGateLength;
  const int minWordSizeSamples;
  const int maxWordSizeSamples;
  const float attackSeconds = 0.008f;
  const float minDecaySeconds = 0.016f;
  const float maxDecaySeconds = 1.0f;

public: 
  MarkovPatch()
    : listening(OFF), resetInSamples(0), lastLearnLeft(0), lastLearnRight(0)
    , genBuffer(0), lastGenLeft(0), lastGenRight(0), voct(-0.5f, 4)
    , wordEndedGate(0), wordEndedGateLength(getSampleRate()*attackSeconds)
    , minWordSizeSamples((getSampleRate()*attackSeconds)), maxWordSizeSamples(getSampleRate()*0.25f)
  {
    markov = MarkovGenerator::create(getSampleRate()*4);

    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);

    listenEnvelope = DecayEnvelope::create(getSampleRate());
    listenEnvelope->setAttack(attackSeconds);
    listenEnvelope->setRelease(attackSeconds);

    genBuffer = AudioBuffer::create(2, getBlockSize());
    generateEnvelope = DecayEnvelope::create(getSampleRate());
    generateEnvelope->setAttack(attackSeconds);
    generateEnvelope->setRelease(minDecaySeconds);

    voct.setTune(-4);
    registerParameter(inWordSize, "Word Size");
    registerParameter(inDryWet, "Dry/Wet");
    registerParameter(inDecay, "Decay");
    registerParameter(inSpeed, "Speed");
    registerParameter(outDecayEnvelope, "Envelope>");
  }

  ~MarkovPatch()
  {
    MarkovGenerator::destroy(markov);
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
    AudioBuffer::destroy(genBuffer);
    DecayEnvelope::destroy(listenEnvelope);
    DecayEnvelope::destroy(generateEnvelope);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == inToggleListen && value == ON)
    {
      listening = listening == ON ? OFF : ON;
      listenEnvelope->gate(listening == ON, samples);
    }
    else if (bid == inToggleGenerate)
    {
      bool gateOpen = value == ON;
      if (gateOpen)
      {
        // +1 to samples because we want to reset even if samples is zero.
        // in our generate loop we check for a non-zero value before decrementing.
        resetInSamples = samples+1;
      }
      generateEnvelope->gate(gateOpen, samples);
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

    for (int i = 0; i < inSize; ++i)
    {
      // need to generate even if we don't use the value otherwise internal state won't update
      const float env = listenEnvelope->generate();
      if (!listenEnvelope->isIdle())
      {
        markov->learn(ComplexFloat(inLeft[i]*env, inRight[i]*env));
      }
      //markov->learn(inLeft[i]);
    }

    int wordEndedGateDelay = 0;
    if (wordEndedGate > 0)
    {
      if (wordEndedGate < getBlockSize())
      {
        wordEndedGateDelay = wordEndedGate;
      }
      wordEndedGate -= getBlockSize();
    }

    // will a word end this block?
    const int samplesUntilWordEnd = markov->getCurrentWordSize() - markov->getLetterCount();
    if (samplesUntilWordEnd <= getBlockSize())
    {
      wordEndedGate = wordEndedGateLength;
      wordEndedGateDelay = samplesUntilWordEnd;
    }

    speed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    decay = minDecaySeconds + getParameterValue(inDecay)*(maxDecaySeconds - minDecaySeconds);
    generateEnvelope->setRelease(decay);

    int wordSize = minWordSizeSamples + getParameterValue(inWordSize) * (maxWordSizeSamples - minWordSizeSamples);
    markov->setWordSize(wordSize);

    for (int i = 0; i < inSize; ++i)
    {
      if (resetInSamples && --resetInSamples == 0)
      {
        markov->resetGenerate();
      }

      ComplexFloat sample = markov->generate() * generateEnvelope->generate();
      genLeft[i] = sample.re;
      genRight[i] = sample.im;
      //genLeft[i] = markov->generate() * envelope->generate();
      //genRight[i] = genLeft[i];
    }

    float dryWet = getParameterValue(inDryWet);
    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inLeft.multiply(dryAmt);
    inRight.multiply(dryAmt);
    genLeft.multiply(wetAmt);
    genRight.multiply(wetAmt);
    inLeft.add(genLeft);
    inRight.add(genRight);

    setButton(inToggleListen, listening);
    setButton(outWordEnded, wordEndedGate > 0, wordEndedGateDelay);
    setParameterValue(outDecayEnvelope, generateEnvelope->getLevel());

    MarkovGenerator::Stats stats = markov->getStats();
    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "nodes ");
    debugCpy = stpcpy(debugCpy, msg_itoa(stats.memorySize, 10));
    debugCpy = stpcpy(debugCpy, " min ");
    debugCpy = stpcpy(debugCpy, msg_itoa(stats.minChainLength, 10));
    debugCpy = stpcpy(debugCpy, "(");
    debugCpy = stpcpy(debugCpy, msg_itoa(stats.minChainCount, 10));
    debugCpy = stpcpy(debugCpy, ") max ");
    debugCpy = stpcpy(debugCpy, msg_itoa(stats.maxChainLength, 10));
    debugCpy = stpcpy(debugCpy, "(");
    debugCpy = stpcpy(debugCpy, msg_itoa(stats.maxChainCount, 10));
    debugCpy = stpcpy(debugCpy, ") avg ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(stats.avgChainLength, 10));
    debugCpy = stpcpy(debugCpy, " dcy ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(decay, 10));
    debugCpy = stpcpy(debugCpy, " wrd ");
    debugCpy = stpcpy(debugCpy, msg_itoa(int((float)wordSize / getSampleRate() * 1000), 10));
    debugMessage(debugMsg);
  }
  
};
