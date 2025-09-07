#pragma once

#include "vessl/vessl.h"
#include "MarkovGenerator.h"

using vessl::unit;
using vessl::unitProcessor;
using vessl::parameter;
using vessl::array;
using vessl::clockable;
using Slew = vessl::slew<float>;
using Smoother = vessl::smoother<float>;
using Asr = vessl::asr<float>;

template<typename T, typename H>
class Markov final : public unitProcessor<T>, public clockable
{
  static constexpr int    CLOCK_PERIOD_MAX  = (1 << 17);
  static constexpr float  ATTACK_SECONDS    = 0.005f;
  static constexpr float  MIN_DECAY_SECONDS = 0.010f;
  
  unit::init<7> init = {
    "markov",
    {
      parameter("listen", parameter::type::binary),
      parameter("word size", parameter::type::analog),
      parameter("variation", parameter::type::analog),
      parameter("decay", parameter::type::analog),
      parameter("progress", parameter::type::analog),
      parameter("envelope", parameter::type::analog),
      parameter("word started", parameter::type::binary),
    }
  };

  Slew listenEnvelope;
  Smoother decaySmoother;
  Asr expoGenerateEnvelope;
  Asr linearGenerateEnvelope;
  MarkovGenerator<T, H> generator;

  float envelopeShape;
  int samplesSinceLastTock;
  int clocksToReset;
  int samplesToReset;
  int wordsToNewInterval;
  int wordGateLength;
  int wordStartedGate;
  int wordStartedGateLength;
  int minWordGateLength;
  int minWordSizeSamples;
  
  parameter& progressOut() { return init.params[4]; }
  parameter& envelopeOut() { return init.params[5]; }
  parameter& wordStartedOut() { return init.params[6]; }
  
public:
  Markov(float sampleRate, size_t bufferSize) : unitProcessor<T>(init, sampleRate)
  , clockable(sampleRate, 16, CLOCK_PERIOD_MAX, 120)
  , listenEnvelope(sampleRate, 5, 5), decaySmoother(0.9f, MIN_DECAY_SECONDS)
  , expoGenerateEnvelope(ATTACK_SECONDS, MIN_DECAY_SECONDS, sampleRate), linearGenerateEnvelope(ATTACK_SECONDS, MIN_DECAY_SECONDS, sampleRate)
  , generator(sampleRate, bufferSize)
  , samplesSinceLastTock(CLOCK_PERIOD_MAX), clocksToReset(0), samplesToReset(-1), wordsToNewInterval(0)
  , wordGateLength(1), wordStartedGate(0), wordStartedGateLength(static_cast<int>(sampleRate*ATTACK_SECONDS))
  , minWordGateLength(static_cast<int>(sampleRate*ATTACK_SECONDS)), minWordSizeSamples(static_cast<int>(sampleRate*ATTACK_SECONDS*2))
  {
    decay() << MIN_DECAY_SECONDS;
  }

  // when processing, if listen is greater than 1, this is interpreted as a time-delayed gate
  parameter& listen() { return init.params[0]; }
  parameter& wordSize() { return init.params[1]; }
  parameter& variation() { return init.params[2]; }
  parameter& decay() { return init.params[3]; }

  // outputs
  const parameter& progress() const { return init.params[4]; }
  const parameter& envelope() const { return init.params[5]; }
  const parameter& wordStarted() const { return init.params[6]; }

  typename MarkovGenerator<T,H>::Chain::Stats getChainStats() const { return generator.chain().getStats(); }
  int wordSizeMs() const { return static_cast<int>(static_cast<float>(generator.chain().getCurrentWordSize()) / unit::getSampleRate() * 1000);}
  int clocksUntilReset() const { return clocksToReset; }
  
  T process(const T& input) override
  {
    return input;
  }

  void process(array<T> in, array<T> out) override
  {
    size_t inSize = in.getSize();
    tick(inSize);
    
    if (samplesSinceLastTock < CLOCK_PERIOD_MAX)
    {
      samplesSinceLastTock += inSize;
    }
    
    for (T s : in)
    {
      float listenState = *listen();
      // need to generate even if we don't use the value otherwise internal state won't update
      float env = listenEnvelope.process(listenState);
      if (env > vessl::math::epsilon<float>())
      {
        generator.learn(s*env);
      }
    }

    size_t blockSize = out.getSize();
    size_t wordStartedGateDelay = 0;
    if (wordStartedGate > 0)
    {
      if (static_cast<size_t>(wordStartedGate) < blockSize)
      {
        wordStartedGateDelay = static_cast<size_t>(wordStartedGate);
      }
      wordStartedGate -= blockSize;
    }

    envelopeShape = decaySmoother.process(*decay());

    typename array<T>::writer w(out);
    while (w)
    {
      if (samplesToReset == 0)
      {
        generator.chain().resetWord();
      }

      if (samplesToReset >= 0)
      {
        --samplesToReset;
      }

      // word going to start, update the word size, envelope settings
      if (generator.chain().getLetterCount() == 0)
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
        wordStartedGateDelay = blockSize - w.available();
      }

      updateEnvelope();

      w << generator.generate() * getEnvelopeLevel();
    }

    progressOut() << generator.chain().getWordProgress();
    envelopeOut() << getEnvelopeLevel();
    wordStartedOut().write(wordStartedGate > 0, static_cast<uint32_t>(wordStartedGateDelay));
  }

protected:
  void tock(size_t sampleDelay) override
  {
    samplesSinceLastTock = -static_cast<int>(sampleDelay);

    // don't reset when doing full random variation
    if (*variation() < 0.53f && clocksToReset == 0)
    {
      samplesToReset = static_cast<int>(sampleDelay);
    }

    if (clocksToReset > 0)
    {
      --clocksToReset;
    }
  }

private:
  void setEnvelopeRelease(const int wordSize)
  {
    if (envelopeShape >= 0.99f)
    {
      wordGateLength = wordSize;
    }
    else if (envelopeShape >= 0.53f)
    {
      float t = (envelopeShape - 0.53f) * 2.12f;
      wordGateLength = static_cast<int>(vessl::easing::lerp(static_cast<float>(minWordGateLength), static_cast<float>(wordSize - minWordGateLength), t));
    }
    else
    {
      wordGateLength = minWordSizeSamples;
    }
    const float wordReleaseSeconds = static_cast<float>(wordSize - wordGateLength) / unit::getSampleRate();
    expoGenerateEnvelope.release().duration() << wordReleaseSeconds;
    linearGenerateEnvelope.release().duration() << wordReleaseSeconds;
  }
  
  void updateEnvelope()
  {
    const bool state = generator.chain().getLetterCount() < wordGateLength;
    expoGenerateEnvelope.gate(state);
    linearGenerateEnvelope.gate(state);

    expoGenerateEnvelope.generate<vessl::easing::expo::out>();
    linearGenerateEnvelope.generate<vessl::easing::linear>();
  }

  float getEnvelopeLevel() const
  {
    float expo = *expoGenerateEnvelope.value();
    float line = *linearGenerateEnvelope.value();
    if (envelopeShape <= 0.47f)
    {
      float t = (0.47f - envelopeShape) * 2.12f;
      return vessl::easing::lerp<float>(line, expo, t);
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

    float divMultT = vessl::easing::lerp(0.f, static_cast<float>(DIV_MULT_LEN - 1), *wordSize());
    bool smoothDivMult = samplesSinceLastTock >= CLOCK_PERIOD_MAX;
    int divMultIdx = smoothDivMult ? static_cast<int>(divMultT) : static_cast<int>(round(divMultT));
    int intervalIdx = 3;
    float wordScale = smoothDivMult ? vessl::easing::lerp<float>(DIV_MULT[divMultIdx], DIV_MULT[divMultIdx+1], divMultT - static_cast<float>(divMultIdx))
                                    : DIV_MULT[divMultIdx];

    float wordVariationParam = *variation();
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
      float scale = vessl::easing::lerp<float>(1.f, 4.f, randf()*varyAmt);
      // weight towards shorter
      if (randf() > 0.25f) { scale = 1.0f / scale; }
      wordScale *= scale;
      wordsToNewInterval = 1;
    }
    // random variation using musical mult/divs of the current word size
    else if (wordVariationParam <= 0.47f)
    {
      // when varyAmt is zero, we want the interval in the middle of the array (ie 1).
      // so we offset from 0.5f with a random value between -0.5 and 0.5, scaled by varyAmt
      // (ie as vary amount gets larger we can pick values closer to the ends of the array).
      intervalIdx = static_cast<int>(vessl::easing::lerp<float>(0, INTERVALS_LEN - 1, 0.5f + (randf() - 0.5f) * varyAmt));
      float interval = INTERVALS[intervalIdx];
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

    float periodInSamples = static_cast<float>(tempo.samples);
    int wordSize = std::max(minWordSizeSamples, static_cast<int>(periodInSamples * wordScale));
    clocksToReset = COUNTERS[divMultIdx][intervalIdx] - 1;

    generator.chain().setWordSize(wordSize);
    setEnvelopeRelease(wordSize);
  }
};