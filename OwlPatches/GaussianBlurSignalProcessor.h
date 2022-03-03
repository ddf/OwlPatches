#include "SignalProcessor.h"
#include "BlurSignalProcessor.h"

// performs a 2D Gaussian blur on the input signal
template<typename TextureSizeType = size_t>
class GaussianBlurSignalProcessor : SignalProcessor
{
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

  void process(FloatArray input, FloatArray output, SimpleArray<TextureSizeType> textureSize, BlurKernel kernelStep)
  {
    blurX->process(input, output, textureSize, kernelStep);
    //blurY->process(output, output, textureSize, kernelStep);
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
