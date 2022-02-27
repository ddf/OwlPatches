#include "SignalProcessor.h"
#include "BlurSignalProcessor.h"

// performs a 2D Gaussian blur on the input signal
class GaussianBlurSignalProcessor : SignalProcessor
{
  BlurSignalProcessor<AxisX>* blurX;
  BlurSignalProcessor<AxisY>* blurY;

public:
  GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>* blurX, BlurSignalProcessor<AxisY>* blurY)
    : blurX(blurX), blurY(blurY)
  {

  }

  void setTextureSize(int textureSize)
  {
    blurX->setTextureSize(textureSize);
    blurY->setTextureSize(textureSize);
  }

  void setBlur(float size, float standardDeviation, float brightness = 1.0f)
  {
    blurX->kernel.setGauss(size, standardDeviation);
    blurY->kernel.setGauss(size, standardDeviation, brightness);
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
    return new GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>::create(maxTextureSize, kernelX),
                                           BlurSignalProcessor<AxisY>::create(maxTextureSize, kernelY));
  }

  static void destroy(GaussianBlurSignalProcessor* processor)
  {
    BlurKernel::destroy(processor->blurX->kernel);
    BlurKernel::destroy(processor->blurY->kernel);
    BlurSignalProcessor<AxisX>::destroy(processor->blurX);
    BlurSignalProcessor<AxisY>::destroy(processor->blurY);
  }
};
