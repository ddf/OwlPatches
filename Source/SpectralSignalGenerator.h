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
    float amplitude;
    float decay;
    int   partials[kSpectralBandPartials];
  };
  
  using BandArray = vessl::array<Band>;
  using SampleArray = vessl::array<float>;

  SpectralGen* generator_;

  BandArray bands_;
  float decay_dec_;
  float spread_;
  float brightness_;
  float volume_;

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
  SpectralSignalGenerator(SpectralGen* spec_gen, float sample_rate, size_t bands_size,
                          // should be at least bands_size long.
                          Band* bands_data, float* spec_bright_data, float* spec_spread_data)
    : generator_(spec_gen)
    , bands_(bands_data, bands_size)
    , spread_(0)
    , brightness_(0)
    , spec_bright_(spec_bright_data, bands_size)
    , spec_spread_(spec_spread_data, bands_size)
    , sample_rate_(sample_rate)
    , one_over_sample_rate_(1.0f/sample_rate)
    , band_width_((2.0f / SpectrumSize) * (sample_rate / 2.0f))
    , half_band_width_(band_width_/2.0f)
    , overlap_size_(SpectrumSize/2)
    , overlap_size_half_(overlap_size_/2)
    , overlap_size_mask_(overlap_size_-1)
    , spread_bands_max_(SpectrumSize/8)
  {
    setVolume(1.0f);
    setDecay(1.0f);
    for (int i = 1; i < bands_size; ++i)
    {
      const float band_freq = generator_->get_band_frequency(i);
      bands_[i].amplitude = 0;
      // boost low frequencies and attenuate high frequencies with an equal loudness curve.
      // attenuation of high frequencies is to try to prevent distortion that happens when 
      // the spectrum is particularly overloaded in the high end.
      float weight = band_freq < 1000.0f ? clamp(1.0f / elc::b(band_freq), 0.0f, 4.0f) : elc::b(band_freq);

      for (int p = 0; p < kSpectralBandPartials; ++p)
      {
        float partial_freq = band_freq*(2 + p);
        // only add partials most people can actually hear
        bands_[i].partials[p] = partial_freq < 16000.0f ? generator_->get_band_index(partial_freq) : SpectrumSize;
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
    const size_t bidx = generator_->get_band_index(freq);
    if (bidx > 0 && bidx < bands_.size())
    {
      bands_[bidx].amplitude = amp;
      bands_[bidx].decay = 1;
    }
  }

  void excite(int bidx, float amp, float phase)
  {
    if (bidx > 0 && bidx < bands_.size())
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
  
  VESSL_INLINE size_t get_band_index(float frequency)
  {
    return generator_->get_band_index(frequency);
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
    SpectralGen* spectral_gen = SpectralGen::create(sampleRate, vessl::sample::windows::type::triangle);
    size_t bands_max = spectral_gen->get_band_index(16000.f);
    Band* bands_data = new Band[bands_max];
    float* bright_data = new float[bands_max];
    float* spread_data = new float[bands_max];
    return new SpectralSignalGenerator(spectral_gen, sampleRate, bands_max, bands_data, bright_data, spread_data);
  }

  static void destroy(SpectralSignalGenerator* synth)
  {
    SpectralGen::destroy(synth->generator_);
    delete[] synth->bands_.data();
    delete[] synth->spec_bright_.data();
    delete[] synth->spec_spread_.data();
    delete synth;
  }

  typename SpectralGen::frequency_band getBand(float freq) const
  {
    const size_t idx = generator_->get_band_index(freq);
    // get from band generator for phase
    return generator_->get_band(idx);
  }

  float getMagnitudeMean()
  {
    float accum = 0;
    for (int i = 1; i < bands_.size(); ++i)
    {
      accum += generator_->get_band(i).magnitude();
    }
    return (accum / bands_.size());
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
    const int midx = generator_->get_band_index(bandFreq);
    const int lidx = midx - spread_bands_max_ * spread_; // freqToIndex(bandFreq - bandFreq * 0.5f*spread);
    const int hidx = midx + spread_bands_max_ * spread_; // freqToIndex(bandFreq + bandFreq * spread);
    addSinusoidWithSpread(midx, amp, lidx, hidx);
  }

  void fill_spectrum()
  {
    constexpr float freq_mult = 1.0f;

    spec_bright_.fill(0);
    spec_spread_.fill(0);

    for (size_t i = 1; i < bands_.size(); ++i)
    {
      processBand(i, bands_.size());
    }

    // spread the raw bright spectrum with a sort of filter than runs forwards and backwards.
    // adapted from ExponentialDecayEnvelope
    float spread_mult = 1.0 + (vessl::math::log(0.00001f) - vessl::math::log(1.0f)) / (spread_bands_max_*spread_ + 12);
    spread_mult *= spread_mult;
    float pi = 0;
    float pj = 0;
    size_t count = spec_bright_.size();
    for (size_t i = 1; i < count; ++i)
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
    
    for (size_t i = 1; i < spec_spread_.size()-1; ++i)
    {
      // grab the magnitude as set by our pluck with spread pass
      const float a = vessl::math::min(spec_spread_[i] * volume_, volume_);
      //const float a = vessl::math::min(bands_[i].amplitude * spectral_magnitude_, spectral_magnitude_);
      
      // copy result into the generator's band magnitudes
      auto& gen_band = generator_->get_band(i);
      gen_band.set_magnitude(a);

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
