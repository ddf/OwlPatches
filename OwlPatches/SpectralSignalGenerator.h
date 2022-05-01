#include "SignalGenerator.h"
#include "Window.h"
#include "SimpleArray.h"
#include "FloatArray.h"
#include "ComplexFloatArray.h"
#include "ExponentialDecayEnvelope.h"
#include "Interpolator.h"

#include "FastFourierTransform.h"
typedef FastFourierTransform FFT;

//#include "KissFFT.h"
//typedef KissFFT FFT;

static const int kSpectralBandPartials = 1;

template<bool linearDecay = true>
class SpectralSignalGenerator : public SignalGenerator
{
  struct Band
  {
    // the frequency of this band, for faster conversion between index and frequency
    float frequency;
    float amplitude;
    float decay;
    float phase;
    ComplexFloat complex[2];
    int   partials[kSpectralBandPartials];
  };

  FFT* fft;
  Window window;

  SimpleArray<Band> bands;
  float decayDec;
  float spread;
  float brightness;

  FloatArray specBright;
  FloatArray specSpread;

  FloatArray specMag;
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
  const int   spectralMagnitude;
  const float spreadBandsMax;

  const int outIndexMask;

public:
  SpectralSignalGenerator(FFT* fft, float sampleRate, 
                          // these need to all be the same length
                          Band* bandsData, float* specBrightData, float* specSpreadData, float* specMagData, int specSize,
                          // these need to all be the same length
                          ComplexFloat* complexData, float* outputDataA, float* outputDataB, float* windowData, int blockSize)
    : fft(fft), window(windowData, blockSize), bands(bandsData, specSize), sampleRate(sampleRate), oneOverSampleRate(1.0f/sampleRate)
    , bandWidth((2.0f / blockSize) * (sampleRate / 2.0f)), halfBandWidth(bandWidth/2.0f)
    , overlapSize(blockSize/2), overlapSizeHalf(overlapSize/2), overlapSizeMask(overlapSize-1), spectralMagnitude(blockSize/64)
    , specBright(specBrightData, specSize), specSpread(specSpreadData, specSize), specMag(specMagData, specSize)
    , complex(complexData, blockSize), outputBufferA(outputDataA, blockSize), outputBufferB(outputDataB, blockSize)
    , outIndexA(0), outIndexB(blockSize/2), outIndexMask(blockSize-1), phaseIdx(0)
    , spread(0), spreadBandsMax(specSize/4), brightness(0)
  {
    setDecay(1.0f);
    for (int i = 0; i < specSize; ++i)
    {
      bands[i].frequency = frequencyForIndex(i);
      bands[i].amplitude = 0;
      bands[i].phase = randf()*M_PI*2;
      bands[i].complex[0].setPolar(1.0f, bands[i].phase);
      // ODD bands need to be 180 out of phase every other buffer generation
      // because our overlap is half the size of the buffer generated.
      // this ensure that phase lines up for those sinusoids in every buffer.
      if (i % 2 == 1)
      {
        bands[i].complex[1].setPolar(1.0f, bands[i].phase + M_PI);
      }
      else
      {
        bands[i].complex[1] = bands[i].complex[0];
      }

      for (int p = 0; p < kSpectralBandPartials; ++p)
      {
        bands[i].partials[p] = freqToIndex(bands[i].frequency*(2 + p));
      }
    }
    specSpread.clear();
    specMag.clear();
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

  void pluck(float freq, float amp)
  {
    const int bidx = freqToIndex(freq);
    if (bidx > 0 && bidx < bands.getSize())
    {
      bands[bidx].amplitude = amp;
      bands[bidx].decay = 1;
    }
  }

  //float generate() override
  //{
  //  const int op = outIndex & overlapSizeMask;

  //  // transfer bands into spread array halfway through the overlap
  //  // so that we do this work in a different block than synthesis
  //  if (op == overlapSizeHalf)
  //  {
  //    fillSpread();
  //  }
  //  else if (op == 0)
  //  {
  //    fillComplex();

  //    fft->ifft(complex, inverse);
  //    window.apply(inverse);

  //    for (int s = 0; s < inverse.getSize(); ++s)
  //    {
  //      int ind = (s + outIndex) & outIndexMask;

  //      outputBuffer[ind] += inverse[s];
  //    }

  //    bufferIdx ^= 1;
  //  }

  //  float result = outputBuffer[outIndex];
  //  outputBuffer[outIndex++] = 0;
  //  outIndex &= outIndexMask;

  //  return result;
  //}

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
    float* magData = new float[specSize];
    float* outputA = new float[blockSize];
    float* outputB = new float[blockSize];
    Window window  = Window::create(Window::TriangularWindow, blockSize);
    ComplexFloat* complexData = new ComplexFloat[blockSize];
    return new SpectralSignalGenerator(FFT::create(blockSize), sampleRate,
      bandsData, brightData, spreadData, magData, specSize,
      complexData, outputA, outputB, window.getData(), blockSize
    );
  }

  static void destroy(SpectralSignalGenerator* spectralGen)
  {
    FFT::destroy(spectralGen->fft);
    delete[] spectralGen->bands.getData();
    delete[] spectralGen->specBright.getData();
    delete[] spectralGen->specSpread.getData();
    delete[] spectralGen->specMag.getData();
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
    int idx = freqToIndex(freq);
    // get from bands array for phase
    Band b = bands[idx];
    // set normalized amplitude based on magnitude array (which includes spread and brightness)
    b.amplitude = specMag[idx] / spectralMagnitude;
    return b;
  }

  float getMagnitudeMean()
  {
    return specMag.getMean() / spectralMagnitude;
  }

private:
  ExponentialDecayEnvelope falloffEnv;

  void addSinusoidWithSpread(const int idx, const float amp, const int lidx, const int hidx)
  {
    specSpread[idx] += amp;

    if (lidx < idx)
    {
      falloffEnv.setDecaySamples(idx - lidx + 1);
      falloffEnv.setLevel(1);
      falloffEnv.generate();
      for (int bidx = idx-1; bidx >= lidx && bidx > 0; --bidx)
      {
        specSpread[bidx] += amp * falloffEnv.generate();
      }
    }

    if (hidx > idx)
    {
      falloffEnv.setDecaySamples(hidx - idx + 1);
      falloffEnv.setLevel(1);
      falloffEnv.generate();
      for (int bidx = idx + 1; bidx <= hidx && bidx < bands.getSize(); ++bidx)
      {
        specSpread[bidx] += amp * falloffEnv.generate();
      }
    }

    //const int range = idx - lidx;
    //for (int bidx = lidx; bidx <= hidx; ++bidx)
    //{
    //  if (bidx > 0 && bidx < bands.getSize())
    //  {
    //    if (bidx == idx)
    //    {
    //      specSpread[bidx] += amp;
    //    }
    //    else
    //    {
    //      const float fn = fabsf(bidx - idx) / range;
    //      // exponential fall off from the center, see: https://www.desmos.com/calculator/gzqrz4isyb
    //      specSpread[bidx] += amp * exp(-10 * fn);
    //    }
    //  }
    //}
  }

  void addSinusoidWithSpread(float bandFreq, float amp)
  {
    // get low and high frequencies for spread
    const int midx = freqToIndex(bandFreq);
    const int lidx = midx - spreadBandsMax * spread; // freqToIndex(bandFreq - bandFreq * 0.5f*spread);
    const int hidx = midx + spreadBandsMax * spread; // freqToIndex(bandFreq + bandFreq * spread);
    addSinusoidWithSpread(midx, amp, lidx, hidx);
  }

  void fillComplex()
  {
    const int specSize = bands.getSize();

    complex.clear();

    for (int i = 1; i < specSize; ++i)
    {
      // grab the magnitude as set by our pluck with spread pass
      float a = specSpread[i];

      // copy accumulated result into the magnitude array, scaling by our max amplitude
      specMag[i] = fmin(a * spectralMagnitude, spectralMagnitude);

      // #TODO probably sounds better to do the pitch-shift here?
      // At this point we have gAnaMagn and gAnaFreq from
      // http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/

      // done with this band, we can construct the complex representation.
      complex[i] = bands[i].complex[phaseIdx] * specMag[i];
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
      processBand(i);
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

  void processBand(int idx)
  {
    Band& b = bands[idx];
    if (linearDecay)
    {
      b.decay = b.decay > decayDec ? b.decay - decayDec : 0;
    }
    else
    {
      b.decay *= decayDec;
    }
    //if (b.decay > 0)
    {
      float a = b.decay*b.amplitude;
      specBright[idx] += a;
      //addSinusoidWithSpread(b.frequency, a);
      // add brightness if we have a decent signal to work with
      static const float epsilon = 0.01f;
      for (int i = 0; i < kSpectralBandPartials; ++i)
      {
        int p = 2 + i;
        a *= brightness;
        int pidx = b.partials[i];
        if (pidx < specBright.getSize())
        {
          specBright[pidx] += a / p;
        }
        //addSinusoidWithSpread(b.partials[i], a / p);
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
