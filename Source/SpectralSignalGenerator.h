#pragma once 

#include "ExponentialDecayEnvelope.h"
#include "EqualLoudnessCurves.h"
#include "vessicle/SpectralGenerator.h"
#include "vessicle/vessl/vessl.h"

//#include "KissFFT.h"
//typedef KissFFT FFT;

static const int kSpectralBandPartials = 40;

// @todo: the freq <-> index conversion functions are a little confused now.
// this is because I gave SpectralGen a 0-indexed frequency bands array, but band[0] is complex_bin[1].
// it will probably help to add the following to SpectralGenerator:
// get_band_frequency(size_t index) -> the center frequency of the complex_bin that band[index] maps to.
// get_band_index(analog_t frequency) -> the index of the band that "contains" the frequency.
// these can then be used instead of the translation functions built into this class.
template<vessl::size_t SpectrumSize, bool LinearDecay = true>
class SpectralSignalGenerator
{
  using size_t = vessl::size_t;
  using phase_t = vessl::phase_t;
  using SpectralGen = SpectralGenerator<float, SpectrumSize>;
  struct Band
  {
    // the frequency of this band, for faster conversion between index and frequency
    float frequency;
    float amplitude;
    float decay;
    int   partials[kSpectralBandPartials];
  };
  
  static constexpr vessl::size_t BandsSize = SpectrumSize >> 1;
  using BandArray = vessl::array<Band>;
  using SampleArray = vessl::array<float>;

  SpectralGen* generator_;

  BandArray bands_;
  float decay_dec_;
  float spread_;
  float brightness_;
  float volume_;
  float spectral_magnitude_;

  SampleArray spec_bright_;
  SampleArray spec_spread_;

  float sample_rate_;
  float one_over_sample_rate_;
  float band_width_;
  float half_band_width_;
  size_t overlap_size_;
  size_t overlap_size_half_;
  size_t overlap_size_mask_;
  float spread_bands_max_;

public:
  SpectralSignalGenerator(SpectralGen* spec_gen, float sample_rate, 
                          // these need to all be the same length
                          Band* bands_data, float* spec_bright_data, float* spec_spread_data)
    : generator_(spec_gen)
    , bands_(bands_data, BandsSize)
    , spread_(0)
    , brightness_(0)
    , spectral_magnitude_(SpectrumSize/64)
    , spec_bright_(spec_bright_data, BandsSize)
    , spec_spread_(spec_spread_data, BandsSize)
    , sample_rate_(sample_rate)
    , one_over_sample_rate_(1.0f/sample_rate)
    , band_width_((2.0f / SpectrumSize) * (sample_rate / 2.0f))
    , half_band_width_(band_width_/2.0f)
    , overlap_size_(BandsSize)
    , overlap_size_half_(overlap_size_/2)
    , overlap_size_mask_(overlap_size_-1)
    , spread_bands_max_(BandsSize/4)
  {
    setVolume(1.0f);
    setDecay(1.0f);
    for (int i = 0; i < BandsSize; ++i)
    {
      bands_[i].frequency = frequencyForIndex(i);
      bands_[i].amplitude = 0;
      // boost low frequencies and attenuate high frequencies with an equal loudness curve.
      // attenuation of high frequencies is to try to prevent distortion that happens when 
      // the spectrum is particularly overloaded in the high end.
      float weight = bands_[i].frequency < 1000.0f ? clamp(1.0f / elc::b(bands_[i].frequency), 0.0f, 4.0f) : elc::b(bands_[i].frequency);

      for (int p = 0; p < kSpectralBandPartials; ++p)
      {
        float partialFreq = bands_[i].frequency*(2 + p);
        // only add partials most people can actually hear
        bands_[i].partials[p] = partialFreq < 16000.0f ? freq_to_index(partialFreq) : SpectrumSize;
      }
    }
    spec_spread_.fill(0);
  }

  void setSpread(float val)
  {
    spread_ = val;
  }

  void setDecay(float in_seconds)
  {
    // having a shorter decay than the overlap size doesn't make sense
    // and we also want to avoid divide-by-zero.
    float decay_seconds = vessl::math::max(overlap_size_ * one_over_sample_rate_, in_seconds);
    if (LinearDecay)
    {
      // amplitude needs to decrease by 1 / (decaySeconds * sampleRate()) every sample.
      // eg decaySeconds == 1 -> 1 / sampleRate()
      //    decaySeconds == 0.5 -> 1 / (0.5 * sampleRate), which is twice as fast, equivalent to 2 / sampleRate()
      // since we generate a new buffer every overlapSize samples, we multiply that rate by overlapSize, giving:
      decay_dec_ = overlap_size_ / (decay_seconds * sample_rate_);
    }
    else // exponential decay
    {
      float block_rate = sample_rate_ / overlap_size_;
      float length_in_blocks = decay_seconds * block_rate;
      decay_dec_ = 1.0 + vessl::math::log(0.0001f) / (length_in_blocks + 20);
    }
  }

  void setBrightness(float amt)
  {
    brightness_ = amt;
  }

  void setVolume(float amt)
  {
    volume_ = clamp(amt, 0.0f, 1.0f);
  }

  void pluck(float freq, float amp)
  {
    const size_t bidx = freq_to_index(freq);
    if (bidx < bands_.size())
    {
      bands_[bidx].amplitude = amp;
      bands_[bidx].decay = 1;
    }
  }

  void excite(int bidx, float amp, float phase)
  {
    if (bidx >= 0 && bidx < bands_.size())
    {
      Band& b = bands_[bidx];
      const float ea = amp;
      const float ba = b.amplitude;
      if (ea > ba)
      {
        b.amplitude = ba + 0.9f*(ea - ba);
        b.decay = 1;
      }
      b.phase = phase;
    }
  }

  void generate(SampleArray output)
  {
    int size = output.size();
    float* out = output.data();
    while (size--)
    {
      // transfer bands into spread array halfway through the overlap
      // so that we do this work in a different block than synthesis
      if ( generator_->get_read_head(0)+overlap_size_half_ == SpectrumSize 
        || generator_->get_read_head(1)+overlap_size_half_ == SpectrumSize
      )
      {
        fill_spectrum();
      }
      
      *out++ = generator_->generate();
    }
  }

  static SpectralSignalGenerator* create(float sampleRate)
  {
    Band* bands_data = new Band[BandsSize];
    float* bright_data = new float[BandsSize];
    float* spread_data = new float[BandsSize];
    SpectralGen* spectral_gen = SpectralGen::create(sampleRate, vessl::sample::windows::type::triangle);
    return new SpectralSignalGenerator(spectral_gen, sampleRate, bands_data, bright_data, spread_data);
  }

  static void destroy(SpectralSignalGenerator* synth)
  {
    SpectralGen::destroy(synth->generator_);
    delete[] synth->bands_.data();
    delete[] synth->spec_bright_.data();
    delete[] synth->spec_spread_.data();
    delete synth;
  }

  float indexToFreq(int i)
  {
    return bands_[i].frequency;
  }
  
  float frequencyForIndex(int i) const
  {
    // special case: the width of the first bin is half that of the others.
    //               so the center frequency is a quarter of the way.
    if (i == 0) return band_width_ * 0.25f;
    // special case: the width of the last bin is half that of the others.
    if (i == bands_.size()-1)
    {
      float lastBinBeginFreq = (sample_rate_ / 2) - (band_width_ / 2);
      float binHalfWidth = band_width_ * 0.25f;
      return lastBinBeginFreq + binHalfWidth;
    }
    // the center frequency of the ith band is simply i*bw
    // because the first band is half the width of all others.
    // treating it as if it wasn't offsets us to the middle 
    // of the band.
    return i * band_width_;
  }

  size_t freq_to_index(float freq) const
  {
    //return freq >= half_band_width_ ? vessl::math::round((freq - half_band_width_) / band_width_) : 0;
    
    // simplified version of below
    return freq > 0 && freq < half_band_width_ ? 0 
    : static_cast<int>(vessl::math::round(static_cast<float>(SpectrumSize) * freq * one_over_sample_rate_));

    //// special case: freq is lower than the bandwidth of spectrum[0] but not negative
    //if (freq > 0 && freq < halfBandWidth) return 0;
    //// all other cases
    //const float fraction = freq * oneOverSampleRate;
    //// roundf is not available in windows, so we do this
    //const int i = (int)((float)fft->getSize() * fraction + 0.5f);
    //return i;
  }

  typename SpectralGen::frequency_band getBand(float freq) const
  {
    const size_t idx = freq_to_index(freq);
    // get from band generator for phase
    typename SpectralGen::frequency_band band = generator_->get_band(idx);
    // set normalized amplitude based on magnitude array (which includes spread and brightness)
    band.amplitude /= spectral_magnitude_;
    return band;
  }

  float getMagnitudeMean()
  {
    float accum = 0;
    for (int i = 0; i < BandsSize; ++i)
    {
      accum += generator_->get_band(i).magnitude;
    }
    return (accum / BandsSize) / spectral_magnitude_;
  }

private:
  ExponentialDecayEnvelope falloffEnv;

  void addSinusoidWithSpread(const int idx, const float amp, const int lidx, const int hidx)
  {
    spec_spread_[idx] += amp;

    if (lidx < idx)
    {
      falloffEnv.setDecaySamples(idx - lidx + 1);
      falloffEnv.setLevel(1);
      falloffEnv.generate();
      for (int bidx = idx-1; bidx >= lidx && bidx > 0; --bidx)
      {
        spec_spread_[bidx] += amp * falloffEnv.generate();
      }
    }

    if (hidx > idx)
    {
      falloffEnv.setDecaySamples(hidx - idx + 1);
      falloffEnv.setLevel(1);
      falloffEnv.generate();
      for (int bidx = idx + 1; bidx <= hidx && bidx < bands_.getSize(); ++bidx)
      {
        spec_spread_[bidx] += amp * falloffEnv.generate();
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
    const int midx = freq_to_index(bandFreq);
    const int lidx = midx - spread_bands_max_ * spread_; // freqToIndex(bandFreq - bandFreq * 0.5f*spread);
    const int hidx = midx + spread_bands_max_ * spread_; // freqToIndex(bandFreq + bandFreq * spread);
    addSinusoidWithSpread(midx, amp, lidx, hidx);
  }

  void fill_spectrum()
  {
    const float freqMult = 1.0f;

    spec_bright_.fill(0);
    spec_spread_.fill(0);

    for (size_t i = 0; i < BandsSize; ++i)
    {
      processBand(i, BandsSize);
    }

    // spread the raw bright spectrum with a sort of filter than runs forwards and backwards.
    // adapted from ExponentialDecayEnvelope
    float spread_mult = 1.0 + (vessl::math::log(0.00001f) - vessl::math::log(1.0f)) / (spread_bands_max_*spread_ + 12);
    spread_mult *= spread_mult;
    float pi = 0;
    float pj = 0;
    size_t count = spec_bright_.size();
    for (size_t i = 0; i < count; ++i)
    {
      float ci = spec_bright_[i];
      spec_spread_[i] += ci + pi;
      pi = vessl::math::max(ci, pi)*spread_mult;
    
      // we don't add in bright on the backwards pass
      // because it gets added in the forward pass
      size_t j = count - 1 - i;
      float cj = spec_bright_[j];
      spec_spread_[j] += pj;
      pj = vessl::math::max(cj, pj)*spread_mult;
    }
    
    spectral_magnitude_ = static_cast<float>(BandsSize / 8)*volume_;  // NOLINT(bugprone-integer-division)
    for (size_t i = 0; i < BandsSize; ++i)
    {
      // grab the magnitude as set by our pluck with spread pass
      const float a = vessl::math::min(spec_spread_[i] * spectral_magnitude_, spectral_magnitude_);
      //const float a = vessl::math::min(bands_[i].amplitude * spectral_magnitude_, spectral_magnitude_);
      
      // copy result into the generator's band magnitudes
      auto& gen_band = generator_->get_band(i);
      gen_band.magnitude = a;

      // #TODO probably sounds better to do the pitch-shift here?
      // At this point we have gAnaMagn and gAnaFreq from
      // http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
    }
  }

  void processBand(int idx, int specSize)
  {
    Band& b = bands_[idx];
    if (LinearDecay)
    {
      b.decay = b.decay > decay_dec_ ? b.decay - decay_dec_ : 0;
    }
    else
    {
      //b.decay *= decayDec;
      b.amplitude *= decay_dec_;
    }
    
    //if (b.decay > 0)
    {
      //float a = b.decay*b.amplitude;
      float a = b.amplitude;
      spec_bright_[idx] += a;
      // @todo brightness is causing glitching :(
      constexpr int iters = kSpectralBandPartials;
      for (int i = 0; i < iters && b.partials[i] < specSize; ++i)
      {
        int p = 2 + i;
        a *= brightness_;
        int pidx = b.partials[i];
        spec_bright_[pidx] += a / p;
      }
    }
  }
};
