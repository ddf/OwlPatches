#pragma once 

#include "ExponentialDecayEnvelope.h"
#include "EqualLoudnessCurves.h"
#include "vessicle/SpectralGenerator.h"
#include "vessicle/vessl/vessl.h"

template<vessl::size_t SpectrumSize, bool LinearDecay = true>
class SpectralSynth : public vessl::unit_generator<float>, vessl::plist<4>
{
public:
  using SpectralGen = SpectralGenerator<float, SpectrumSize>;
  using SampleArray = vessl::array<float>;
  using Parameter = vessl::parameter;
  using size_t = vessl::size_t;
  using phase_t = vessl::phase_t;
  
  static constexpr int SpectralBandPartials = 40;
  
  struct Band
  {
    float amplitude;
    float decay;
    float phase; // unused now but might bring back
    int   partials[SpectralBandPartials];
  };
  
  // all data arrays should be at least bands_size long.
  SpectralSynth(SpectralGen* spec_gen, float sample_rate, size_t bands_size,
                Band* bands_data, float* spec_bright_data, float* spec_spread_data)
    : sample_rate_(sample_rate)
    , spread_bands_max_(SpectrumSize/8)
    , generator_(spec_gen)
    , spec_bright_(spec_bright_data, bands_size)
    , spec_spread_(spec_spread_data, bands_size)
    , bands_(bands_data, bands_size)
    , overlap_size_(SpectrumSize/2)
    , overlap_size_half_(overlap_size_/2)
  {
    params_.volume.value = 1.0f;
    params_.decay.value = vessl::duration_t::from_seconds(1.0f, sample_rate);
    set_decay(1.0f);

    for (int i = 1; i < bands_size; ++i)
    {
      const float band_freq = generator_->get_band_frequency(i);
      bands_[i].amplitude = 0;
      // boost low frequencies and attenuate high frequencies with an equal loudness curve.
      // attenuation of high frequencies is to try to prevent distortion that happens when 
      // the spectrum is particularly overloaded in the high end.
      float weight = band_freq < 1000.0f ? clamp(1.0f / elc::b(band_freq), 0.0f, 4.0f) : elc::b(band_freq);

      for (int p = 0; p < SpectralBandPartials; ++p)
      {
        float partial_freq = band_freq*(2 + p);
        // only add partials most people can actually hear
        bands_[i].partials[p] = partial_freq < 16000.0f ? generator_->get_band_index(partial_freq) : SpectrumSize;
      }
    }
    spec_spread_.fill(0);
  }
      
  VESSL_INLINE size_t get_band_index(float frequency)
  {
    return generator_->get_band_index(frequency);
  }
  
  VESSL_INLINE float get_band_frequency(size_t band_index)
  {
    return generator_->get_band_frequency(band_index);
  }
  
  VESSL_INLINE Band& get_band(float freq)
  {
    const size_t idx = generator_->get_band_index(freq);
    return bands_[idx];
  }

  float get_magnitude_mean()
  {
    float accum = 0;
    for (int i = 1; i < bands_.size(); ++i)
    {
      accum += generator_->get_band(i).magnitude();
    }
    return (accum / bands_.size());
  }
  
  void set_spread_bands_max(float num_bands)
  {
    spread_bands_max_ = num_bands;
  }

  VESSL_INLINE Parameter spread() const { return params_.spread("spread", 's'); }
  VESSL_INLINE Parameter decay() const { return params_.decay("decay", 'd'); }\
  VESSL_INLINE Parameter brightness() const { return params_.brightness("brightness", 'b'); }
  VESSL_INLINE Parameter volume() const { return params_.volume("volume", 'v'); }

  void pluck(float freq, float amp)
  {
    const size_t bidx = generator_->get_band_index(freq);
    if (bidx > 1 && bidx < bands_.size())
    {
      bands_[bidx].amplitude = amp;
      bands_[bidx].decay = 1;
    }
  }

  void excite(int bidx, float amp, float phase)
  {
    if (bidx > 1 && bidx < bands_.size())
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
  
  void excite(float freq, float amp)
  {
    const int bidx = generator_->get_band_index(freq);
    excite(bidx, amp, 0);
  }
  
  [[nodiscard]] const parameter_list& parameters() const override { return *this; }
  
  VESSL_INLINE float generate() override
  {
    float decay_param = params_.decay.value.to_seconds(sample_rate_);
    if (vessl::math::abs(decay_seconds_ - decay_param) > 0.001f)
    {
      set_decay(decay_param);
    }
    
    // transfer bands into spread array halfway through the overlap
    // so that we (hopefully) do this work in a different block than synthesis
    if ( generator_->get_read_head(0)+overlap_size_half_ == SpectrumSize 
      || generator_->get_read_head(1)+overlap_size_half_ == SpectrumSize
    )
    {
      fill_spectrum();
    }
    
    return generator_->generate();
  }
  
  VESSL_INLINE void generate(SampleArray output)
  {
    auto w = output.make_writer();
    while (w)
    {
      w << generate();
    }
  }

  static SpectralSynth* create(float sample_rate)
  {
    SpectralGen* spectral_gen = SpectralGen::create(sample_rate, vessl::sample::windows::type::triangle);
    size_t bands_max = spectral_gen->get_band_index(16000.f);
    Band* bands_data = new Band[bands_max];
    float* bright_data = new float[bands_max];
    float* spread_data = new float[bands_max];
    return new SpectralSynth(spectral_gen, sample_rate, bands_max, bands_data, bright_data, spread_data);
  }

  static void destroy(SpectralSynth* synth)
  {
    SpectralGen::destroy(synth->generator_);
    delete[] synth->bands_.data();
    delete[] synth->spec_bright_.data();
    delete[] synth->spec_spread_.data();
    delete synth;
  }

private:
  struct 
  {
    vessl::duration_p decay;
    vessl::analog_p   spread;
    vessl::analog_p   brightness;
    vessl::analog_p   volume;
  } params_;
  
  float sample_rate_;
  float spread_bands_max_;
  
  // cache this so we only recalculate decay_dec_ when necessary.
  float decay_seconds_;
  float decay_dec_;
  
  SpectralGen* generator_;
  SampleArray spec_bright_;
  SampleArray spec_spread_;
  vessl::array<Band> bands_;
  
  size_t overlap_size_;
  size_t overlap_size_half_;
  size_t overlap_size_mask_;
  
  void set_decay(const float in_seconds)
  {
    // having a shorter decay than the overlap size doesn't make sense
    // and we also want to avoid divide-by-zero.
    decay_seconds_ = vessl::math::max(overlap_size_ / sample_rate_, in_seconds);
    if constexpr (LinearDecay)
    {
      // amplitude needs to decrease by 1 / (decaySeconds * sampleRate()) every sample.
      // eg decaySeconds == 1 -> 1 / sampleRate()
      //    decaySeconds == 0.5 -> 1 / (0.5 * sampleRate), which is twice as fast, equivalent to 2 / sampleRate()
      // since we generate a new buffer every overlapSize samples, we multiply that rate by overlapSize, giving:
      decay_dec_ = overlap_size_ / (decay_seconds_ * sample_rate_);
    }
    else // exponential decay
    {
      float block_rate = sample_rate_ / overlap_size_;
      float length_in_blocks = decay_seconds_ * block_rate;
      decay_dec_ = 1.0 + vessl::math::log(0.0001f) / (length_in_blocks + 20);
    }
  }

  VESSL_INLINE void fill_spectrum()
  {
    constexpr float freq_mult = 1.0f;

    spec_bright_.fill(0);
    spec_spread_.fill(0);

    for (size_t i = 1; i < bands_.size(); ++i)
    {
      process_band(i, bands_.size());
    }

    // spread the raw bright spectrum with a sort of filter than runs forwards and backwards.
    // adapted from ExponentialDecayEnvelope
    const float spread = params_.spread.value;
    float spread_mult = 1.0 + (vessl::math::log(0.00001f) - vessl::math::log(1.0f)) / (spread_bands_max_*spread + 12);
    spread_mult *= spread_mult;
    float pi = 0, pj = 0;
    const size_t count = spec_bright_.size();
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
    
    const float volume = vessl::math::constrain(params_.volume.value, 0.f, 1.f);
    for (size_t i = 1; i < spec_spread_.size()-1; ++i)
    {
      // grab the magnitude as set by our pluck with spread pass
      const float a = vessl::math::min(spec_spread_[i] * volume, volume);
      //const float a = vessl::math::min(bands_[i].amplitude * spectral_magnitude_, spectral_magnitude_);
      
      // copy result into the generator's band magnitudes
      auto& gen_band = generator_->get_band(i);
      gen_band.set_magnitude(a);

      // #TODO probably sounds better to do the pitch-shift here?
      // At this point we have gAnaMagn and gAnaFreq from
      // http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/
    }
  }

  VESSL_INLINE void process_band(int idx, int spec_size)
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
      float bright = params_.brightness.value;
      //float a = b.decay*b.amplitude;
      float a = b.amplitude;
      spec_bright_[idx] += a;
      constexpr int iters = SpectralBandPartials;
      for (int i = 0; i < iters && b.partials[i] < spec_size; ++i)
      {
        int p = 2 + i;
        a *= bright;
        int pidx = b.partials[i];
        spec_bright_[pidx] += a / p;
      }
    }
  }

protected:
  vessl::parameter element_at(vessl::size_t index) const override
  {
    switch (index)
    {
      case 0: return decay();
      case 1: return spread();
      case 2: return brightness();
      case 3: return volume();
      default: return Parameter::none();
    }
  }

  // legacy functions from the VST, superceded by fill_spectrum.
  void addSinusoidWithSpread(const int idx, const float amp, const int lidx, const int hidx)
  {
    ExponentialDecayEnvelope falloff_env;
    
    spec_spread_[idx] += amp;

    if (lidx < idx)
    {
      falloff_env.setDecaySamples(idx - lidx + 1);
      falloff_env.setLevel(1);
      falloff_env.generate();
      for (int bidx = idx-1; bidx >= lidx && bidx > 0; --bidx)
      {
        spec_spread_[bidx] += amp * falloff_env.generate();
      }
    }

    if (hidx > idx)
    {
      falloff_env.setDecaySamples(hidx - idx + 1);
      falloff_env.setLevel(1);
      falloff_env.generate();
      for (int bidx = idx + 1; bidx <= hidx && bidx < bands_.getSize(); ++bidx)
      {
        spec_spread_[bidx] += amp * falloff_env.generate();
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
    const int lidx = midx - spread_bands_max_ * params_.spread.value; // freqToIndex(bandFreq - bandFreq * 0.5f*spread);
    const int hidx = midx + spread_bands_max_ * params_.spread.value; // freqToIndex(bandFreq + bandFreq * spread);
    addSinusoidWithSpread(midx, amp, lidx, hidx);
  }
};
