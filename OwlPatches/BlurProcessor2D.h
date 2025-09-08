#pragma once

#include "BlurProcessor1D.h"
#include "vessl/vessl.h"

using vessl::parameter;

// performs a 2D blur on the input signal
template<TextureSizeType TextureSizeType = TextureSizeType::Integral>
class BlurProcessor2D : vessl::unitProcessor<float>
{
  static constexpr parameter::type TEXTURE_SIZE_TYPE = TextureSizeType == TextureSizeType::Integral ? parameter::type::digital : parameter::type::analog;
  
  init<1> init = {
    "Blur Processor 2D",
    {
      parameter("Texture Size", TEXTURE_SIZE_TYPE)
    }
  };

  BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX;
  BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY;
  BlurKernel kernel;

public:
  BlurProcessor2D(float sampleRate, BlurProcessor1D<BlurAxis::X, TextureSizeType>* blurX, BlurProcessor1D<BlurAxis::Y, TextureSizeType>* blurY, BlurKernel blurKernel)
    : unitProcessor(init, sampleRate), blurX(blurX), blurY(blurY), kernel(blurKernel)
  {
    textureSize() << blurX->textureSize();
  }

  parameter& textureSize() { return init.params[0]; }

  void setGauss(float size, float standardDeviation, float brightness = 1.0f)
  {
    kernel.setGauss(size, standardDeviation, brightness);
  }

  BlurKernel getKernel() const { return kernel; }

  float process(const float& in) override
  {
    if (TextureSizeType == TextureSizeType::Integral)
    {
      size_t tsz = textureSize().template read<size_t>();
      blurX->textureSize() << tsz;
      blurY->textureSize() << tsz;
    }
    else
    {
      vessl::analog_t tsz = *textureSize();
      blurX->textureSize() << tsz;
      blurY->textureSize() << tsz;
    }
    return blurY->process(blurX->process(in));
  }

  void process(array<float> input, array<float> output) override
  {
    if (TextureSizeType == TextureSizeType::Integral)
    {
      size_t tsz = textureSize().template read<size_t>();
      blurX->textureSize() << tsz;
      blurY->textureSize() << tsz;
    }
    else
    {
      vessl::analog_t tsz = *textureSize();
      blurX->textureSize() << tsz;
      blurY->textureSize() << tsz;
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