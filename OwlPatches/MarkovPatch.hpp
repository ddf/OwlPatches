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
#include "basicmaths.h"
#include <string.h>

class ListenEnvelope : public ExponentialAdsrEnvelope
{
  ListenEnvelope(float sr) : ExponentialAdsrEnvelope(sr) {}

public:

  bool isIdle() const { return stage == kIdle; }

  static ListenEnvelope* create(float sr)
  {
    return new ListenEnvelope(sr);
  }

  static void destroy(ListenEnvelope* env)
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

  static const int TAP_TRIGGER_LIMIT = (1 << 17);

  TapTempo* tempo;
  MarkovGenerator* markov;
  uint16_t listening;
  ListenEnvelope* listenEnvelope;
  ExponentialAdsrEnvelope* expoGenerateEnvelope;
  LinearAdsrEnvelope* linearGenerateEnvelope;

  StereoDcBlockingFilter* dcBlockingFilter;
  AudioBuffer* genBuffer;

  int  samplesSinceLastTap;
  int  clocksToReset;
  int  samplesToReset;
  int  wordsToNewInterval;

  SmoothFloat envelopeShape;

  int wordGateLength;
  int wordStartedGate;

  const float attackSeconds = 0.005f;
  const float minDecaySeconds = 0.010f;
  const float maxDecaySeconds = 1.0f;

  const int wordStartedGateLength;
  const int minWordGateLength;
  const int minWordSizeSamples;

public: 
  MarkovPatch()
    : listening(OFF), samplesSinceLastTap(TAP_TRIGGER_LIMIT), clocksToReset(0), samplesToReset(-1), wordsToNewInterval(0), genBuffer(0)
    , wordGateLength(1), wordStartedGate(0), wordStartedGateLength(getSampleRate()*attackSeconds)
    , minWordGateLength((getSampleRate()*attackSeconds)), minWordSizeSamples((getSampleRate()*attackSeconds*2))
  {
    tempo = TapTempo::create(getSampleRate(), TAP_TRIGGER_LIMIT);
    tempo->setBeatsPerMinute(120);

    markov = MarkovGenerator::create(getSampleRate()*4);

    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);

    listenEnvelope = ListenEnvelope::create(getSampleRate());
    listenEnvelope->setAttack(attackSeconds);
    listenEnvelope->setRelease(attackSeconds);

    genBuffer = AudioBuffer::create(2, getBlockSize());
    expoGenerateEnvelope = ExponentialAdsrEnvelope::create(getSampleRate());
    expoGenerateEnvelope->setAttack(attackSeconds);
    expoGenerateEnvelope->setRelease(minDecaySeconds);

    linearGenerateEnvelope = LinearAdsrEnvelope::create(getSampleRate());
    linearGenerateEnvelope->setAttack(attackSeconds);
    linearGenerateEnvelope->setRelease(minDecaySeconds);

    registerParameter(inWordSize, "Word Size");
    registerParameter(inWordSizeVariation, "Word Size Variation");
    registerParameter(inDryWet, "Dry/Wet");
    registerParameter(inDecay, "Decay");
    registerParameter(outWordProgress, "Word>");
    registerParameter(outDecayEnvelope, "Envelope>");

    setParameterValue(inWordSizeVariation, 0.5f);
  }

  ~MarkovPatch()
  {
    TapTempo::destroy(tempo);
    MarkovGenerator::destroy(markov);
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
    AudioBuffer::destroy(genBuffer);
    ListenEnvelope::destroy(listenEnvelope);
    ExponentialAdsrEnvelope::destroy(expoGenerateEnvelope);
    LinearAdsrEnvelope::destroy(linearGenerateEnvelope);
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

      samplesSinceLastTap = -samples;

      // don't reset when doing full random variation
      if (on && getParameterValue(inWordSizeVariation) < 0.53f && clocksToReset == 0)
      {
        samplesToReset = samples;
      }

      if (on && clocksToReset > 0)
      {
        --clocksToReset;
      }
    }
  }

  void setEnvelopeRelease(int wordSize)
  {
    if (envelopeShape >= 0.99f)
    {
      wordGateLength = wordSize;
    }
    else if (envelopeShape >= 0.53f)
    {
      float t = (envelopeShape - 0.53f) * 2.12f;
      wordGateLength = Interpolator::linear(minWordGateLength, wordSize - minWordGateLength, t);
    }
    else
    {
      wordGateLength = minWordSizeSamples;
    }
    float wordReleaseSeconds = (float)(wordSize - wordGateLength) / getSampleRate();
    expoGenerateEnvelope->setRelease(wordReleaseSeconds);
    linearGenerateEnvelope->setRelease(wordReleaseSeconds);
  }

  void updateEnvelope()
  {
    bool state = markov->getLetterCount() < wordGateLength;
    expoGenerateEnvelope->gate(state);
    linearGenerateEnvelope->gate(state);

    expoGenerateEnvelope->generate();
    linearGenerateEnvelope->generate();
  }

  float getEnvelopeLevel()
  {
    float expo = expoGenerateEnvelope->getLevel();
    float line = linearGenerateEnvelope->getLevel();
    if (envelopeShape <= 0.47f)
    {
      float t = (0.47f - envelopeShape) * 2.12f;
      return Interpolator::linear(line, expo, t);
    }
    return line;
  }

  void updateWordSettings()
  {
    static const int divMultLen = 7;
    static const float divMult[divMultLen] = { 1.0f / 4, 1.0f / 3, 1.0f / 2, 1, 2, 3, 4 };
    static const int intervalsLen = 7;
    static const float intervals[intervalsLen] = { 1.0f / 3, 1.0f / 4, 1.0f / 2, 1, 2, 4, 3 };
    static const int counters[divMultLen][intervalsLen] = {
      // intervals: 1/3  1/4  1/2  1  2  4   3   |    divmult
                   { 1,   1,   1,  1, 1, 1,  3   }, // 1/4
                   { 1,   1,   1,  1, 1, 4,  1   }, // 1/3
                   { 1,   1,   1,  1, 1, 2,  3   }, // 1/2
                   { 1,   1,   1,  1, 2, 4,  3   }, // 1
                   { 2,   1,   1,  2, 4, 8,  6   }, // 2
                   { 1,   3,   3,  3, 6, 12, 9   }, // 3
                   { 4,   1,   2,  4, 8, 16, 12  }, // 4
    };

    float divMultT = Interpolator::linear(0, divMultLen - 1, getParameterValue(inWordSize));
    bool smoothDivMult = samplesSinceLastTap >= TAP_TRIGGER_LIMIT;
    int divMultIdx = smoothDivMult ? (int)divMultT 
                                   : (int)roundf(divMultT);
    int intervalIdx = 3;
    float wordScale = smoothDivMult ? Interpolator::linear(divMult[divMultIdx], divMult[divMultIdx+1], divMultT - divMultIdx)
                                    : divMult[divMultIdx];

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

    // random variation up to 8 times longer or 8 times shorter
    if (wordVariationParam >= 0.53f)
    {
      float scale = Interpolator::linear(1, 4, randf()*varyAmt);
      // weight towards shorter
      if (randf() > 0.25f) scale = 1.0f / scale;
      wordScale *= scale;
      wordsToNewInterval = 1;
    }
    // random variation using musical mult/divs of the current word size
    else if (wordVariationParam <= 0.47f)
    {
      // when varyAmt is zero, we want the interval in the middle of the array (ie 1).
      // so we offset from 0.5f with a random value between -0.5 and 0.5, scaled by varyAmt
      // (ie as vary amount gets larger we can pick values at closer to the ends of the array.
      intervalIdx = Interpolator::linear(0, intervalsLen - 1, 0.5f + (randf() - 0.5f)*varyAmt);
      float interval = intervals[intervalIdx];
      wordScale *= interval;
      if (interval < 1)
      {
        wordsToNewInterval = (int)(1.0f / interval);
      }
    }
    else
    {
      wordsToNewInterval = 1;
    }

    int wordSize = std::max(minWordSizeSamples, (int)(tempo->getPeriodInSamples() * wordScale));
    clocksToReset = counters[divMultIdx][intervalIdx] - 1;

    markov->setWordSize(wordSize);
    setEnvelopeRelease(wordSize);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int inSize = audio.getSize();
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray genLeft = genBuffer->getSamples(0);
    FloatArray genRight = genBuffer->getSamples(1);

    tempo->clock(inSize);
    if (samplesSinceLastTap < TAP_TRIGGER_LIMIT)
    {
      samplesSinceLastTap += getBlockSize();
    }

    dcBlockingFilter->process(audio, audio);

    for (int i = 0; i < inSize; ++i)
    {
      // need to generate even if we don't use the value otherwise internal state won't update
      const float env = listenEnvelope->generate();
      if (!listenEnvelope->isIdle())
      {
        markov->learn(ComplexFloat(inLeft[i]*env, inRight[i]*env));
      }
    }

    int wordStartedGateDelay = 0;
    if (wordStartedGate > 0)
    {
      if (wordStartedGate < getBlockSize())
      {
        wordStartedGateDelay = wordStartedGate;
      }
      wordStartedGate -= getBlockSize();
    }

    envelopeShape = getParameterValue(inDecay);

    for (int i = 0; i < inSize; ++i)
    {
      if (samplesToReset == 0)
      {
        markov->resetWord();
      }

      if (samplesToReset >= 0)
      {
        --samplesToReset;
      }

      // word going to start, update the word size, envelope settings
      if (markov->getLetterCount() == 0)
      {
        if (wordsToNewInterval > 0)
        {
          --wordsToNewInterval;
        }

        if (wordsToNewInterval == 0)
        {
          updateWordSettings();
        }

        wordStartedGate = wordStartedGateLength;
        wordStartedGateDelay = i;
      }

      updateEnvelope();

      ComplexFloat sample = markov->generate() * getEnvelopeLevel();
      genLeft[i] = sample.re;
      genRight[i] = sample.im;
    }

    float dryWet = std::clamp(getParameterValue(inDryWet)*1.02f, 0.0f, 1.0f);
    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inLeft.multiply(dryAmt);
    inRight.multiply(dryAmt);
    genLeft.multiply(wetAmt);
    genRight.multiply(wetAmt);
    inLeft.add(genLeft);
    inRight.add(genRight);

    setButton(inToggleListen, listening);
    setButton(outWordEnded, wordStartedGate > 0, wordStartedGateDelay);
    setParameterValue(outWordProgress, (float)markov->getLetterCount() / markov->getCurrentWordSize());
    setParameterValue(outDecayEnvelope, getEnvelopeLevel());
    //setParameterValue(outDecayEnvelope, (float)clocksToReset / 16);

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
    debugCpy = stpcpy(debugCpy, " C ");
    debugCpy = stpcpy(debugCpy, msg_itoa(clocksToReset, 10));
    debugCpy = stpcpy(debugCpy, " w ");
    debugCpy = stpcpy(debugCpy, msg_itoa(int((float)markov->getCurrentWordSize() / getSampleRate() * 1000), 10));
    debugMessage(debugMsg);
  }
};
