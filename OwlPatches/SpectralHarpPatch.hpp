#include "Patch.h"
#include "SpectralSignalGenerator.h"
#include "Frequency.h"
#include "Interpolator.h"

template<int spectrumSize, typename PatchClass>
class SpectralHarpPatch : public PatchClass
{
protected:
  static const PatchParameterId inPitch = PARAMETER_A;
  static const PatchParameterId inSpread = PARAMETER_B;
  static const PatchParameterId inDecay = PARAMETER_C;
  static const PatchParameterId inBrightness = PARAMETER_D;
  static const PatchParameterId inTuning = PARAMETER_E;
  static const PatchParameterId inDensity = PARAMETER_F;

  const float spreadMax = 13000.0f;
  const float decayMin = 0.150f;
  const float decayMax = 5.0f;
  const float decayDefault = 0.5f;
  const int   densityMin = 12.0f;
  const int   densityMax = 121.0f;
  const float bandFirst = 64;
  const float bandLast = 13000;

  SpectralSignalGenerator* spectralGen;

  float       pluckCenter;
  SmoothFloat spread;
  SmoothFloat decay;
  SmoothFloat brightness;
  SmoothFloat linLogLerp;
  SmoothFloat bandDensity;

public:

  using PatchClass::registerParameter;
  using PatchClass::getParameterValue;
  using PatchClass::setParameterValue;
  using PatchClass::getSampleRate;
  using PatchClass::isButtonPressed;

  SpectralHarpPatch() : PatchClass()
  {
    spectralGen = SpectralSignalGenerator::create(spectrumSize, getSampleRate());

    registerParameter(inPitch, "Pitch");
    registerParameter(inSpread, "Spread");
    registerParameter(inDecay, "Decay");
    registerParameter(inBrightness, "Brightness");
    registerParameter(inTuning, "Tuning");
    registerParameter(inDensity, "Density");

    setParameterValue(inDecay, (decayDefault - decayMin) / (decayMax - decayMin));
    setParameterValue(inDensity, 1.0f);
  }

  ~SpectralHarpPatch()
  {
    SpectralSignalGenerator::destroy(spectralGen);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int blockSize = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    pluckCenter = getParameterValue(inPitch);
    spread = getParameterValue(inSpread)*spreadMax;
    decay = Interpolator::linear(decayMin, decayMax, getParameterValue(inDecay));
    brightness = getParameterValue(inBrightness);
    linLogLerp = getParameterValue(inTuning);
    bandDensity = Interpolator::linear(densityMin, densityMax, getParameterValue(inDensity));

    spectralGen->setSpread(spread);
    spectralGen->setDecay(decay);
    spectralGen->setBrightness(brightness);

    if (isButtonPressed(BUTTON_1))
    {
      //pluck(spectralGen, pluckCenter);
      for (int i = 0; i < blockSize; ++i)
      {
        float location = left[i] * 0.5f + 0.5f;
        float amplitude = right[i] * 0.5f + 0.5f;
        pluck(spectralGen, location, amplitude);
      }
    }

    spectralGen->generate(left);
    left.copyTo(right);
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

};
