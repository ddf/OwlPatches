#include "SignalProcessor.h"
#include "BlurSignalProcessor.h"

// performs a 2D Gaussian blur on the input signal
class GaussianBlurSignalProcessor : SignalProcessor
{
  BlurSignalProcessor<AxisX>* blurX;
  BlurSignalProcessor<AxisY>* blurY;
  BlurKernel kernelX, kernelY;

public:
  GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>* blurX, BlurSignalProcessor<AxisY>* blurY, BlurKernel kernelX, kernelY)
    : blurX(blurX), blurY(blurY), kernelX(kernelX), kernelY(kernelY)
  {

  }

  void setTextureSize(int textureSize)
  {
    blurX->setTextureSize(textureSize);
    blurY->setTextureSize(textureSize);
  }

  void setBlur(float size, float standardDeviation, float scale = 1.0f)
  {
    kernelX.setGauss(size, standardDeviation);
    kernelY.setGauss(size, standardDeviation, scale);
    blurX->setKernel(kernelX);
    blurY->setKernel(kernelY);
  }

  void process(FloatArray input, FloatArray output) override
  {
    blurX->process(input, output);
    blurY->process(output, output);
  }

public:
  static GaussianBlurSignalProcessor* create(int maxTextureSize, float blurSize, float standardDeviation, int kernelSize)
  {
    BlurKernel kernelX = BlurKernel::create(kernelSize);
    kernelX.setGauss(blurSize, standardDeviation);
    BlurKernel kernelY = BlurKernel::create(kernelSize);
    kernelY.setGauss(blurSize, standardDeviation);
    return new GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>::create(maxTextureSize, kernelX)
      , BlurSignalProcessor<AxisY>::create(maxTextureSize, kernelY)
      , kernelX, kernelY);
  }

  static void destroy(GaussianBlurSignalProcessor* processor)
  {
    BlurKernel::destroy(processor->kernelX);
    BlurKernel::destroy(processor->kernelY);
    BlurSignalProcessor<AxisX>::destroy(processor->blurX);
    BlurSignalProcessor<AxisY>::destroy(processor->blurY);
  }
};
