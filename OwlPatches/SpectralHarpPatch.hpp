#define USE_MIDI_CALLBACK

#include "Patch.h"
#include "MidiMessage.h"
#include "SpectralSignalGenerator.h"
#include "Frequency.h"
#include "Interpolator.h"

template<int spectrumSize, typename PatchClass>
class SpectralHarpPatch : public PatchClass
{
protected:
  static const PatchParameterId inHarpFundamental = PARAMETER_A;
  static const PatchParameterId inHarpOctaves = PARAMETER_B;
  static const PatchParameterId inDensity = PARAMETER_C;
  static const PatchParameterId inTuning = PARAMETER_D;
  static const PatchParameterId inDecay = PARAMETER_E;
  static const PatchParameterId inSpread = PARAMETER_F;
  static const PatchParameterId inBrightness = PARAMETER_G;

  static const PatchParameterId outStrumX = PARAMETER_AA;
  static const PatchParameterId outStrumY = PARAMETER_AB;

  const float spreadMax = 1.0f;
  const float decayMin = 0.15f;
  const float decayMax = 5.0f;
  const float decayDefault = 0.5f;
  const int   densityMin = 6.0f;
  const int   densityMax = 121.0f;
  const float octavesMin = 2;
  const float octavesMax = 8;
  const int   fundamentalNoteMin = 36;
  const int   fundaMentalNoteMax = 128 - octavesMin * 12;
  const float bandMin = Frequency::ofMidiNote(fundamentalNoteMin).asHz();
  const float bandMax = Frequency::ofMidiNote(128).asHz();

  SpectralSignalGenerator* spectralGen;

  StiffFloat bandFirst;
  StiffFloat bandLast;
  SmoothFloat spread;
  SmoothFloat decay;
  SmoothFloat brightness;
  SmoothFloat linLogLerp;
  SmoothFloat bandDensity;

  MidiMessage* midiNotes;

public:

  using PatchClass::registerParameter;
  using PatchClass::getParameterValue;
  using PatchClass::setParameterValue;
  using PatchClass::getSampleRate;
  using PatchClass::isButtonPressed;

  SpectralHarpPatch() : PatchClass()
  {
    spectralGen = SpectralSignalGenerator::create(spectrumSize, getSampleRate());
    midiNotes = new MidiMessage[128];
    memset(midiNotes, 0, sizeof(MidiMessage)*128);

    registerParameter(inHarpFundamental, "Harp Fund");
    registerParameter(inHarpOctaves, "Harp Oct");
    registerParameter(inSpread, "Spread");
    registerParameter(inDecay, "Decay");
    registerParameter(inBrightness, "Brightness");
    registerParameter(inTuning, "Tuning");
    registerParameter(inDensity, "Density");

    registerParameter(outStrumX, "Strum X>");
    registerParameter(outStrumY, "Strum Y>");

    setParameterValue(inHarpFundamental, 0.0f);
    setParameterValue(inHarpOctaves, 1.0f);
    setParameterValue(inDecay, (decayDefault - decayMin) / (decayMax - decayMin));
    setParameterValue(inDensity, 1.0f);
  }

  ~SpectralHarpPatch()
  {
    SpectralSignalGenerator::destroy(spectralGen);
    delete[] midiNotes;
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

    float harpFund = Interpolator::linear(fundamentalNoteMin, fundaMentalNoteMax, getParameterValue(inHarpFundamental));
    float harpOctaves = Interpolator::linear(octavesMin, octavesMax, getParameterValue(inHarpOctaves));
    bandFirst = Frequency::ofMidiNote(harpFund).asHz();
    bandLast = fmin(Frequency::ofMidiNote(harpFund + harpOctaves*12).asHz(), bandMax);
    bandDensity = Interpolator::linear(densityMin, densityMax, getParameterValue(inDensity));
    linLogLerp = getParameterValue(inTuning);

    spread = getParameterValue(inSpread)*spreadMax;
    decay = Interpolator::linear(decayMin, decayMax, getParameterValue(inDecay));
    brightness = getParameterValue(inBrightness);

    spectralGen->setSpread(spread);
    spectralGen->setDecay(decay);
    spectralGen->setBrightness(brightness);

    float strumX = 0;
    float strumY = 0;
    if (isButtonPressed(BUTTON_1))
    {
      for (int i = 0; i < blockSize; ++i)
      {
        float location = left[i] * 0.5f + 0.5f;
        float amplitude = right[i] * 0.5f + 0.5f;
        pluck(spectralGen, location, amplitude);
        strumX = fmax(strumX, location);
        strumY = fmax(strumY, amplitude);
      }
    }

    for (int i = 0; i < 128; ++i)
    {
      if (midiNotes[i].isNoteOn())
      {
        pluck(spectralGen, midiNotes[i]);
      }
    }

    spectralGen->generate(left);
    left.copyTo(right);

    setParameterValue(outStrumX, strumX);
    setParameterValue(outStrumY, strumY);
  }

protected:
  float frequencyOfString(int stringNum, int stringCount, float lowFreqHz, float hiFreqHz, float linLogLerp)
  {
    const float t = (float)stringNum / stringCount;
    // convert first and last bands to midi notes and then do a linear interp, converting back to Hz at the end.
    Frequency lowFreq = Frequency::ofHertz(lowFreqHz);
    Frequency hiFreq = Frequency::ofHertz(hiFreqHz);
    const float linFreq = Interpolator::linear(lowFreq.asHz(), hiFreq.asHz(), t);
    const float midiNote = Interpolator::linear(lowFreq.asMidiNote(), hiFreq.asMidiNote(), t);
    const float logFreq = Frequency::ofMidiNote(midiNote).asHz();
    // we lerp from logFreq up to linFreq because log spacing clusters frequencies
    // towards the bottom of the range, which means that when holding down the mouse on a string
    // and lowering this param, you'll hear the pitch drop, which makes more sense than vice-versa.
    return Interpolator::linear(logFreq, linFreq, linLogLerp);
  }

private:
  void pluck(SpectralSignalGenerator* spectrum, float location, float amp)
  {
    const int   numBands = roundf(bandDensity);
    const int   band = roundf(Interpolator::linear(0, numBands, location));
    const float freq = frequencyOfString(band, numBands, bandFirst, bandLast, linLogLerp);
    spectrum->pluck(freq, amp);
  }

  void pluck(SpectralSignalGenerator* spectrum, MidiMessage msg)
  {
    float freq = Frequency::ofMidiNote(msg.getNote()).asHz();
    float amp = msg.getVelocity() / 127.0f;
    spectrum->pluck(freq, amp);
  }
}; 
