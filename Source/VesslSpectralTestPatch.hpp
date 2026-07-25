#pragma once

#include "vessicle/SpectralGenerator.h"
#include "PatchBase.h"
#include "AudioBufferSourceSink.h"

constexpr vessl::size_t SpectrumSize = 1024;
using SpectralGen = SpectralGenerator<float, SpectrumSize, 2>;

class VesslSpectralTestPatch : public PatchBase
{
  vessl::array<float> window_;
  int windx_ = 0;
  
public:
  VesslSpectralTestPatch() : PatchBase()
  , window_(new float[SpectrumSize], SpectrumSize)
  {
    spectral_generator_ = SpectralGen::create(getSampleRate(), vessl::sample::windows::type::triangle);
    spectral_generator_->get_band(31).magnitude = 2.f;
    
    registerParameter(PARAMETER_A, "band");
    
    vessl::sample::windows::render(vessl::sample::windows::type::triangle, window_);
  }
  
  virtual ~VesslSpectralTestPatch() override
  {
    delete window_.data();
    SpectralGen::destroy(spectral_generator_);
  }
  
  void processAudio(AudioBuffer &audio) override
  {
    PatchBase::processAudio(audio);
    
    // int bidx = vessl::math::lerp(0, 255, getParameterValue(PARAMETER_A));
    // for (int i = 0; i < 256; i++)
    // {
    //   auto&[magnitude, phase] = spectral_generator_->get_band(i);
    //   bidx == i ? magnitude = 2.75 : magnitude *= 0.f;
    // }
    
    AudioBufferWriter<2> writer(audio);
    while (writer)
    {
      vessl::sample::frame<float,2> frame(spectral_generator_->generate());
      writer.write(frame);
    }
    
    if (windx_ < SpectrumSize)
    {
      float wv = window_[windx_++];
      setParameterValue(PARAMETER_F, wv);
    }
  }

private:
  SpectralGen* spectral_generator_;
};