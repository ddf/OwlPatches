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
  using DecayEnvelope = ExponentialDecayEnvelope;

  struct Chirp
  {
    SineOscillator* osc;
    DecayEnvelope* decay;
    float finc;
  };

  static const PatchParameterId inPitch = PARAMETER_A;
  static const PatchParameterId inDecay = PARAMETER_B;
  static const PatchParameterId inWobble = PARAMETER_C;
  static const PatchParameterId inEcho = PARAMETER_D;
  static const PatchParameterId inWidth = PARAMETER_E;

  static const PatchParameterId outAmp = PARAMETER_F;
  static const PatchParameterId outFreq = PARAMETER_G;

  const float pitchLow = 60;
  const float pitchHigh = 60 * 8;

  const float decayMinLow = 0.01f;
  const float decayMaxLow = 0.03f;
  const float decayMinHigh = 2.9f;
  const float decayMaxHigh = 3.1f;


  static const int kChirpCount = 8;
  Chirp chirps[kChirpCount];

  SineOscillator* wave;
  SineOscillator* pan;
  SawOscillator* delayMod;
  Delay* ampDelay;
  Delay* freqDelay;
  WaveShaper* freqDelayShaper;
  FloatArray freqDelayShaperTable;
  float ampDelayValue;
  float freqDelayValue;
  PatchButtonId lastButtonPress = BUTTON_1;

  SmoothFloat pitch;
  SmoothFloat decay;
  SmoothFloat wobble;
  SmoothFloat echo;
  SmoothFloat width;

public:
  CreaturePatch() 
    : MonochromeScreenPatch()
    , pitch(0.9f, pitchLow), ampDelayValue(0), freqDelayValue(0)
  {
    for (int i = 0; i < kChirpCount; ++i)
    {
      chirps[i].osc = SineOscillator::create(getSampleRate());
      chirps[i].decay = DecayEnvelope::create(getSampleRate());
      chirps[i].finc = 0;
    }

    wave = SineOscillator::create(getSampleRate());
    wave->setFrequency(pitch);

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

    registerParameter(inPitch, "Pitch");
    registerParameter(inDecay, "Decay");
    registerParameter(inWobble, "Wobble");
    registerParameter(inEcho, "Echo");
    registerParameter(inWidth, "Width");

    registerParameter(outAmp, "Amp>");
    registerParameter(outFreq, "Freq>");
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
      const float lowFreq = Interpolator::linear(10.f, 1000.0f, pitch);
      const float hiFreq = Interpolator::linear(80.f, 8000.0f, pitch);
      const float durMin = Interpolator::linear(decayMinLow, decayMinHigh, decay);
      const float durMax = Interpolator::linear(decayMaxLow, decayMaxHigh, decay);
      // find chirp with lowest level and retrigger it
      int cidx = 0;
      for (int i = 1; i < kChirpCount; ++i)
      {
        if (chirps[i].decay->getLevel() < chirps[cidx].decay->getLevel())
        {
          cidx = i;
        }
      }
      Chirp& chirp = chirps[cidx];
      float dur = Interpolator::linear(durMin, durMax, randf());
      float freq = Interpolator::linear(lowFreq, hiFreq, randf());
      float fromFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
      float toFreq = freq * Interpolator::linear(0.8f, 1.2f, randf());
      chirp.osc->setFrequency(fromFreq);
      chirp.finc = (toFreq - fromFreq) / (dur * getSampleRate());
      chirp.osc->reset();
      chirp.decay->setDecay(dur);
      //chirp.decay->setSustain(0);
      chirp.decay->trigger();
      // pull one sample so that the level is not 0 in process audio
      chirp.decay->generate();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    pitch = getParameterValue(inPitch); // Interpolator::linear(pitchLow, pitchHigh, getParameterValue(inPitch));
    decay = getParameterValue(inDecay);
    wobble = getParameterValue(inWobble)*0.95f;
    echo = getParameterValue(inEcho)*0.95f;
    width = getParameterValue(inWidth);

    //wave->setFrequency(pitch);

    for (int i = 0; i < size; ++i)
    {
      float chirpSignal = 0;
      for (int c = 0; c < kChirpCount; ++c)
      {
        Chirp& chirp = chirps[c];
        if (chirp.decay->getLevel() > 0.0001f)
        {
          chirpSignal += chirp.osc->generate() * chirp.decay->generate() * 0.6f;
          chirp.osc->setFrequency(chirp.osc->getFrequency() + chirp.finc);
        }
      }

      float freqDelaySamples = (0.05f + ampDelayValue*0.02f)*getSampleRate();
      freqDelay->setDelay(freqDelaySamples);
      freqDelayValue = freqDelay->process(chirpSignal + freqDelayValue * wobble); // *wave->getSample();

      float delayModFM = fabsf(freqDelayValue * wave->getSample());
      delayMod->setFrequency(delayModFM);
      float ampDelaySamples = (delayMod->generate()*0.1f + 0.15f) * getSampleRate();
      ampDelay->setDelay(ampDelaySamples);
      ampDelayValue = clamp(ampDelay->process(chirpSignal + ampDelayValue*echo), -1.0f, 1.0f);

      float waveFreq = freqDelayShaper->process(freqDelayValue);
      //wave->setFrequency(waveFreq);
      pan->setFrequency(Interpolator::linear(0.2f, 10.f, freqDelayValue*0.5f + 0.5f));

      float waveValue = wave->generate(waveFreq / getSampleRate()) * ampDelayValue;
      float panValue = pan->generate() * width;

      float normBalance = (panValue + 1.f) * 0.5f;

      // note that I am calculating amplitude directly, by using the linear value
      // that the MIDI specification suggests inputing into the dB formula.
      float leftAmp = cosf(M_PI_2 * normBalance);
      float rightAmp = sinf(M_PI_2 * normBalance);
      left[i] = leftAmp * waveValue;
      right[i] = rightAmp * waveValue;
    }

    setParameterValue(outAmp, ampDelayValue*0.5f + 0.5f);
    setParameterValue(outFreq, clamp(freqDelayValue*0.5f + 0.5f, 0.0, 1.0f));
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
