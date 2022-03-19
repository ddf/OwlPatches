#include "SignalProcessor.h"
#include "BlurSignalProcessor.h"
#include "custom_dsp.h"

// performs a 2D Gaussian blur on the input signal
template<typename TextureSizeType = size_t>
class GaussianBlurSignalProcessor : SignalProcessor
{
protected:
  BlurSignalProcessor<AxisX, TextureSizeType>* blurX;
  BlurSignalProcessor<AxisY, TextureSizeType>* blurY;

public:
  GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX, TextureSizeType>* blurX, BlurSignalProcessor<AxisY, TextureSizeType>* blurY)
    : blurX(blurX), blurY(blurY)
  {

  }

  void setTextureSize(TextureSizeType textureSize)
  {
    blurX->setTextureSize(textureSize);
    blurY->setTextureSize(textureSize);
  }

  void setBlur(float size, float standardDeviation, float brightness = 1.0f)
  {
    blurX->kernel.setGauss(size, standardDeviation);
    blurY->kernel.setGauss(size, standardDeviation, brightness);
  }

  float getBlurSize() const
  {
    return blurX->kernel.blurSize;
  }

  BlurKernelSample getKernelSample(int i)
  {
    return blurX->kernel[i];
  }

  void process(FloatArray input, FloatArray output) override
  {
    blurX->process(input, output);
    blurY->process(output, output);
  }

  virtual void process(FloatArray input, FloatArray output, SimpleArray<TextureSizeType> textureSize, BlurKernel kernelStep)
  {
    blurX->process(input, output, textureSize, kernelStep);
    blurY->process(output, output, textureSize, kernelStep);
  }

public:
  static GaussianBlurSignalProcessor<TextureSizeType>* create(size_t maxTextureSize, float maxBlurSize, float standardDeviation, int kernelSize)
  {
    BlurKernel kernelX = BlurKernel::create(kernelSize);
    kernelX.setGauss(0.0f, standardDeviation);
    BlurKernel kernelY = BlurKernel::create(kernelSize);
    kernelY.setGauss(0.0f, standardDeviation);
    return new GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX, TextureSizeType>::create(maxTextureSize, maxBlurSize, kernelX),
                                           BlurSignalProcessor<AxisY, TextureSizeType>::create(maxTextureSize, maxBlurSize, kernelY));
  }

  static void destroy(GaussianBlurSignalProcessor<TextureSizeType>* processor)
  {
    BlurKernel::destroy(processor->blurX->kernel);
    BlurKernel::destroy(processor->blurY->kernel);
    BlurSignalProcessor<AxisX, TextureSizeType>::destroy(processor->blurX);
    BlurSignalProcessor<AxisY, TextureSizeType>::destroy(processor->blurY);
  }
};

template<typename TextureSizeType = size_t>
class GaussianBlurWithFeedback : public GaussianBlurSignalProcessor<TextureSizeType>
{
protected:
  ComplexFloat feedback;
  FloatArray feedX;
  FloatArray feedY;
  BiquadFilter* filterX;
  BiquadFilter* filterY;

public:
  GaussianBlurWithFeedback(BlurSignalProcessor<AxisX, TextureSizeType>* blurX, BlurSignalProcessor<AxisY, TextureSizeType>* blurY, 
                           float* dataX, float* dataY, int dataSize, 
                           BiquadFilter* filterX, BiquadFilter* filterY)
    : GaussianBlurSignalProcessor<TextureSizeType>(blurX, blurY)
    , feedX(dataX, dataSize), feedY(dataY, dataSize), filterX(filterX), filterY(filterY)
  {

  }

  void setFeedback(float mag, float phase)
  {
    feedback.setPolar(mag, phase);
  }

  void process(FloatArray input, FloatArray output) override
  {
    // add x-axis feedback to the input signal, putting the result into x-axis feedback
    applyFeedback(input, feedX, feedback.re, filterX, feedX);
    // process in place so we have the result for the next block
    this->blurX->process(feedX, feedX);
    // add y-axis feedback to result of the x-axis blur, putting the result into y-axis feedback
    applyFeedback(feedX, feedY, feedback.im, filterY, feedY);
    // process in place so we have the result for the next block
    this->blurY->process(feedY, feedY);
    // copy the result to the output
    feedY.copyTo(output);
  }

  void process(FloatArray input, FloatArray output, SimpleArray<TextureSizeType> textureSize, BlurKernel kernelStep) override
  {
    applyFeedback(input, feedX, feedback.re, filterX, feedX);
    this->blurX->process(feedX, feedX, textureSize, kernelStep);
    applyFeedback(feedX, feedY, feedback.im, filterY, feedY);
    this->blurY->process(feedY, feedY, textureSize, kernelStep);
    feedY.copyTo(output);
  }

private:
  void applyFeedback(FloatArray input, FloatArray feed, float amount, BiquadFilter* filter, FloatArray output)
  {
    filter->setHighPass(20.0f + 100.0f * (amount*amount), 1);
    filter->process(feed);
    const int size = input.getSize();
    const float softLimitCoeff = amount * 1.4f;
    for (int i = 0; i < size; ++i)
    {
      float in = input[i];
      output[i] = in + amount * (daisysp::SoftLimit(softLimitCoeff * feed[i] + in) - in);
    }
  }

public:

  static GaussianBlurWithFeedback<TextureSizeType>* create(size_t maxTextureSize, float maxBlurSize, float standardDeviation, int kernelSize, float sampleRate, int blockSize)
  {
    BlurKernel kernelX = BlurKernel::create(kernelSize);
    kernelX.setGauss(0.0f, standardDeviation);
    BlurKernel kernelY = BlurKernel::create(kernelSize);
    kernelY.setGauss(0.0f, standardDeviation);
    BiquadFilter* filterLeft = BiquadFilter::create(sampleRate);
    BiquadFilter* filterRight = BiquadFilter::create(sampleRate);
    return new GaussianBlurWithFeedback(BlurSignalProcessor<AxisX, TextureSizeType>::create(maxTextureSize, maxBlurSize, kernelX),
                                        BlurSignalProcessor<AxisY, TextureSizeType>::create(maxTextureSize, maxBlurSize, kernelY),
                                        new float[blockSize], new float[blockSize], blockSize,
                                        filterLeft, filterRight);
  }

  static void destroy(GaussianBlurWithFeedback<TextureSizeType>* processor)
  {
    BlurKernel::destroy(processor->blurX->kernel);
    BlurKernel::destroy(processor->blurY->kernel);
    BlurSignalProcessor<AxisX, TextureSizeType>::destroy(processor->blurX);
    BlurSignalProcessor<AxisY, TextureSizeType>::destroy(processor->blurY);
    BiquadFilter::destroy(processor->filterX);
    BiquadFilter::destroy(processor->filterY);
    delete[] processor->feedX.getData();
    delete[] processor->feedY.getData();
  }
};
