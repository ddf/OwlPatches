#pragma once

#include "BlurProcessor1D.h"
#include "vessl/vessl.h"

using vessl::parameter;

// performs a 2D blur on the input signal
template<TextureSizeType TextureSizeType = TextureSizeType::Integral>
class BlurProcessor2D : vessl::unitProcessor<float>
{
  using size_t = vessl::size_t;
  using pdl = parameter::desclist<1>;
  
  struct P : vessl::parameterList<pdl::size>
  {
    vessl::analog_p textureSize;
    
    parameter::reflist<1> operator*() const override { return { textureSize }; }
  };
  
  P params;
  BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX;
  BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY;
  BlurKernel kernel;

public:
  BlurProcessor2D(float sampleRate, BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX, BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY, BlurKernel blurKernel)
    : unitProcessor(sampleRate), blurX(blurX), blurY(blurY), kernel(std::move(blurKernel))
  {
    params.textureSize.value = blurX->textureSize().readAnalog();
  }

  unit::description getDescription() const override
  {
    static constexpr pdl p = {
      {
        { "Texture Size", 't', parameter::valuetype::analog }
      }
    };
    return { "blur processor 2d", p.descs, pdl::size };
  }
  
  const vessl::list<parameter>& getParameters() const override { return params; }

  parameter& textureSize() { return params.textureSize; }

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