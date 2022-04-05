#include "Patch.h"
#include "SpectralSignalGenerator.h"
#include "Frequency.h"
#include "Interpolator.h"

class SpectralHarpPatch : public Patch
{
  static const PatchParameterId inTune = PARAMETER_A;

  SpectralSignalGenerator* spectralGen;

  SmoothFloat pluckCenter;

public:
  SpectralHarpPatch() : Patch()
  {
    spectralGen = SpectralSignalGenerator::create(4096, getSampleRate());

    registerParameter(inTune, "Tune");
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

    pluckCenter = getParameterValue(inTune);

    if (isButtonPressed(BUTTON_1))
    {
      pluck(spectralGen, pluckCenter);
      //for (int i = 0; i < blockSize; ++i)
      //{
      //  pluck(spectralGen, left[i] * 0.5f + 0.5f);
      //}
    }

    spectralGen->generate(left);
    left.copyTo(right);
  }

protected:
  float frequencyOfString(int stringNum, float stringCount, float lowFreqHz, float hiFreqHz, float linLogLerp)
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
  void pluck(SpectralSignalGenerator* spectrum, float location)
  {
    const float numBands = 288;
    if (numBands > 0)
    {
      const float lowBand = 64;
      const float hiBand = 13000;
      const float linLogLerp = 1.0f;
      for (int b = 0; b < numBands; ++b)
      {
        const float freq = frequencyOfString(b, numBands, lowBand, hiBand, linLogLerp);
        const float normBand = (float)b / numBands;
        if (fabsf(normBand - location) < 0.002f)
        {
          spectrum->pluck(freq, 1);
        }
      }
    }
  }

};
