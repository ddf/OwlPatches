#include "SignalGenerator.h"
#include "Window.h"
#include "SimpleArray.h"
#include "FloatArray.h"
#include "ComplexFloatArray.h"
#include "ExponentialDecayEnvelope.h"
#include "Interpolator.h"
#include "Easing.h"
#include "EqualLoudnessCurves.h"

#include "FastFourierTransform.h"
typedef FastFourierTransform FFT;

//#include "KissFFT.h"
//typedef KissFFT FFT;

static const int kSpectralBandPartials = 40;

template<bool linearDecay = true>
class SpectralSignalGenerator : public SignalGenerator
{
  struct Band
  {
    // the frequency of this band, for faster conversion between index and frequency
    float frequency;
    float amplitude;
    float phase;
    float weight;
    int   partials[kSpectralBandPartials];
  };

  FFT* fft;
  Window window;

  SimpleArray<Band> bands;
  float decayDec;
  float spread;
  float brightness;
  float volume;
  float spectralMagnitude;

  FloatArray specBright;
  FloatArray specSpread;

  ComplexFloatArray complex;
  FloatArray outputBufferA;
  FloatArray outputBufferB;
  int outIndexA;
  int outIndexB;
  int phaseIdx;

  const float sampleRate;
  const float oneOverSampleRate;
  const float bandWidth;
  const float halfBandWidth;
  const int   overlapSize;
  const int   overlapSizeHalf;
  const int   overlapSizeMask;
  const float spreadBandsMax;

  const int outIndexMask;

public:
  SpectralSignalGenerator(FFT* fft, float sampleRate, 
                          // these need to all be the same length
                          Band* bandsData, float* specBrightData, float* specSpreadData, int specSize,
                          // these need to all be the same length
                          ComplexFloat* complexData, float* outputDataA, float* outputDataB, float* windowData, int blockSize)
    : fft(fft), window(windowData, blockSize), bands(bandsData, specSize), sampleRate(sampleRate), oneOverSampleRate(1.0f/sampleRate)
    , bandWidth((2.0f / blockSize) * (sampleRate / 2.0f)), halfBandWidth(bandWidth/2.0f)
    , overlapSize(blockSize/2), overlapSizeHalf(overlapSize/2), overlapSizeMask(overlapSize-1), spectralMagnitude(blockSize/64)
    , specBright(specBrightData, specSize), specSpread(specSpreadData, specSize)
    , complex(complexData, blockSize), outputBufferA(outputDataA, blockSize), outputBufferB(outputDataB, blockSize)
    , outIndexA(0), outIndexB(blockSize/2), outIndexMask(blockSize-1), phaseIdx(0)
    , spread(0), spreadBandsMax(specSize/4), brightness(0)
  {
    setVolume(1.0f);
    setDecay(1.0f);
    for (int i = 0; i < specSize; ++i)
    {
      bands[i].frequency = frequencyForIndex(i);
      bands[i].amplitude = 0;
      bands[i].phase = randf()*M_PI*2;
      // boost low frequencies and attenuate high frequencies with an equal loudness curve.
      // attenuation of high frequencies is to try to prevent distortion that happens when 
      // the spectrum is particularly overloaded in the high end.
      bands[i].weight = bands[i].frequency < 1000.0f ? clamp(1.0f / elc::b(bands[i].frequency), 0.0f, 4.0f) : elc::b(bands[i].frequency);
     
      for (int p = 0; p < kSpectralBandPartials; ++p)
      {
        float partialFreq = bands[i].frequency*(2 + p);
        // only add partials most people can actually hear
        bands[i].partials[p] = partialFreq < 16000.0f ? freqToIndex(partialFreq) : blockSize;
      }
    }
    specSpread.clear();
    complex.clear();
    outputBufferA.clear();
    outputBufferB.clear();
  }

  void setSpread(float val)
  {
    spread = val;
  }

  void setDecay(float inSeconds)
  {
    // having a shorter decay than the overlap size doesn't make sense
    // and we also want to avoid divide-by-zero.
    float decaySeconds = fmax(overlapSize * oneOverSampleRate, inSeconds);
    if (linearDecay)
    {
      // amplitude needs to decrease by 1 / (decaySeconds * sampleRate()) every sample.
      // eg decaySeconds == 1 -> 1 / sampleRate()
      //    decaySeconds == 0.5 -> 1 / (0.5 * sampleRate), which is twice as fast, equivalent to 2 / sampleRate()
      // since we generate a new buffer every overlapSize samples, we multiply that rate by overlapSize, giving:
      decayDec = overlapSize / (decaySeconds * sampleRate);
    }
    else // exponential decay
    {
      float blockRate = sampleRate / overlapSize;
      float lengthInBlocks = decaySeconds * blockRate;
      decayDec = 1.0 + logf(0.0001f) / (lengthInBlocks + 20);
    }
  }

  void setBrightness(float amt)
  {
    brightness = amt;
  }

  void setVolume(float amt)
  {
    volume = clamp(amt, 0.0f, 1.0f);
  }

  void pluck(float freq, float amp)
  {
    const int bidx = freqToIndex(freq);
    if (bidx > 0 && bidx < bands.getSize())
    {
      bands[bidx].amplitude = amp;
    }
  }

  void excite(int bidx, float amp, float phase)
  {
    if (bidx > 0 && bidx < bands.getSize())
    {
      Band& b = bands[bidx];
      const float ea = amp;
      const float ba = b.amplitude;
      if (ea > ba)
      {
        b.amplitude = ba + 0.9f*(ea - ba);
      }
      b.phase = phase;
    }
  }

  void generate(FloatArray output) override
  {
    const int blockSize = complex.getSize();
    // transfer bands into spread array halfway through the overlap
    // so that we do this work in a different block than synthesis
    if (outIndexA+overlapSizeHalf == blockSize || outIndexB+overlapSizeHalf == blockSize)
    {
      fillSpread();
    }
    
    if (outIndexA == 0)
    {
      phaseIdx = 0;
      fillComplex();
      fft->ifft(complex, outputBufferA);
    }
    
    if (outIndexB == 0)
    {
      phaseIdx = 1;
      fillComplex();
      fft->ifft(complex, outputBufferB);
    }

    int size = output.getSize();
    float* out = output.getData();
    while (size--)
    {
      *out++ = outputBufferA[outIndexA] * window[outIndexA];
      outIndexA = (outIndexA+1) & outIndexMask;
    }

    size = output.getSize();
    out = output.getData();
    while (size--)
    {
      *out++ += outputBufferB[outIndexB] * window[outIndexB];
      outIndexB = (outIndexB+1) & outIndexMask;
    }
  }

  static SpectralSignalGenerator* create(int blockSize, float sampleRate)
  {
    const int specSize = blockSize / 2;
    Band* bandsData = new Band[specSize];
    float* brightData = new float[specSize];
    float* spreadData = new float[specSize];
    ComplexFloat* complexData = new ComplexFloat[blockSize];
    float* outputB = new float[blockSize];
    float* outputA = new float[blockSize];
    Window window = Window::create(Window::TriangularWindow, blockSize);
    return new SpectralSignalGenerator(FFT::create(blockSize), sampleRate,
      bandsData, brightData, spreadData, specSize,
      complexData, outputA, outputB, window.getData(), blockSize
    );
  }

  static void destroy(SpectralSignalGenerator* spectralGen)
  {
    FFT::destroy(spectralGen->fft);
    delete[] spectralGen->bands.getData();
    delete[] spectralGen->specBright.getData();
    delete[] spectralGen->specSpread.getData();
    delete[] spectralGen->outputBufferA.getData();
    delete[] spectralGen->outputBufferB.getData();
    delete[] spectralGen->window.getData();
    delete[] spectralGen->complex.getData();
    delete spectralGen;
  }

  float indexToFreq(int i)
  {
    return bands[i].frequency;
  }

  int freqToIndex(float freq)
  {
    // simplified version of below
    return freq > 0 && freq < halfBandWidth ? 0 : (int)((float)fft->getSize()*freq*oneOverSampleRate + 0.5f);

    //// special case: freq is lower than the bandwidth of spectrum[0] but not negative
    //if (freq > 0 && freq < halfBandWidth) return 0;
    //// all other cases
    //const float fraction = freq * oneOverSampleRate;
    //// roundf is not available in windows, so we do this
    //const int i = (int)((float)fft->getSize() * fraction + 0.5f);
    //return i;
  }

  Band getBand(float freq)
  {
    const int idx = freqToIndex(freq);
    // get from bands array for phase
    Band b = bands[idx];
    // set normalized amplitude based on magnitude array (which includes spread and brightness)
    b.amplitude = specSpread[idx];
    return b;
  }

  float getBandWidth() const { return bandWidth; }

  float getMagnitudeMean()
  {
    return specSpread.getMean();
  }

private:
  void fillComplex()
  {
    const int specSize = bands.getSize();

    complex.clear();

    spectralMagnitude = (complex.getSize() / 8.0f)*volume;
    for (int i = 1; i < specSize; ++i)
    {
      const Band& band = bands[i];
      // grab the magnitude as set by our pluck with spread pass, scale by spectral magnitude
      const float a = fmin(specSpread[i] * spectralMagnitude, spectralMagnitude);
      // construct the complex representation. 
      // odd bands need to be 180 degrees off phase every other overlap to prevent beating.
      complex[i].setPolar(a*band.weight, band.phase + (M_PI*phaseIdx*(i%2)));
    }
  }

  void fillSpread()
  {
    const float freqMult = 1.0f;
    const int specSize = bands.getSize();

    specBright.clear();
    specSpread.clear();

    for (int i = 1; i < specSize; ++i)
    {
      processBand(i, specSize);
    }

    // spread the raw bright spectrum with a sort of filter than runs forwards and backwards.
    // adapted from ExponentialDecayEnvelope
    float spreadMult = 1.0 + (logf(0.00001f) - logf(1.0f)) / (spreadBandsMax*spread + 12);
    spreadMult *= spreadMult;
    float pi = 0;
    float pj = 0;
    int count = specSize - 1;
    for (int i = 1; i < count; ++i)
    {
      float ci = specBright[i];
      specSpread[i] += ci + pi;
      pi = max(ci, pi)*spreadMult;

      // we don't add in bright on the backwards pass
      // because it gets added in the forward pass
      int j = count - i;
      float cj = specBright[j];
      specSpread[j] += pj;
      pj = max(cj, pj)*spreadMult;
    }
  }

  void processBand(int idx, int specSize)
  {
    Band& b = bands[idx];
    if (linearDecay)
    {
      // this is probably wrong, but keeping it for now
      b.amplitude = b.amplitude > decayDec ? b.amplitude - decayDec : 0;
    }
    else
    {
      b.amplitude *= decayDec;
    }

    {
      float a = b.amplitude;
      specBright[idx] += a;
      for (int i = 0; i < kSpectralBandPartials && b.partials[i] < specSize; ++i)
      {
        int p = 2 + i;
        a *= brightness;
        int pidx = b.partials[i];
        specBright[pidx] += a / p;
      }
    }
  }

  float frequencyForIndex(int i) const
  {
    // special case: the width of the first bin is half that of the others.
    //               so the center frequency is a quarter of the way.
    if (i == 0) return bandWidth * 0.25f;
    // special case: the width of the last bin is half that of the others.
    if (i == bands.getSize())
    {
      float lastBinBeginFreq = (sampleRate / 2) - (bandWidth / 2);
      float binHalfWidth = bandWidth * 0.25f;
      return lastBinBeginFreq + binHalfWidth;
    }
    // the center frequency of the ith band is simply i*bw
    // because the first band is half the width of all others.
    // treating it as if it wasn't offsets us to the middle 
    // of the band.
    return i * bandWidth;
  }
};
