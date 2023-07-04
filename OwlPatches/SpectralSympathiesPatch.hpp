/**

AUTHOR:
    (c) 2023 Damien Quartz

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
    Synthesizes sound by using overlap-add IFFT synthesis of spectral data.
    Send audio to L In to excite a portion of the spectrum using the 
    Fundamental, Octaves, Density, and Tuning settings. Fundamental
    and Octaves are used to determine the portion of the spectrum that
    L In excites (shown at the top of the screen in Hz). Density determines
    how many "strings" are available in that range, acting like a kind of
    comb filter on the input. Tuning determines how strings are spaced within
    the frequency range from logarithmic to linear. Decay controls how 
    quickly strings decay to silence after being excited, Spread will excite 
    nearby strings, Brightness fades in overtones of excited strings, and Crush
    reduces the sample rate of the output. Width stereoizes the output with a diffuser,
    which is followed by reverb with controls for blend, time, and tone.
*/

#include "MonochromeScreenPatch.h"
#include "MidiMessage.h"
#include "SpectralSignalGenerator.h"
#include "BitCrusher.hpp"
#include "Diffuser.h"
#include "Reverb.h"
#include "Frequency.h"
#include "Interpolator.h"
#include "Easing.h"
#include "SmoothValue.h"
#include "Window.h"

struct SpectralSympathiesParameterIds
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
};

template<int spectrumSize>
class SpectralSympathiesPatch : public MonochromeScreenPatch
{
  using SpectralGen = SpectralSignalGenerator<false>;
  using BitCrush = BitCrusher<24>;

protected:
  const SpectralSympathiesParameterIds params;

  const float spreadMax = 1.0f;
  const float decayMin;
  const float decayMax;
  const float decayDefault = 0.5f;
  const int   densityMin = 24;
  const int   densityMax = 127;
  const float octavesMin = 2;
  const float octavesMax = 8;
  const int   fundamentalNoteMin = 36;
  const int   fundaMentalNoteMax = 128 - octavesMin * 12;
  const float bandMin = Frequency::ofMidiNote(fundamentalNoteMin).asHz();
  const float bandMax = Frequency::ofMidiNote(128).asHz();
  const float crushRateMin = 1000.0f;

  int inputBufferWrite;
  float inputAtten;
  FloatArray inputBuffer;
  Window inputWindow;
  FloatArray inputAnalyze;
  ComplexFloatArray inputSpectrum;
  FastFourierTransform* inputTransform;

  SpectralGen* spectralGen;
  BitCrush* bitCrusher;
  Diffuser* diffuser;

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

public:

  SpectralSympathiesPatch(SpectralSympathiesParameterIds paramIds) : MonochromeScreenPatch()
    , params(paramIds), inputBufferWrite(0), inputAtten(0)
    , decayMin((float)spectrumSize*0.5f / getSampleRate()), decayMax(10.0f)
    , bandFirst(1.f), bandLast(1.f)
  {
    inputBuffer = FloatArray::create(spectrumSize);
    inputWindow = Window::create(Window::HanningWindow, spectrumSize);
    inputAnalyze = FloatArray::create(spectrumSize);
    inputSpectrum = ComplexFloatArray::create(spectrumSize);
    inputTransform = FastFourierTransform::create(spectrumSize);

    spectralGen = SpectralGen::create(spectrumSize, getSampleRate());
    bitCrusher = BitCrush::create(getSampleRate(), getSampleRate());
    diffuser = Diffuser::create();

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
    registerParameter(params.inWidth, "Width");

    setParameterValue(params.inHarpFundamental, 0.0f);
    setParameterValue(params.inHarpOctaves, 1.0f);
    setParameterValue(params.inDecay, (decayDefault - decayMin) / (decayMax - decayMin));
    setParameterValue(params.inDensity, 1.0f);
    setParameterValue(params.inSpread, 0.0f);
    setParameterValue(params.inBrightness, 0.0f);
    setParameterValue(params.inCrush, 0.0f);
    setParameterValue(params.inTuning, 0.0f);
    setParameterValue(params.inWidth, 0.0f);
  }

  ~SpectralSympathiesPatch()
  {
    FloatArray::destroy(inputBuffer);
    Window::destroy(inputWindow);
    FloatArray::destroy(inputAnalyze);
    ComplexFloatArray::destroy(inputSpectrum);
    FastFourierTransform::destroy(inputTransform);
    SpectralGen::destroy(spectralGen);
    BitCrush::destroy(bitCrusher);
    Diffuser::destroy(diffuser);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples)
  {
  }

  void processMidi(MidiMessage msg) override
  {
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int blockSize = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    float harpFund = Interpolator::linear(fundamentalNoteMin, fundaMentalNoteMax, getParameterValue(params.inHarpFundamental));
    float harpOctaves = Interpolator::linear(octavesMin, octavesMax, getParameterValue(params.inHarpOctaves));
    bandFirst = Frequency::ofMidiNote(harpFund).asHz();
    bandLast = fmin(Frequency::ofMidiNote(harpFund + harpOctaves * MIDIOCTAVE).asHz(), bandMax);
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

    // probably this means applying attenuation to inputAnalyze instead of while we record.
    // for the purposes of input attenuation we ignore linlog
    const float bandSpacing = (bandLast - bandFirst) / getStringCount();
    const float maxSpacing = (bandMax - bandMin) / densityMin;
    const float attenT = (bandSpacing - spectralGen->getBandWidth()) / (maxSpacing - spectralGen->getBandWidth());
    inputAtten = Interpolator::linear(1.0f / 512.0f, 1.0f / 64.0f, attenT);
    for (int i = 0; i < blockSize; ++i)
    {
      inputBuffer[inputBufferWrite++] = left[i];
      if (inputBufferWrite == spectrumSize)
      {
        // window the input and output to an analysis buffer
        // because running the fft messes up the input samples.
        inputWindow.process(inputBuffer, inputAnalyze);
        inputAnalyze.multiply(inputAtten);
        inputTransform->fft(inputAnalyze, inputSpectrum);
        const int stringCount = getStringCount();
        for(int s = 0; s < stringCount; ++s)
        {
          const float freq = frequencyOfString(s);
          const int bidx = spectralGen->freqToIndex(freq);
          const float inMag = inputSpectrum[bidx].getMagnitude();
          const float inPhase = inputSpectrum[bidx].getPhase();
          spectralGen->excite(bidx, inMag, inPhase);
        }
        // copy the back half of the array to the front half
        // continue recording input from the middle of the array.
        // doing this means we can update the spectral data for sound generation every overlap.
        inputBufferWrite = spectrumSize / 2;
        inputBuffer.copyFrom(inputBuffer.subArray(inputBufferWrite, spectrumSize / 2));
      }
    }

    spectralGen->generate(left);
    bitCrusher->process(left, left);

    left.copyTo(right);

    stereoWidth = getParameterValue(params.inWidth);
    diffuser->setAmount(stereoWidth);
    diffuser->process(audio, audio);
  }

  virtual void processScreen(MonochromeScreenBuffer& screen) override {}

protected:
  // get the current string count based on the density setting
  int getStringCount()
  {
    return (int)(bandDensity + 0.5f);
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
};
