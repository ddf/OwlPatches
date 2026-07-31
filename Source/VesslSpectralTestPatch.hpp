#pragma once

#include "PatchBase.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/SpectralGenerator.h"

constexpr vessl::size_t SpectrumSize = 2048;
using SpectralGen = SpectralGenerator<float, SpectrumSize>;

class VesslSpectralTestPatch : public PatchBase
{
public:
  VesslSpectralTestPatch() : PatchBase()
  {
    spectral_generator_ = SpectralGen::create(getSampleRate(), vessl::sample::windows::type::triangle);
    //spectral_generator_->get_band(63).magnitude = 8.f;
    //spectral_generator_->get_band(63).phase = 0;
    
    registerParameter(PARAMETER_A, "band");
  }
  
  virtual ~VesslSpectralTestPatch() override
  {
    SpectralGen::destroy(spectral_generator_);
  }
  
  void processAudio(AudioBuffer &audio) override
  {
    PatchBase::processAudio(audio);
    
    const int band_max = spectral_generator_->get_band_index(13000.f);
    const int bidx = vessl::math::lerp(1, band_max, getParameterValue(PARAMETER_A));
    constexpr float mag = static_cast<float>(SpectrumSize)/128.f;
    for (int i = 1; i < band_max; i++)
    {
      auto& band = spectral_generator_->get_band(i);
      if (bidx == i)
      {
        band.set_magnitude(1.f);
      }
      else
      {
        band.scale(0.99f);
      }
    }
    
    AudioBufferWriter<2> writer(audio);
    while (writer)
    {
      vessl::sample::frame<float,2> frame(spectral_generator_->generate());
      writer.write(frame);
    }
    
    setParameterValue(PARAMETER_BA, static_cast<float>(spectral_generator_->get_read_head(0)) / SpectrumSize);
    setParameterValue(PARAMETER_BB, static_cast<float>(spectral_generator_->get_read_head(1)) / SpectrumSize);
  }

private:
  SpectralGen* spectral_generator_;
};