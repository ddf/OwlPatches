#define USE_MIDI_CALLBACK

#include "Patch.h"
#include "MidiMessage.h"
#include "SpectralSignalGenerator.h"
#include "BitCrusher.hpp"
#include "Diffuser.h"
#include "Reverb.h"
#include "Frequency.h"
#include "Interpolator.h"
#include "Easing.h"

struct SpectralHarpParameterIds
{
  PatchParameterId inHarpFundamental; // = PARAMETER_A;
  PatchParameterId inHarpOctaves; // = PARAMETER_B;
  PatchParameterId inDensity; // = PARAMETER_C;
  PatchParameterId inTuning; // PARAMETER_D;
  PatchParameterId inDecay; // = PARAMETER_E;
  PatchParameterId inSpread; // = PARAMETER_F;
  PatchParameterId inBrightness; // = PARAMETER_G;
  PatchParameterId inCrush; // = PARAMETER_H;

  PatchParameterId inWidth; // = PARAMETER_AA;
  PatchParameterId inReverbBlend; // = PARAMETER_AB;
  PatchParameterId inReverbTime; // = PARAMETER_AC;
  PatchParameterId inReverbTone; // = PARAMETER_AD;

  PatchParameterId outStrumX; // = PARAMETER_AE;
  PatchParameterId outStrumY; // = PARAMETER_AF;
};

template<int spectrumSize, bool reverb_enabled, typename PatchClass = Patch>
class SpectralHarpPatch : public PatchClass
{
  using SpectralGen = SpectralSignalGenerator<false>;
  using BitCrush = BitCrusher<24>;

protected:
  const SpectralHarpParameterIds params;

  const float spreadMax = 1.0f;
  const float decayMin;
  const float decayMax;
  const float decayDefault = 0.5f;
  const int   densityMin = 6;
  const int   densityMax = 129;
  const float octavesMin = 2;
  const float octavesMax = 8;
  const int   fundamentalNoteMin = 36;
  const int   fundaMentalNoteMax = 128 - octavesMin * 12;
  const float bandMin = Frequency::ofMidiNote(fundamentalNoteMin).asHz();
  const float bandMax = Frequency::ofMidiNote(128).asHz();
  const float crushRateMin = 1000.0f;

  SpectralGen* spectralGen;
  BitCrush* bitCrusher;
  Diffuser* diffuser;
  Reverb*   reverb;
  
  int        pluckAtSample;
  int        gateOnAtSample;
  int        gateOffAtSample;
  bool       gateState;
  StiffFloat bandFirst;
  StiffFloat bandLast;
  SmoothFloat spread;
  SmoothFloat decay;
  SmoothFloat brightness;
  SmoothFloat volume;
  SmoothFloat crush;
  SmoothFloat linLogLerp;
  SmoothFloat bandDensity;
  SmoothFloat stereoWidth;
  SmoothFloat reverbTime;
  SmoothFloat reverbTone;
  SmoothFloat reverbBlend;

  MidiMessage* midiNotes;

public:

  using PatchClass::registerParameter;
  using PatchClass::getParameterValue;
  using PatchClass::setParameterValue;
  using PatchClass::getSampleRate;
  using PatchClass::isButtonPressed;

  SpectralHarpPatch(SpectralHarpParameterIds paramIds) : PatchClass()
    , params(paramIds), pluckAtSample(-1), gateOnAtSample(-1), gateOffAtSample(-1), gateState(false)
    , decayMin((float)spectrumSize*0.5f / getSampleRate()), decayMax(10.0f)
    , bandFirst(1.f), bandLast(1.f)
  {
    spectralGen = SpectralGen::create(spectrumSize, getSampleRate());
    bitCrusher = BitCrush::create(getSampleRate(), getSampleRate());

    if (reverb_enabled)
    {
      diffuser = Diffuser::create();
      reverb = Reverb::create(getSampleRate());
    }

    midiNotes = new MidiMessage[128];
    memset(midiNotes, 0, sizeof(MidiMessage)*128);

    // register Decay and Spread first
    // so that these wind up as the default CV A and B parameters on Genius
    registerParameter(params.inDecay, "Decay");
    registerParameter(params.inSpread, "Spread");
    registerParameter(params.inBrightness, "Brightness");
    registerParameter(params.inCrush, "Crush");
    registerParameter(params.inHarpFundamental, "Fundamentl");
    registerParameter(params.inHarpOctaves, "Octaves");
    registerParameter(params.inDensity, "Density");
    registerParameter(params.inTuning, "Tuning");
    if (reverb_enabled)
    {
      registerParameter(params.inWidth, "Width");
      registerParameter(params.inReverbTime, "Verb Time");
      registerParameter(params.inReverbTone, "Verb Tone");
      registerParameter(params.inReverbBlend, "Verb Blend");
    }

    registerParameter(params.outStrumX, "Strum X>");
    registerParameter(params.outStrumY, "Strum Y>");

    setParameterValue(params.inHarpFundamental, 0.0f);
    setParameterValue(params.inHarpOctaves, 1.0f);
    setParameterValue(params.inDecay, (decayDefault - decayMin) / (decayMax - decayMin));
    setParameterValue(params.inDensity, 1.0f);
    setParameterValue(params.inSpread, 0.0f);
    setParameterValue(params.inBrightness, 0.0f);
    setParameterValue(params.inCrush, 0.0f);
    setParameterValue(params.inTuning, 0.0f);

    if (reverb_enabled)
    {
      setParameterValue(params.inReverbTone, 1.0f);
    }
  }

  ~SpectralHarpPatch()
  {
    SpectralGen::destroy(spectralGen);
    BitCrush::destroy(bitCrusher);
    if (reverb_enabled)
    {
      Diffuser::destroy(diffuser);
      Reverb::destroy(reverb);
    }
    delete[] midiNotes;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples)
  {
    if ((bid == PUSHBUTTON || bid == BUTTON_1) && value == Patch::ON)
    {
      pluckAtSample = samples;
    }

    if (bid == BUTTON_2)
    {
      if (value == Patch::ON)
      {
        gateOnAtSample = samples;
      }
      else
      {
        gateOffAtSample = samples;
      }
    }
  }

  void processMidi(MidiMessage msg) override
  {
    if (msg.isNote())
    {
      midiNotes[msg.getNote()] = msg;

      if (msg.isNoteOn())
      {
        pluck(spectralGen, msg);
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int blockSize = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    float harpFund = Interpolator::linear(fundamentalNoteMin, fundaMentalNoteMax, getParameterValue(params.inHarpFundamental));
    float harpOctaves = Interpolator::linear(octavesMin, octavesMax, getParameterValue(params.inHarpOctaves));
    bandFirst = Frequency::ofMidiNote(harpFund).asHz();
    bandLast  = fmin(Frequency::ofMidiNote(harpFund + harpOctaves * MIDIOCTAVE).asHz(), bandMax);
    int bandFirstIdx = spectralGen->freqToIndex(bandFirst);
    int bandLastIdx = spectralGen->freqToIndex(bandLast);
    bandDensity = Interpolator::linear(densityMin, min(bandLastIdx - bandFirstIdx, densityMax), getParameterValue(params.inDensity));
    linLogLerp = getParameterValue(params.inTuning);

    spread = getParameterValue(params.inSpread)*spreadMax;
    decay = Interpolator::linear(decayMin, decayMax, getParameterValue(params.inDecay));
    brightness = getParameterValue(params.inBrightness);
    crush = Easing::expoOut(getSampleRate(), crushRateMin, getParameterValue(params.inCrush));

    // reduce volume based on combination of decay, spread, and brightness parameters
    volume = Easing::expoOut(1.0f, 0.15f, 0.2f*getParameterValue(params.inDecay)
                                        + 0.7f*getParameterValue(params.inSpread)
                                        + 0.1f*getParameterValue(params.inBrightness));

    spectralGen->setSpread(spread);
    spectralGen->setDecay(decay);
    spectralGen->setBrightness(brightness);
    spectralGen->setVolume(volume);
    bitCrusher->setBitRate(crush);

    float strumX = 0;
    float strumY = 0;

    if (pluckAtSample != -1)
    {
      float location = left[pluckAtSample] * 0.5f + 0.5f;
      float amplitude = right[pluckAtSample] * 0.5f + 0.5f;
      pluck(spectralGen, location, amplitude);
      pluckAtSample = -1;
      strumX = location;
      strumY = amplitude;
    }

    for (int i = 0; i < blockSize; ++i)
    {
      if (i == gateOnAtSample) gateState = true;
      if (i == gateOffAtSample) gateState = false;

      if (gateState)
      {
        float location = left[i] * 0.5f + 0.5f;
        float amplitude = right[i] * 0.5f + 0.5f;
        pluck(spectralGen, location, amplitude);
        strumX = fmax(strumX, location);
        strumY = fmax(strumY, amplitude);
      }
    }
    
    gateOnAtSample = -1;
    gateOffAtSample = -1;

    for (int i = 0; i < 128; ++i)
    {
      if (midiNotes[i].isNoteOn())
      {
        pluck(spectralGen, midiNotes[i]);
      }
    }

    spectralGen->generate(left);
    bitCrusher->process(left, left);

    left.copyTo(right);

    if (reverb_enabled)
    {
      stereoWidth = getParameterValue(params.inWidth);
      reverbTime = 0.35f + 0.6f*getParameterValue(params.inReverbTime);
      reverbTone = Interpolator::linear(0.2f, 0.97f, getParameterValue(params.inReverbTone));
      reverbBlend = getParameterValue(params.inReverbBlend) * 0.56f;

      diffuser->setAmount(stereoWidth);
      diffuser->process(audio, audio);

      float meanSpectralMagnitude = spectralGen->getMagnitudeMean();
      float reverbInputGain = clamp(0.2f - meanSpectralMagnitude, 0.05f, 1.0f);

      reverb->setDiffusion(0.7f);
      reverb->setInputGain(reverbInputGain);
      reverb->setReverbTime(reverbTime);
      reverb->setLowPass(reverbTone);
      reverb->setAmount(reverbBlend);
      reverb->process(audio, audio);
    }

    setParameterValue(params.outStrumX, strumX);
    setParameterValue(params.outStrumY, strumY);
  }

protected:
  // get the current string count based on the density setting
  int getStringCount()
  {
    return (int)(bandDensity+0.5f);
  }

  float frequencyOfString(int stringNum)
  {
    const float t = (float)stringNum / getStringCount();
    // convert first and last bands to midi notes and then do a linear interp, converting back to Hz at the end.
    Frequency lowFreq = Frequency::ofHertz(bandFirst);
    Frequency hiFreq = Frequency::ofHertz(bandLast);
    const float linFreq = Interpolator::linear(lowFreq.asHz(), hiFreq.asHz(), t);
    const float midiNote = Interpolator::linear(lowFreq.asMidiNote(), hiFreq.asMidiNote(), t);
    const float logFreq = Frequency::ofMidiNote(midiNote).asHz();
    // we lerp from logFreq up to linFreq because log spacing clusters frequencies
    // towards the bottom of the range, which means that when holding down the mouse on a string
    // and lowering this param, you'll hear the pitch drop, which makes more sense than vice-versa.
    return Interpolator::linear(logFreq, linFreq, linLogLerp);
  }

private:
  void pluck(SpectralGen* spectrum, float location, float amp)
  {
    const int   numBands = getStringCount();
    const int   band = roundf(Interpolator::linear(0, numBands, location));
    const float freq = frequencyOfString(band);
    spectrum->pluck(freq, amp);
  }

  void pluck(SpectralGen* spectrum, MidiMessage msg)
  {
    float freq = Frequency::ofMidiNote(msg.getNote()).asHz();
    float amp = msg.getVelocity() / 127.0f;
    spectrum->pluck(freq, amp);
  }
}; 
