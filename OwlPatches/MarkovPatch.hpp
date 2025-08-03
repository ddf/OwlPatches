/**

AUTHOR:
    (c) 2022-2025 Damien Quartz

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
    Markov is essentially a "smart" granular synthesizer that plays 
    only one grain at a time and chooses the starting sample of each 
    grain based on the last sample of the previous grain.

    Teach the patch how to generate sound by engaging Listen mode
    by pressing Button 1 or sending a trigger to Gate 1. 
    While listening, the patch records to a four second "memory" 
    and analyzes it using a 1-dimensional Markov Chain. The patch
    will "forget" old sound when more than four seconds are recorded.

    Sound is continuously generated based on what has been learned
    with control over the "word" size, which is analogous to grain size 
    in a typical granular synthesizer.  Each word has an envelope 
    applied to it, which can be morphed from an exponential decay, 
    to a linear decay, to a box car.  When the envelope shape parameter 
    is turned all the way up, the envelope is kept open at all times, 
    putting the patch into a kind of pure synthesis mode where word size 
    becomes less obvious.

    The default word size with the word size parameter at 0.5 is half 
    a second and can be increased to two seconds and decreased to 
    an eighth of a second. The word size can also be set by tapping 
    a tempo on Button 2 or by sending clock to Gate 2. While receiving 
    clock at Gate 2, the word size parameter will divide or multiply 
    the word size by musical durations.

    Random variation can be added to the word size with the word variation
    parameter. Below 0.5 only musical divisions and multiplications
    are allowed, increasing in range as the parameter moves towards zero.
    Above 0.5 the variation is totally random, increasing in range 
    as the parameter moves towards one.
*/

#pragma once

#include "MonochromeScreenPatch.h"
#include "PatchParameterDescription.h"
#include "DcBlockingFilter.h"
#include "Adsr.h"
#include "Easing.h"
#include "TapTempo.h"
#include "basicmaths.h"

#include "MarkovChain.hpp"

static constexpr PatchButtonId IN_TOGGLE_LISTEN = BUTTON_1;
static constexpr PatchButtonId IN_CLOCK = BUTTON_2;
static constexpr PatchButtonId OUT_WORD_ENDED = OUT_GATE_1;

static constexpr PatchParameterId IN_WORD_SIZE = PARAMETER_A;
static constexpr PatchParameterId IN_DECAY = PARAMETER_B;
static constexpr PatchParameterId IN_WORD_SIZE_VARIATION = PARAMETER_C;
static constexpr PatchParameterId IN_DRY_WET = PARAMETER_D;

static constexpr PatchParameterId OUT_WORD_PROGRESS = OUT_PARAMETER_A;
static constexpr PatchParameterId OUT_DECAY_ENVELOPE = OUT_PARAMETER_B;

constexpr float ATTACK_SECONDS    = 0.005f;
constexpr float MIN_DECAY_SECONDS = 0.010f;
constexpr float MAX_DECAY_SECONDS = 1.0f;

static constexpr int TAP_TRIGGER_LIMIT = (1 << 17);

// forward declaration for constructor use below to suppress warning
template<>
SmoothValue<float>::SmoothValue(float lambda);

class MarkovPatch final : public MonochromeScreenPatch  // NOLINT(cppcoreguidelines-special-member-functions)
{
  typedef ComplexFloatMarkovGenerator MarkovGenerator;

  TapTempo        tempo;
  ExponentialAdsr listenEnvelope;
  ExponentialAdsr expoGenerateEnvelope;
  LinearAdsr      linearGenerateEnvelope;

  AudioBuffer* genBuffer;
  StereoDcBlockingFilter* dcBlockingFilter;
  MarkovGenerator* markov;
  
  int samplesSinceLastTap;
  int clocksToReset;
  int samplesToReset;
  int wordsToNewInterval;
  int wordGateLength;
  int wordStartedGate;
  int wordStartedGateLength;
  int minWordGateLength;
  int minWordSizeSamples;

  SmoothFloat envelopeShape;

  uint16_t listening;

public: 
  MarkovPatch()
    : tempo(getSampleRate(), TAP_TRIGGER_LIMIT)
    , listenEnvelope(getSampleRate()), expoGenerateEnvelope(getSampleRate()), linearGenerateEnvelope(getSampleRate())
    , genBuffer(nullptr), dcBlockingFilter(nullptr), markov(nullptr)
    , samplesSinceLastTap(TAP_TRIGGER_LIMIT), clocksToReset(0), samplesToReset(-1), wordsToNewInterval(0), wordGateLength(1)
    , wordStartedGate(0), wordStartedGateLength(static_cast<int>(getSampleRate()*ATTACK_SECONDS)), minWordGateLength(static_cast<int>(getSampleRate()*ATTACK_SECONDS))
    , minWordSizeSamples(static_cast<int>(getSampleRate()*ATTACK_SECONDS*2)), envelopeShape(0.9f), listening(OFF)
  {
    tempo.setBeatsPerMinute(120);

    genBuffer = AudioBuffer::create(2, getBlockSize());
    markov = MarkovGenerator::create(static_cast<int>(getSampleRate()*4));
    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);
    
    listenEnvelope.setAttack(ATTACK_SECONDS);
    listenEnvelope.setRelease(ATTACK_SECONDS);
    
    expoGenerateEnvelope.setAttack(ATTACK_SECONDS);
    expoGenerateEnvelope.setRelease(MIN_DECAY_SECONDS);
    
    linearGenerateEnvelope.setAttack(ATTACK_SECONDS);
    linearGenerateEnvelope.setRelease(MIN_DECAY_SECONDS);

    registerParameter(IN_WORD_SIZE, "Word Size");
    registerParameter(IN_WORD_SIZE_VARIATION, "Word Size Variation");
    registerParameter(IN_DRY_WET, "Dry/Wet");
    registerParameter(IN_DECAY, "Decay");
    registerParameter(OUT_WORD_PROGRESS, "Word>");
    registerParameter(OUT_DECAY_ENVELOPE, "Envelope>");

    setParameterValue(IN_WORD_SIZE, 0.5f);
    setParameterValue(IN_WORD_SIZE_VARIATION, 0.5f);
  }

  ~MarkovPatch() override
  {
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
    MarkovGenerator::destroy(markov);
    AudioBuffer::destroy(genBuffer);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == IN_TOGGLE_LISTEN && value == Patch::ON)
    {
      listening = listening == Patch::ON ? OFF : Patch::ON;
      listenEnvelope.gate(listening == ON, samples);
    }
    else if (bid == IN_CLOCK)
    {
      const bool on = value == ON;
      tempo.trigger(on, samples);

      samplesSinceLastTap = -samples;

      // don't reset when doing full random variation
      if (on && getParameterValue(IN_WORD_SIZE_VARIATION) < 0.53f && clocksToReset == 0)
      {
        samplesToReset = samples;
      }

      if (on && clocksToReset > 0)
      {
        --clocksToReset;
      }
    }
  }

  void setEnvelopeRelease(const int wordSize)
  {
    if (envelopeShape >= 0.99f)
    {
      wordGateLength = wordSize;
    }
    else if (envelopeShape >= 0.53f)
    {
      float t = (envelopeShape - 0.53f) * 2.12f;
      wordGateLength = static_cast<int>(Easing::interp(static_cast<float>(minWordGateLength), static_cast<float>(wordSize - minWordGateLength), t));
    }
    else
    {
      wordGateLength = minWordSizeSamples;
    }
    const float wordReleaseSeconds = static_cast<float>(wordSize - wordGateLength) / getSampleRate();
    expoGenerateEnvelope.setRelease(wordReleaseSeconds);
    linearGenerateEnvelope.setRelease(wordReleaseSeconds);
  }

  void updateEnvelope()
  {
    const bool state = markov->chain().getLetterCount() < wordGateLength;
    expoGenerateEnvelope.gate(state);
    linearGenerateEnvelope.gate(state);

    expoGenerateEnvelope.generate();
    linearGenerateEnvelope.generate();
  }

  float getEnvelopeLevel()
  {
    const float expo = expoGenerateEnvelope.getLevel();
    const float line = linearGenerateEnvelope.getLevel();
    if (envelopeShape <= 0.47f)
    {
      const float t = (0.47f - envelopeShape) * 2.12f;
      return Easing::interp(line, expo, t);
    }
    return line;
  }

  void updateWordSettings()
  {
    static constexpr int DIV_MULT_LEN = 7;
    static constexpr float DIV_MULT[DIV_MULT_LEN] = { 1.0f / 4, 1.0f / 3, 1.0f / 2, 1, 2, 3, 4 };
    static constexpr int INTERVALS_LEN = 7;
    static constexpr float INTERVALS[INTERVALS_LEN] = { 1.0f / 3, 1.0f / 4, 1.0f / 2, 1, 2, 4, 3 };
    static const int COUNTERS[DIV_MULT_LEN][INTERVALS_LEN] = {
      // intervals:    1/3  1/4  1/2  1  2  4   3   |    divMult
                   { 1,   1,   1,  1, 1, 1,  3   }, // 1/4
                   { 1,   1,   1,  1, 1, 4,  1   }, // 1/3
                   { 1,   1,   1,  1, 1, 2,  3   }, // 1/2
                   { 1,   1,   1,  1, 2, 4,  3   }, // 1
                   { 2,   1,   1,  2, 4, 8,  6   }, // 2
                   { 1,   3,   3,  3, 6, 12, 9   }, // 3
                   { 4,   1,   2,  4, 8, 16, 12  }, // 4
    };

    const float divMultT = Easing::interp(0, DIV_MULT_LEN - 1, getParameterValue(IN_WORD_SIZE));
    const bool smoothDivMult = samplesSinceLastTap >= TAP_TRIGGER_LIMIT;
    const int divMultIdx = smoothDivMult ? static_cast<int>(divMultT) : static_cast<int>(roundf(divMultT));
    int intervalIdx = 3;
    float wordScale = smoothDivMult ? Easing::interp(DIV_MULT[divMultIdx], DIV_MULT[divMultIdx+1], divMultT - static_cast<float>(divMultIdx))
                                    : DIV_MULT[divMultIdx];

    const float wordVariationParam = getParameterValue(IN_WORD_SIZE_VARIATION);
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

    // smooth random variation
    if (wordVariationParam >= 0.53f)
    {
      float scale = Easing::interp(1, 4, randf()*varyAmt);
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
      // (ie as vary amount gets larger we can pick values at closer to the ends of the array).
      intervalIdx = static_cast<int>(Easing::interp(0, INTERVALS_LEN - 1, 0.5f + (randf() - 0.5f) * varyAmt));
      const float interval = INTERVALS[intervalIdx];
      wordScale *= interval;
      if (interval < 1)
      {
        wordsToNewInterval = static_cast<int>(1.0f / interval);
      }
    }
    else
    {
      wordsToNewInterval = 1;
    }

    const int wordSize = std::max(minWordSizeSamples, static_cast<int>(static_cast<float>(tempo.getPeriodInSamples()) * wordScale));
    clocksToReset = COUNTERS[divMultIdx][intervalIdx] - 1;

    markov->chain().setWordSize(wordSize);
    setEnvelopeRelease(wordSize);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int inSize = audio.getSize();
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray genLeft = genBuffer->getSamples(0);
    FloatArray genRight = genBuffer->getSamples(1);

    tempo.clock(inSize);
    if (samplesSinceLastTap < TAP_TRIGGER_LIMIT)
    {
      samplesSinceLastTap += getBlockSize();
    }

    dcBlockingFilter->process(audio, audio);

    for (int i = 0; i < inSize; ++i)
    {
      // need to generate even if we don't use the value otherwise internal state won't update
      const float env = listenEnvelope.generate();
      if (!listenEnvelope.isIdle())
      {
        markov->learn(ComplexFloat(inLeft[i]*env, inRight[i]*env));
      }
    }

    int wordStartedGateDelay = 0;
    if (wordStartedGate > 0)
    {
      if (wordStartedGate < getBlockSize())
      {
        wordStartedGateDelay = static_cast<uint16_t>(wordStartedGate);
      }
      wordStartedGate -= getBlockSize();
    }

    envelopeShape = getParameterValue(IN_DECAY);

    for (int i = 0; i < inSize; ++i)
    {
      if (samplesToReset == 0)
      {
        markov->chain().resetWord();
      }

      if (samplesToReset >= 0)
      {
        --samplesToReset;
      }

      // word going to start, update the word size, envelope settings
      if (markov->chain().getLetterCount() == 0)
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

      const ComplexFloat sample = markov->generate() * getEnvelopeLevel();
      genLeft[i] = sample.re;
      genRight[i] = sample.im;
    }

    const float dryWet = clamp(getParameterValue(IN_DRY_WET)*1.02f, 0.0f, 1.0f);
    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inLeft.multiply(dryAmt);
    inRight.multiply(dryAmt);
    genLeft.multiply(wetAmt);
    genRight.multiply(wetAmt);
    inLeft.add(genLeft);
    inRight.add(genRight);

#if defined(OWL_LICH)
    setButton(inToggleListen, listening);
#endif
    setButton(OUT_WORD_ENDED, wordStartedGate > 0, static_cast<uint16_t>(wordStartedGateDelay));
    setParameterValue(OUT_WORD_PROGRESS, markov->chain().getWordProgress());
    // setting exactly 1.0 on an output parameter causes a glitch on Genius, so we scale down our envelope value a little bit
    setParameterValue(OUT_DECAY_ENVELOPE, getEnvelopeLevel()*0.98f);
    //setParameterValue(outDecayEnvelope, (float)clocksToReset / 16);
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const MarkovGenerator::Chain::Stats stats = markov->chain().getStats();
    screen.setCursor(0, 8);
    screen.print("n ");
    screen.print(stats.memorySize);
    screen.print("\n min ");
    screen.print(stats.minChainLength);
    screen.print("(");
    screen.print(stats.minChainCount);
    screen.print(")\n max ");
    screen.print(stats.maxChainLength);
    screen.print("(");
    screen.print(stats.maxChainCount);
    screen.print(")\n avg ");
    screen.print(stats.avgChainLength);
    screen.print("\n Wms " );
    const int wordMs = static_cast<int>(static_cast<float>(markov->chain().getCurrentWordSize()) / getSampleRate() * 1000);
    screen.print(wordMs);
    screen.print("\n C ");
    screen.print(clocksToReset);
  }
};
