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
  
  array<BlurKernel> blurKernels;
  GaussProcessor* processorLeft;
  GaussProcessor* processorRight;
  Smoother textureSizeSmoother;
  Smoother blurSizeSmoother;
  Smoother feedbackAmountSmoother;

  GaussSampleFrame feedbackFrame;

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
    , blurKernels(new BlurKernel[KERNEL_COUNT], KERNEL_COUNT)
    , textureSizeSmoother(0.9f, MIN_TEXTURE_SIZE)
    , blurSizeSmoother(0.9f, MIN_BLUR_SIZE)
    , feedbackAmountSmoother(0.9f)
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
    // @todo high pass the feedback signal
    // to use CMSIS filters for this, we have to process feedback in blocks, maybe that's overkill here.
    
    // Note: the way feedback is applied is based on how Clouds does it
    // see: https://github.com/pichenettes/eurorack/tree/master/clouds
    float fdbk = feedbackAmountSmoother.process(vessl::easing::interp<vessl::easing::quad::out>(0.f, 0.99f, *feedback()));
    float slco = fdbk * 1.4f;
    float inLeft = in.left();
    float inRight = in.right();
    float feedLeft = feedbackFrame.left();
    float feedRight = feedbackFrame.right();
    float procLeft = inLeft + fdbk * (vessl::saturation::softlimit(slco*feedLeft + inLeft) - inLeft);
    float procRight = inRight + fdbk * (vessl::saturation::softlimit(slco*feedRight + inRight) - inRight);
    
    float tsz = textureSizeSmoother.process(vessl::easing::lerp<float>(MIN_TEXTURE_SIZE, MAX_TEXTURE_SIZE, *textureSize()));
    processorLeft->setTextureSize(tsz);
    processorRight->setTextureSize(tsz);

    float blurIdx = blurSizeSmoother.process(*blurSize()*(KERNEL_COUNT-1));
    float blurLow;
    float blurFrac = vessl::math::mod(blurIdx, &blurLow);
    float blurHigh = blurLow + 1;
    BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorLeft->getKernel());
    BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorRight->getKernel());

    float scale  = vessl::gain<float>::decibelsToScale(*gain());
    GaussSampleFrame procOut = { processorLeft->process(procLeft), processorRight->process(procRight) };
    feedbackFrame = procOut;
    procOut.scale(scale);
    return procOut;
  }

  using unitProcessor::process;
};