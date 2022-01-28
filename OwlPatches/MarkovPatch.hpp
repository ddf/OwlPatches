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
#include "Interpolator.h"
#include "MarkovChain.hpp"
#include "TapTempo.h"
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
  static const PatchButtonId inClock = BUTTON_2;
  static const PatchButtonId outWordEnded = PUSHBUTTON;

  static const PatchParameterId inWordSize = PARAMETER_A;
  static const PatchParameterId inDecay = PARAMETER_B;
  static const PatchParameterId inWordSizeVariation = PARAMETER_C;
  static const PatchParameterId inDryWet = PARAMETER_D;

  static const PatchParameterId outWordProgress = PARAMETER_F;
  static const PatchParameterId outDecayEnvelope = PARAMETER_G;

  static const PatchParameterId inSpeed = PARAMETER_G;

  static const int TAP_TRIGGER_LIMIT = (1 << 17);

  TapTempo* tempo;
  MarkovGenerator* markov;
  uint16_t listening;
  VoltsPerOctave voct;
  DecayEnvelope* listenEnvelope;
  DecayEnvelope* generateEnvelope;

  StereoDcBlockingFilter* dcBlockingFilter;
  AudioBuffer* genBuffer;
  
  bool genState;
  uint16_t samplesToGenStateChange;

  SmoothFloat speed;
  SmoothFloat decay;

  float lastLearnLeft, lastLearnRight;
  float lastGenLeft, lastGenRight;

  int wordEndedGate;

  const float attackSeconds = 0.005f;
  const float minDecaySeconds = 0.010f;
  const float maxDecaySeconds = 1.0f;

  const int wordEndedGateLength;
  const int minWordSizeSamples;
  const int maxWordSizeSamples;

public: 
  MarkovPatch()
    : listening(OFF), samplesToGenStateChange(-1), lastLearnLeft(0), lastLearnRight(0)
    , genBuffer(0), lastGenLeft(0), lastGenRight(0), voct(-0.5f, 4)
    , wordEndedGate(0), wordEndedGateLength(getSampleRate()*attackSeconds)
    , minWordSizeSamples((getSampleRate()*attackSeconds)), maxWordSizeSamples(getSampleRate()*0.25f)
  {
    tempo = TapTempo::create(getSampleRate(), TAP_TRIGGER_LIMIT);
    tempo->setBeatsPerMinute(120);

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
    registerParameter(inWordSizeVariation, "Word Size Variation");
    registerParameter(inDryWet, "Dry/Wet");
    registerParameter(inDecay, "Decay");
    registerParameter(inSpeed, "Speed");
    registerParameter(outWordProgress, "Word>");
    registerParameter(outDecayEnvelope, "Envelope>");

    setParameterValue(inWordSizeVariation, 0.5f);
    setParameterValue(inSpeed, 0.5f);
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
    else if (bid == inClock)
    {
      bool on = value == ON;
      tempo->trigger(on, samples);

      if (on)
      {
        samplesToGenStateChange = samples;
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int inSize = audio.getSize();
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray genLeft = genBuffer->getSamples(0);
    FloatArray genRight = genBuffer->getSamples(1);

    tempo->clock(inSize);

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

    speed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    decay = minDecaySeconds + getParameterValue(inDecay)*(maxDecaySeconds - minDecaySeconds);
    generateEnvelope->setRelease(decay);

    int wordSizeParam = minWordSizeSamples + getParameterValue(inWordSize) * (maxWordSizeSamples - minWordSizeSamples);
    // test tempo: lock word size to clock tick length
    wordSizeParam = tempo->getPeriodInSamples();

    float wordVariationParam = getParameterValue(inWordSizeVariation);
    float varyAmt = 0;
    // maps parameter to [0,1) weight above and below a dead-zone in the center
    if (wordVariationParam >= 0.53f)
    {
      varyAmt = (wordVariationParam - 0.53f) * 2.12f;
    }
    else if (wordVariationParam <= 0.47f)
    {
      varyAmt = (0.47f - wordVariationParam) * 2.12f;
    }

    for (int i = 0; i < inSize; ++i)
    {
      if (samplesToGenStateChange == 0)
      {
        markov->resetGenerate();
      }

      if (samplesToGenStateChange >= 0)
      {
        --samplesToGenStateChange;
      }

      // word going to start, update the word size
      if (markov->getLetterCount() == 0)
      {
        // random variation over the full value of the parameter
        if (wordVariationParam > 0.5f)
        {
          int range = Interpolator::linear(0, maxWordSizeSamples - minWordSizeSamples, randf()*varyAmt);
          if (randf() > 0.5f) range *= -1;
          int wordSize = std::max(minWordSizeSamples, wordSizeParam + range);
          markov->setWordSize(wordSize);
        }
        // random variation using musical mult/divs of the current word size
        else
        {
          static float intervals[] = { 1, 2, 2, 4, 4, 3, 3 };
          int idx = Interpolator::linear(0, 7, randf()*varyAmt);
          float interval = intervals[idx];
          if (randf() > 0.5f) interval = 1.0f / interval;
          int wordSize = std::max(minWordSizeSamples, (int)(wordSizeParam * interval));
          markov->setWordSize(wordSize);
        }
      }
      // word about to end, set the gate
      else if (markov->getLetterCount() == markov->getCurrentWordSize() - 1)
      {
        wordEndedGate = wordEndedGateLength;
        wordEndedGateDelay = i;
      }

      generateEnvelope->gate(markov->getLetterCount() < minWordSizeSamples);

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
    setParameterValue(outWordProgress, (float)markov->getLetterCount() / markov->getCurrentWordSize());
    setParameterValue(outDecayEnvelope, generateEnvelope->getLevel());

    MarkovGenerator::Stats stats = markov->getStats();
    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "n ");
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
    debugCpy = stpcpy(debugCpy, " d ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(decay, 10));
    debugCpy = stpcpy(debugCpy, " w ");
    debugCpy = stpcpy(debugCpy, msg_itoa(int((float)wordSizeParam / getSampleRate() * 1000), 10));
    debugMessage(debugMsg);
  }
  
};
