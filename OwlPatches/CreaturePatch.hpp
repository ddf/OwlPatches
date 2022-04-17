#include "Patch.h"
#include "SineOscillator.h"
#include "RampOscillator.h"
#include "ChirpOscillator.h"
#include "ExponentialDecayEnvelope.h"
#include "DelayProcessor.h"
#include "WaveShaper.h"

class CreaturePatch : public Patch 
{
  using Delay = FractionalDelayProcessor<LINEAR_INTERPOLATION>;
  using SawOscillator = InvertedRampOscillator;

  struct Chirp
  {
    SineOscillator* osc;
    ExponentialDecayEnvelope* decay;
    float finc;
  };

  static const int kChirpCount = 8;
  Chirp chirps[kChirpCount];

  SineOscillator* wave;
  SineOscillator* pan;
  SawOscillator* delayMod;
  Delay* ampDelay;
  Delay* freqDelay;
  WaveShaper* freqDelayShaper;
  FloatArray freqDelayShaperTable;
  float waveCenterFrequency;
  float ampDelayValue;
  float freqDelayValue;

public:
  CreaturePatch() 
    : Patch()
    , waveCenterFrequency(250), ampDelayValue(0), freqDelayValue(0)
  {
    for (int i = 0; i < kChirpCount; ++i)
    {
      chirps[i].osc = SineOscillator::create(getSampleRate());
      chirps[i].decay = ExponentialDecayEnvelope::create(getSampleRate());
      chirps[i].finc = 0;
    }

    wave = SineOscillator::create(getSampleRate());
    wave->setFrequency(waveCenterFrequency);

    pan = SineOscillator::create(getSampleRate());
    pan->setFrequency(1.5f);

    delayMod = SawOscillator::create(getSampleRate());
    delayMod->setFrequency(0.1f);

    ampDelay = Delay::create(getSampleRate());
    freqDelay = Delay::create(0.3f*getSampleRate());
    freqDelay->setDelay(0.29f*getSampleRate());

    freqDelayShaperTable = FloatArray::create(1024);
    freqDelayShaperTable.subArray(0, 257).ramp(100, 1500);
    freqDelayShaperTable.subArray(256, 257).ramp(1500, 350);
    freqDelayShaperTable.subArray(512, 257).ramp(350, 3000);
    freqDelayShaperTable.subArray(768, 256).ramp(3000, 50);
    freqDelayShaper = WaveShaper::create(freqDelayShaperTable);

    registerParameter(PARAMETER_A, "Amp Delay>");
    registerParameter(PARAMETER_B, "Freq Delay>");
  }

  ~CreaturePatch()
  {
    for (int i = 0; i < kChirpCount; ++i)
    {
      SineOscillator::destroy(chirps[i].osc);
      ExponentialDecayEnvelope::destroy(chirps[i].decay);
    }

    SineOscillator::destroy(wave);
    SineOscillator::destroy(pan);
    SawOscillator::destroy(delayMod);
    Delay::destroy(ampDelay);
    Delay::destroy(freqDelay);
    FloatArray::destroy(freqDelayShaperTable);
    WaveShaper::destroy(freqDelayShaper);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (value == Patch::ON)
    {
      const float lowFreq = bid == BUTTON_1 ? 1.0f : 5.0f;
      const float hiFreq = bid == BUTTON_2 ? 20.0f : 1000.0f;
      for (int i = 0; i < kChirpCount; ++i)
      {
        Chirp& chirp = chirps[i];
        if (chirp.decay->getLevel() < 0.0001f)
        {
          float dur = Interpolator::linear(0.1f, 0.2f, randf());
          float freq = Interpolator::linear(lowFreq, hiFreq, randf());
          float fromFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
          float toFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
          chirp.osc->setFrequency(fromFreq);
          chirp.finc = (toFreq - fromFreq) / (dur * getSampleRate());
          chirp.osc->reset();
          chirp.decay->setDecay(dur);
          chirp.decay->trigger();
        }
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    for (int i = 0; i < size; ++i)
    {
      float chirpSignal = 0;
      for (int c = 0; c < kChirpCount; ++c)
      {
        Chirp& chirp = chirps[c];
        if (chirp.decay->getLevel() >= 0.0001f)
        {
          chirpSignal += chirp.osc->generate() * chirp.decay->generate() * 0.5f;
          chirp.osc->setFrequency(chirp.osc->getFrequency() + chirp.finc);
        }
      }

      freqDelayValue = freqDelay->process(chirpSignal + freqDelayValue) * wave->getSample() * ampDelayValue;

      float fd = fabsf(freqDelayValue);
      delayMod->setFrequency(fd);
      float ampDelaySamples = (delayMod->generate()*0.1f + 0.15f) * getSampleRate();
      ampDelay->setDelay(ampDelaySamples);
      ampDelayValue  = ampDelay->process(chirpSignal + ampDelayValue) * 0.5f;

      float waveFreq = waveCenterFrequency + freqDelayValue * 0.5f * waveCenterFrequency; 
      waveFreq = freqDelayShaper->process(freqDelayValue);
      wave->setFrequency(waveFreq);
      pan->setFrequency(Interpolator::linear(0.2f, 10.f, clamp(freqDelayValue*0.5f + 0.5f, 0.0f, 1.0f)));

      float waveValue = wave->generate() * ampDelayValue;
      float panValue = pan->generate() * 0.8f;

      float normBalance = (panValue + 1.f) * 0.5f;

      // note that I am calculating amplitude directly, by using the linear value
      // that the MIDI specification suggests inputing into the dB formula.
      float leftAmp = cosf(M_PI_2 * normBalance);
      float rightAmp = sinf(M_PI_2 * normBalance);
      left[i] = leftAmp * waveValue;
      right[i] = rightAmp * waveValue;
    }

    setParameterValue(PARAMETER_A, ampDelayValue*0.5f + 0.5f);
    setParameterValue(PARAMETER_B, freqDelayValue*0.5f + 0.5f);
  }
};
