#pragma once

#include "BlurProcessor1D.h"
#include "vessl/vessl.h"

// performs a 2D blur on the input signal
template<TextureSizeType TextureSizeType = TextureSizeType::Integral>
class BlurProcessor2D : vessl::unitProcessor<float>
{
  using param = vessl::parameter;
  static constexpr param::desc d_t = { "Texture Size", 't', vessl::analog_p::type };
  using pdl = param::desclist<1>;
  static constexpr pdl p = {{ d_t }};
    
  struct P : vessl::plist<pdl::size>
  {
    vessl::analog_p textureSize;
    
    param::list<pdl::size> get() const override { return { textureSize(d_t) }; }
  };
  
  using size_t = vessl::size_t;
  
  P params;
  BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX;
  BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY;
  BlurKernel kernel;

public:
  BlurProcessor2D(float sampleRate, BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX, BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY, const BlurKernel& blurKernel)
    : blurX(blurX), blurY(blurY), kernel(blurKernel)
  {
    params.textureSize.value = blurX->textureSize().readAnalog();
  }

  unit::description getDescription() const override
  {

    return { "blur processor 2d", p.descs, pdl::size };
  }
  
  const vessl::list<param>& getParameters() const override { return params; }

  param textureSize() { return params.textureSize(d_t); }

  void setGauss(float size, float standardDeviation, float brightness = 1.0f)
  {
    kernel.setGauss(size, standardDeviation, brightness);
  }

  BlurKernel getKernel() const { return kernel; }

  float process(const float& in) override
  {
    if (TextureSizeType == TextureSizeType::Integral)
    {
      size_t tsz = static_cast<size_t>(params.textureSize.value);
      blurX->textureSize() = tsz;
      blurY->textureSize() = tsz;
    }
    else
    {
      vessl::analog_t tsz = params.textureSize.value;
      blurX->textureSize() = tsz;
      blurY->textureSize() = tsz;
    }
    return blurY->process(blurX->process(in));
  }

  void process(vessl::array<float> input, vessl::array<float> output) override
  {
    if (TextureSizeType == TextureSizeType::Integral)
    {
      size_t tsz = static_cast<size_t>(params.textureSize.value);
      blurX->textureSize() = tsz;
      blurY->textureSize() = tsz;
    }
    else
    {
      vessl::analog_t tsz = params.textureSize.value;
      blurX->textureSize() = tsz;
      blurY->textureSize() = tsz;
    }
    blurX->process(input, output);
    blurY->process(output, output);
  }

  static BlurProcessor2D* create(float sampleRate, size_t maxTextureSize, float standardDeviation, int kernelSize)
  {
    BlurKernel kernel = BlurKernel::create(kernelSize);
    kernel.setGauss(0.0f, standardDeviation);
    return new BlurProcessor2D(sampleRate, BlurProcessor1D<BlurAxis::X, TextureSizeType>::create(sampleRate, maxTextureSize, kernel),
                                           BlurProcessor1D<BlurAxis::Y, TextureSizeType>::create(sampleRate, maxTextureSize, kernel),
                                           kernel);
  }

  static void destroy(BlurProcessor2D* processor)
  {
    BlurKernel::destroy(processor->kernel);
    BlurProcessor1D<BlurAxis::X, TextureSizeType>::destroy(processor->blurX);
    BlurProcessor1D<BlurAxis::Y, TextureSizeType>::destroy(processor->blurY);
    delete processor;
  }
};