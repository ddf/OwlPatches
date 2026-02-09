#pragma once

#include "vessl/vessl.h"
#include "BlurProcessor2D.h"

using GaussProcessor = BlurProcessor2D<TextureSizeType::Fractional>;
using Smoother = vessl::smoother<float>;
using GaussSampleFrame = vessl::frame::channels<float, 2>;
using HighPass = vessl::filter<float, vessl::filtering::biquad<1>::highPass>;

class Gauss : public vessl::unitProcessor<GaussSampleFrame>
{
  using param = vessl::parameter;
  static constexpr param::desc d_T = { "Tex Size", 'T', vessl::analog_p::type }; // [0,1)
  static constexpr param::desc d_B = { "Blur Size", 'B', vessl::analog_p::type }; // [0, 1)
  static constexpr param::desc d_F = { "Fdbk Amt", 'F', vessl::analog_p::type }; // [0, 1)
  static constexpr param::desc d_g = { "Gain (dB)", 'g', vessl::analog_p::type }; // dB, any value
  static constexpr param::desc d_t = { "Tex Tilt", 't', vessl::analog_p::type }; // (-1, 1)
  static constexpr param::desc d_b = { "Blur Tilt", 'b', vessl::analog_p::type }; // (-1, 1)
  static constexpr param::desc d_f = { "Crossfdbk", 'f', vessl::analog_p::type }; // [0,1]
  using pdl = param::desclist<7>;
  static constexpr pdl p = {{ d_T, d_B, d_F, d_g, d_t, d_b, d_f }};
  
  struct P : vessl::plist<pdl::size>
  {
    vessl::analog_p textureSize;
    vessl::analog_p blurSize;
    vessl::analog_p feedback;
    vessl::analog_p gain;
    vessl::analog_p textureTilt;
    vessl::analog_p blurTilt;
    vessl::analog_p crossFeedback;
    
    param::list<pdl::size> get() const override
    {
      return { textureSize(d_T), blurSize(d_B), feedback(d_F), gain(d_g), textureTilt(d_t), blurTilt(d_b), crossFeedback(d_f) };
    }
  };
  
public:

  description getDescription() const override
  {
    return { "Gauss", p.descs, pdl::size };
  }
  
  const vessl::list<param>& getParameters() const override { return params; }
  
private:
  P params;
  vessl::array<BlurKernel> blurKernels;
  GaussProcessor* processorLeft;
  GaussProcessor* processorRight;
  HighPass feedbackFilterLeft;
  HighPass feedbackFilterRight;
  Smoother textureSizeLeft;
  Smoother textureSizeRight;
  Smoother textureTiltSmoother;
  Smoother blurSizeLeft;
  Smoother blurSizeRight;
  Smoother blurTiltSmoother;
  Smoother feedbackAmount;
  Smoother feedbackAngle;

  GaussSampleFrame feedbackFrame;
  
public:
  static constexpr int MIN_TEXTURE_SIZE = 16/4;
  static constexpr int MAX_TEXTURE_SIZE = 256/4;
  static constexpr float MIN_BLUR_SIZE  = 0.0f;
  static constexpr float MAX_BLUR_SIZE  = 0.95f;
  static constexpr float MIN_TILT = -1.0f;
  static constexpr float MAX_TILT = 1.0f;
  static constexpr int KERNEL_COUNT = 32;
  static constexpr int KERNEL_SIZE = 13;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  static constexpr float STANDARD_DEVIATION = (KERNEL_SIZE - 1) / 4.0f;

  explicit Gauss(float sampleRate, int blockSize) 
    : params(), blurKernels(new BlurKernel[KERNEL_COUNT], KERNEL_COUNT)
    , feedbackFilterLeft(sampleRate, 20.f, 1.0f)
    , feedbackFilterRight(sampleRate, 20.0f, 1.0f)
    , textureSizeLeft(0.9f, MIN_TEXTURE_SIZE), textureSizeRight(0.9f, MIN_TEXTURE_SIZE), textureTiltSmoother(0.9f)
    , blurSizeLeft(0.9f, MIN_BLUR_SIZE), blurSizeRight(0.9f, MIN_BLUR_SIZE), blurTiltSmoother(0.9f)
    , feedbackAmount(0.9f), feedbackAngle(0.9f)
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

    processorLeft = GaussProcessor::create(sampleRate, MAX_TEXTURE_SIZE, STANDARD_DEVIATION, KERNEL_SIZE);
    processorLeft->textureSize() = MIN_TEXTURE_SIZE;

    processorRight = GaussProcessor::create(sampleRate, MAX_TEXTURE_SIZE, STANDARD_DEVIATION, KERNEL_SIZE);
    processorRight->textureSize() = MIN_TEXTURE_SIZE;
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

  param textureSize() { return params.textureSize(d_T); }
  param textureTilt() { return params.textureTilt(d_t); }
  param blurSize() { return params.blurSize(d_B); }
  param blurTilt() { return params.blurTilt(d_b); }
  param feedback() { return params.feedback(d_F); }
  param crossFeedback() { return params.crossFeedback(d_f); }
  param gain() { return params.gain(d_g); }
  
  BlurKernel kernel() const { return processorLeft->getKernel(); }
  float getTextureSizeLeft() const { return static_cast<float>(processorLeft->textureSize()); }
  float getTextureSizeRight() const { return static_cast<float>(processorRight->textureSize()); }
  float getBlurSizeLeft() const { return blurSizeLeft.value; }
  float getBlurSizeRight() const { return blurSizeRight.value; }
  
  GaussSampleFrame process(const GaussSampleFrame& in) override
  {
    // Note: the way feedback is applied is based on how Clouds does it
    // see: https://github.com/pichenettes/eurorack/tree/master/clouds
    float fdbk = feedbackAmount = (vessl::easing::interp<vessl::easing::quad::out, float>(0.f, 0.99f, params.feedback.value));
    float feedbackAmtLeft = fdbk;
    float feedbackAmtRight = fdbk;
    
    float cutoffL = (20.0f + 100.0f * feedbackAmtLeft * feedbackAmtLeft);
    float cutoffR = (20.0f + 100.0f * feedbackAmtRight * feedbackAmtRight);
    float slcoL = feedbackAmtLeft * 1.4f;
    float slcoR = feedbackAmtRight * 1.4f;

    feedbackFilterLeft.fHz() = cutoffL;
    feedbackFilterRight.fHz() = cutoffR;
    float feedLeft = feedbackFilterLeft.process(feedbackFrame.left());
    float feedRight = feedbackFilterRight.process(feedbackFrame.right());

    float inLeft = in.left();
    float inRight = in.right();
    float procLeft = inLeft + feedbackAmtLeft * (vessl::saturation::softlimit(slcoL*feedLeft + inLeft) - inLeft);
    float procRight = inRight + feedbackAmtRight * (vessl::saturation::softlimit(slcoR*feedRight + inRight) - inRight);

    static constexpr float TILT_SCALE = 6.0f;
    
    float tsz = vessl::easing::lerp<float>(MIN_TEXTURE_SIZE, MAX_TEXTURE_SIZE, params.textureSize.value);
    float tlt = textureTiltSmoother = (vessl::math::constrain<float>(params.textureTilt.value*TILT_SCALE, -TILT_SCALE, TILT_SCALE));
    float tszL = textureSizeLeft = (vessl::math::constrain<float>(tsz * vessl::gain::decibelsToScale(-tlt), MIN_TEXTURE_SIZE, MAX_TEXTURE_SIZE));
    float tszR = textureSizeRight = (vessl::math::constrain<float>(tsz * vessl::gain::decibelsToScale(tlt), MIN_TEXTURE_SIZE, MAX_TEXTURE_SIZE));

    processorLeft->textureSize() = tszL;
    processorRight->textureSize() = tszR;

    float bsz = vessl::easing::lerp(MIN_BLUR_SIZE, MAX_BLUR_SIZE, params.blurSize.value);
    float blt = blurTiltSmoother = (vessl::math::constrain(params.blurTilt.value*TILT_SCALE, -TILT_SCALE, TILT_SCALE));
    // set left kernel
    {
      // scale max blur down so we never blur more than a maximum number of samples away
      float bscl = MIN_TEXTURE_SIZE / tszL;
      float bszL = vessl::math::constrain(blurSizeLeft = (bsz * vessl::gain::decibelsToScale(-blt) * bscl), MIN_BLUR_SIZE, MAX_BLUR_SIZE);
      float blurIdx = bszL * (KERNEL_COUNT - 2);
      float blurLow;
      float blurFrac = vessl::math::mod(blurIdx, &blurLow);
      float blurHigh = blurLow + 1;
      BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorLeft->getKernel());
    }
    // set right kernel
    {
      float bscl = MIN_TEXTURE_SIZE / tszR;
      float bszR = vessl::math::constrain(blurSizeRight = (bsz * vessl::gain::decibelsToScale(blt) * bscl), MIN_BLUR_SIZE, MAX_BLUR_SIZE);
      float blurIdx = bszR * (KERNEL_COUNT - 2);
      float blurLow;
      float blurFrac = vessl::math::mod(blurIdx, &blurLow);
      float blurHigh = blurLow + 1;
      BlurKernel::lerp(blurKernels[static_cast<int>(blurLow)], blurKernels[static_cast<int>(blurHigh)], blurFrac, processorRight->getKernel());
    }
    
    GaussSampleFrame procOut = { processorLeft->process(procLeft), processorRight->process(procRight) };
    
    float feedSame = 1.0f - feedbackAngle.value;
    float feedCross(feedbackAngle.value);
    feedbackFrame.left() = procOut.left()*feedSame + procOut.right()*feedCross;
    feedbackFrame.right() = procOut.right()*feedSame + procOut.left()*feedCross;
    
    float scale  = vessl::gain::decibelsToScale(params.gain.value);
    procOut.scale(scale);
    
    return procOut;
  }
  
  using unitProcessor::process;
};