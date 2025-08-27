#pragma once

#include "vessl/vessl.h"
#include "GaussianBlurSignalProcessor.h"

using GaussProcessor = GaussianBlurSignalProcessor<float>;
using Smoother = vessl::smoother<float>;
using vessl::parameter;
using GaussSampleFrame = vessl::stereo<float>;

class Gauss : public vessl::unitProcessor<GaussSampleFrame>
{
  init<4> init = {
    "Gauss",
    {
      parameter("Texture Size", parameter::type::analog),
      parameter("Blur Size", parameter::type::analog),
      parameter("Feedback", parameter::type::analog),
      parameter("Gain", parameter::type::analog)
    }
  };

  array<float> textureSizeRamp;
  array<BlurKernel> blurKernels;
  BlurKernel processKernel;
  GaussProcessor* processorLeft;
  GaussProcessor* processorRight;
  Smoother textureSizeSmoother;
  Smoother blurSizeSmoother;

  static constexpr int MIN_TEXTURE_SIZE = 16/4;
  static constexpr int MAX_TEXTURE_SIZE = 256/4;
  static constexpr float MIN_BLUR_SIZE  = 0.0f;
  static constexpr float MAX_BLUR_SIZE  = 0.95f;
  static constexpr int KERNEL_COUNT = 32;
  static constexpr int KERNEL_SIZE = 13;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  static constexpr float STANDARD_DEVIATION = (KERNEL_SIZE - 1) / 4.0f;

public:
  explicit Gauss(float sampleRate, int blockSize) : unitProcessor(init, sampleRate)
    , textureSizeRamp(new float[blockSize], blockSize), blurKernels(new BlurKernel[KERNEL_COUNT], KERNEL_COUNT)
    , processKernel(), textureSizeSmoother(MIN_TEXTURE_SIZE, 0.9f), blurSizeSmoother(MIN_BLUR_SIZE, 0.9f)
  {
    // pre-calculating an array of blur kernels in our blur range
    // and then lerping between them to generate kernels for the processors
    // turns out to be _much_ more performant and means we don't have to downsample
    // and can also have a slightly larger kernel size without pegging the OWL3 CPU.
    float blur = MIN_BLUR_SIZE;
    float blurStep = (MAX_BLUR_SIZE - MIN_BLUR_SIZE) / KERNEL_COUNT;
    for (int i = 0; i < KERNEL_COUNT; i++)
    {
      blurKernels[i] = BlurKernel::create(KERNEL_SIZE);
      blurKernels[i].setGauss(blur, STANDARD_DEVIATION, 1.0f);
      blur += blurStep;
    }
    
    processorLeft = GaussProcessor::create(MAX_TEXTURE_SIZE, MAX_BLUR_SIZE, STANDARD_DEVIATION, KERNEL_SIZE);
    processorLeft->setTextureSize(MIN_TEXTURE_SIZE);
    processorLeft->setBlur(MIN_BLUR_SIZE, STANDARD_DEVIATION, 1.0f);

    processorRight = GaussProcessor::create(MAX_TEXTURE_SIZE, MAX_BLUR_SIZE, STANDARD_DEVIATION, KERNEL_SIZE);
    processorRight->setTextureSize(MIN_TEXTURE_SIZE);
    processorRight->setBlur(MIN_BLUR_SIZE, STANDARD_DEVIATION, 1.0f);
  }

  ~Gauss() override
  {
    delete[] textureSizeRamp.getData();
    for (BlurKernel& kernel : blurKernels)
    {
      BlurKernel::destroy(kernel);
    }
    delete[] blurKernels.getData();
    GaussProcessor::destroy(processorLeft);
    GaussProcessor::destroy(processorRight);
  }

  parameter& textureSize() { return init.params[0]; }
  parameter& blurSize() { return init.params[1]; }
  parameter& feedback() { return init.params[2]; }
  parameter& gain() { return init.params[3]; }
  BlurKernel kernel() const { return processorLeft->getKernel(); }
  
  GaussSampleFrame process(const GaussSampleFrame& in) override
  {
    float tsz = textureSizeSmoother.process(vessl::easing::interp<float>(MIN_TEXTURE_SIZE, MAX_TEXTURE_SIZE, *textureSize()));
    processorLeft->setTextureSize(tsz);
    processorRight->setTextureSize(tsz);

    float blurIdx = blurSizeSmoother.process(*blurSize()*(KERNEL_COUNT-1));
    float blurLow;
    float blurFrac = vessl::math::mod(blurIdx, &blurLow);
    float blurHigh = blurLow + 1;
    BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorLeft->getKernel());
    BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorRight->getKernel());

    float scale  = vessl::gain<float>::decibelsToScale(*gain());
    return { processorLeft->process(in.left())*scale, processorRight->process(in.right())*scale };
  }

  using unitProcessor::process;
};