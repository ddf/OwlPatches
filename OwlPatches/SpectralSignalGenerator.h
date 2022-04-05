#include "SignalGenerator.h"
#include "FastFourierTransform.h"
#include "Window.h"
#include "SimpleArray.h"
#include "FloatArray.h"
#include "ComplexFloatArray.h"

class SpectralSignalGenerator : public SignalGenerator
{
  struct Band
  {
    float amplitude;
    float decay;
    float phase;
    ComplexFloat complex[2];
  };

  FastFourierTransform* fft;
  Window window;

  SimpleArray<Band> bands;
  float decaySeconds;
  float spread;
  float brightness;

  FloatArray specSpread;
  FloatArray specMag;
  ComplexFloatArray complex;
  FloatArray inverse;
  FloatArray output;
  int outIndex;
  int phaseIdx;

  const float sampleRate;
  const float bandWidth;
  const int   overlapSize;
  const int   spectralMagnitude;

public:
  SpectralSignalGenerator(FastFourierTransform* fft, float sampleRate, 
                          Band* bandsData, float* specSpreadData, float* specMagData, int specSize,
                          ComplexFloat* complexData, float* inverseData, int blockSize,
                          float* windowData, int windowSize,
                          float* outputData, int outputSize)
    : fft(fft), window(windowData, windowSize), bands(bandsData, specSize), sampleRate(sampleRate)
    , bandWidth((2.0f / blockSize) * (sampleRate / 2.0f))
    , overlapSize(blockSize/2), spectralMagnitude(blockSize / 32)
    , specSpread(specSpreadData, specSize), specMag(specMagData, specSize)
    , complex(complexData, blockSize), inverse(inverseData, blockSize)
    , output(outputData, outputSize), outIndex(0), phaseIdx(0)
    , decaySeconds(1.0f), spread(0), brightness(0)
  {
    for (int i = 0; i < specSize; ++i)
    {
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
    }
    specSpread.clear();
    specMag.clear();
    complex.clear();
    inverse.clear();
    output.clear();
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

  float generate() override
  {
    bool generateOutput = outIndex % overlapSize == 0;
    if (generateOutput)
    {
      // amplitude needs to decrease by 1 / (decaySeconds * sampleRate()) every sample.
      // eg decaySeconds == 1 -> 1 / sampleRate()
      //    decaySeconds == 0.5 -> 1 / (0.5 * sampleRate), which is twice as fast, equivalent to 2 / sampleRate()
      // since we generate a new buffer every overlapSize samples, we multiply that rate by overlapSize, giving:
      const float decayDec = overlapSize / (decaySeconds * sampleRate);
      const float halfSpread = spread * 0.5f;
      const float freqMult = 1.0f;
      const int specSize = bands.getSize();

      specSpread.clear();
      complex.clear();

      for (int i = 1; i < specSize; ++i)
      {
        Band& b = bands[i];
        b.decay = b.decay > decayDec ? b.decay - decayDec : 0;
        if (b.decay > 0)
        {
          const float a = b.decay*b.amplitude;
          const float bandFreq = indexToFreq(i) * freqMult;
          // get low and high frequencies for spread
          const int midx = freqToIndex(bandFreq);
          const int lidx = freqToIndex(bandFreq - halfSpread);
          const int hidx = freqToIndex(bandFreq + halfSpread);
          addSinusoidWithSpread(midx, a, lidx, hidx);
        }
      }

      for (int i = 1; i < specSize; ++i)
      {
        // grab the magnitude as set by our pluck with spread pass
        float a = specSpread[i];
        // copy into the complex spectrum where we will accumulate brightness.
        // it's += because we may have already accumulated some brightness here from a previous band.
        complex[i].re += a;
        // add brightness if we have a decent signal to work with
        static const float epsilon = 0.0001f;
        if (a > epsilon)
        {
          const float bandFreq = indexToFreq(i);
          int partial = 2;
          float partialFreq = bandFreq * partial;
          int pidx = freqToIndex(bandFreq*partial);
          a *= brightness;
          while (pidx < specSize && a > epsilon)
          {
            // accumulate into the complex spectrum
            complex[pidx].re += a / partial;
            ++partial;
            partialFreq = bandFreq * partial;
            pidx = freqToIndex(bandFreq*partial);
            a *= brightness;
          }
        }

        // copy accumulated result into the magnitude array, scaling by our max amplitude
        specMag[i] = fmin(complex[i].re * spectralMagnitude, spectralMagnitude);

        // #TODO probably sounds better to do the pitch-shift here?
        // At this point we have gAnaMagn and gAnaFreq from
        // http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/

        // done with this band, we can construct the complex representation.
        complex[i] = bands[i].complex[phaseIdx] * specMag[i];
      }

      fft->ifft(complex, inverse);
      window.apply(inverse);

      for (int s = 0; s < inverse.getSize(); ++s)
      {
        int ind = (s + outIndex) % output.getSize();

        output[ind] += inverse[s];
      }

      if (phaseIdx == 0) phaseIdx = 1;
      else phaseIdx = 0;
    }

    float result = output[outIndex];
    output[outIndex] = 0;
    outIndex = (outIndex + 1) % output.getSize();

    return result;
  }

  using SignalGenerator::generate;

  static SpectralSignalGenerator* create(int blockSize, float sampleRate)
  {
    const int specSize = blockSize / 2;
    const int outputSize = blockSize * 2;
    Band* bandsData = new Band[specSize];
    float* spreadData = new float[specSize];
    float* magData = new float[specSize];
    float* inverseData = new float[blockSize];
    ComplexFloat* complexData = new ComplexFloat[blockSize];
    float* outputData = new float[outputSize];
    Window window = Window::create(Window::TriangularWindow, blockSize);
    return new SpectralSignalGenerator(FastFourierTransform::create(blockSize), sampleRate,
      bandsData, spreadData, magData, specSize,
      complexData, inverseData, blockSize,
      window.getData(), blockSize,
      outputData, outputSize
    );
  }

  static void destroy(SpectralSignalGenerator* spectralGen)
  {
    FastFourierTransform::destroy(spectralGen->fft);
    delete[] spectralGen->bands.getData();
    delete[] spectralGen->specSpread.getData();
    delete[] spectralGen->specMag.getData();
    delete[] spectralGen->inverse.getData();
    delete[] spectralGen->complex.getData();
    delete[] spectralGen->output.getData();
    delete[] spectralGen->window.getData();
    delete spectralGen;
  }

private:
  void addSinusoidWithSpread(const int idx, const float amp, const int lidx, const int hidx)
  {
    const int range = idx - lidx;
    for (int bidx = lidx; bidx <= hidx; ++bidx)
    {
      if (bidx > 0 && bidx < bands.getSize())
      {
        if (bidx == idx)
        {
          specSpread[bidx] += amp;
        }
        else
        {
          const float fn = fabsf(bidx - idx) / range;
          // exponential fall off from the center, see: https://www.desmos.com/calculator/gzqrz4isyb
          specSpread[bidx] += amp * exp(-10 * fn);
        }
      }
    }
  }

  float indexToFreq(int i) const
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

  int freqToIndex(float freq)
  {
    // special case: freq is lower than the bandwidth of spectrum[0] but not negative
    if (freq > 0 && freq < bandWidth / 2) return 0;
    // all other cases
    const float fraction = freq / sampleRate;
    // roundf is not available in windows, so we do this
    const int i = (int)floorf((float)fft->getSize() * fraction + 0.5f);
    return i;
  }
};
