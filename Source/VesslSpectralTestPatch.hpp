#pragma once

#include "vessicle/SpectralGenerator.h"
#include "PatchBase.h"
#include "AudioBufferSourceSink.h"

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
    
    const int bidx = vessl::math::lerp(0, 255, getParameterValue(PARAMETER_A));
    for (int i = 0; i < 256; i++)
    {
      SpectralGen::frequency_band& band = spectral_generator_->get_band(i);
      if (bidx == i)
      {
        band.magnitude = 8.f;
      }
      else
      {
        band.magnitude *= 0.75f;
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