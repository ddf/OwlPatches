#include "Patch.h"
#include "MonochromeScreenPatch.h"
#include "SineOscillator.h"
#include "RampOscillator.h"
#include "ChirpOscillator.h"
#include "AdsrEnvelope.h"
#include "ExponentialDecayEnvelope.h"
#include "DelayProcessor.h"
#include "WaveShaper.h"

class CreaturePatch : public MonochromeScreenPatch 
{
  using Delay = FractionalDelayProcessor<LINEAR_INTERPOLATION>;
  using SawOscillator = InvertedRampOscillator;
  using DecayEnvelope = AdsrEnvelope<true>;

  struct Chirp
  {
    SineOscillator* osc;
    DecayEnvelope* decay;
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
  PatchButtonId lastButtonPress = BUTTON_1;

public:
  CreaturePatch() 
    : MonochromeScreenPatch()
    , waveCenterFrequency(60), ampDelayValue(0), freqDelayValue(0)
  {
    for (int i = 0; i < kChirpCount; ++i)
    {
      chirps[i].osc = SineOscillator::create(getSampleRate());
      chirps[i].decay = DecayEnvelope::create(getSampleRate());
      chirps[i].finc = 0;
    }

    wave = SineOscillator::create(getSampleRate());
    wave->setFrequency(waveCenterFrequency);

    pan = SineOscillator::create(getSampleRate());
    pan->setFrequency(1.5f);

    delayMod = SawOscillator::create(getSampleRate());
    delayMod->setFrequency(0.1f);

    ampDelay = Delay::create(getSampleRate());
    freqDelay = Delay::create(0.5f*getSampleRate());
    freqDelay->setDelay(0.1f*getSampleRate());

    freqDelayShaperTable = FloatArray::create(1024);
    freqDelayShaperTable.subArray(0, 257).ramp(0, 1500);
    freqDelayShaperTable.subArray(256, 257).ramp(1500, 0);
    freqDelayShaperTable.subArray(512, 257).ramp(0, 3000);
    freqDelayShaperTable.subArray(768, 256).ramp(3000, 0);
    freqDelayShaper = WaveShaper::create(freqDelayShaperTable);

    registerParameter(PARAMETER_A, "Amp Delay>");
    registerParameter(PARAMETER_B, "Freq Delay>");
  }

  ~CreaturePatch()
  {
    for (int i = 0; i < kChirpCount; ++i)
    {
      SineOscillator::destroy(chirps[i].osc);
      DecayEnvelope::destroy(chirps[i].decay);
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
    if ((bid == BUTTON_1 || bid == BUTTON_2) && value == Patch::ON)
    {
      lastButtonPress = bid;
      const float lowFreq = bid == BUTTON_1 ? 10.f : 1000.0f;
      const float hiFreq  = bid == BUTTON_1 ? 80.f : 8000.0f;
      for (int i = 0; i < kChirpCount; ++i)
      {
        Chirp& chirp = chirps[i];
        if (chirp.decay->getLevel() == 0)
        {
          float dur = Interpolator::linear(0.01f, 0.03f, randf());
          float freq = Interpolator::linear(lowFreq, hiFreq, randf());
          float fromFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
          float toFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
          chirp.osc->setFrequency(fromFreq);
          chirp.finc = (toFreq - fromFreq) / (dur * getSampleRate());
          chirp.osc->reset();
          chirp.decay->setDecay(dur);
          chirp.decay->setSustain(0);
          chirp.decay->trigger();
          // pull one sample so that the level is not 0 in process audio
          chirp.decay->generate();
          break;
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
        if (chirp.decay->getLevel() > 0)
        {
          chirpSignal += chirp.osc->generate() * chirp.decay->generate() * 0.6f;
          chirp.osc->setFrequency(chirp.osc->getFrequency() + chirp.finc);
        }
      }

      float freqDelaySamples = (0.1f + ampDelayValue*0.01f)*getSampleRate();
      freqDelay->setDelay(freqDelaySamples);
      freqDelayValue = freqDelay->process(chirpSignal + freqDelayValue * 0.2f); // *wave->getSample();

      float delayModFM = fabsf(freqDelayValue * wave->getSample());
      delayMod->setFrequency(delayModFM);
      float ampDelaySamples = (delayMod->generate()*0.1f + 0.15f) * getSampleRate();
      ampDelay->setDelay(ampDelaySamples);
      ampDelayValue = clamp(ampDelay->process(chirpSignal + ampDelayValue*0.25f), -1.0f, 1.0f);

      //float waveFreq = waveCenterFrequency + freqDelayValue * 0.5f * waveCenterFrequency;
      float waveFreq = freqDelayShaper->process(freqDelayValue);
      //wave->setFrequency(waveFreq);
      pan->setFrequency(Interpolator::linear(0.2f, 10.f, freqDelayValue*0.5f + 0.5f));

      float waveValue = wave->generate(waveFreq / getSampleRate()) * ampDelayValue;
      float panValue = pan->generate() * 0.1f;

      float normBalance = (panValue + 1.f) * 0.5f;

      // note that I am calculating amplitude directly, by using the linear value
      // that the MIDI specification suggests inputing into the dB formula.
      float leftAmp = cosf(M_PI_2 * normBalance);
      float rightAmp = sinf(M_PI_2 * normBalance);
      left[i] = leftAmp * waveValue;
      right[i] = rightAmp * waveValue;
    }

    setParameterValue(PARAMETER_A, ampDelayValue*0.5f + 0.5f);
    setParameterValue(PARAMETER_B, clamp(freqDelayValue*0.5f + 0.5f, 0.0, 1.0f));
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 20);
    switch (lastButtonPress)
    {
      case BUTTON_1: screen.print("BUTTON 1"); break;
      case BUTTON_2: screen.print("BUTTON 2"); break;
      default: screen.print(lastButtonPress); break;
    }
  }

};
