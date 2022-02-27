#include "SignalProcessor.h"
#include "BlurSignalProcessor.h"

// performs a 2D Gaussian blur on the input signal
class GaussianBlurSignalProcessor : SignalProcessor
{
  BlurSignalProcessor<AxisX>* blurX;
  BlurSignalProcessor<AxisY>* blurY;
  BlurKernel kernel;

public:
  GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>* blurX, BlurSignalProcessor<AxisY>* blurY, BlurKernel kernel)
    : blurX(blurX), blurY(blurY), kernel(kernel)
  {

  }

  void setTextureSize(int textureSize)
  {
    blurX->setTextureSize(textureSize);
    blurY->setTextureSize(textureSize);
  }

  void setBlur(float size, float standardDeviation, float scale = 1.0f)
  {
    kernel.setGauss(size, standardDeviation, scale);
    blurX->setKernel(kernel);
    blurY->setKernel(kernel);
  }

  void process(FloatArray input, FloatArray output) override
  {
    blurX->process(input, output);
    blurY->process(output, output);
  }

public:
  static GaussianBlurSignalProcessor* create(int maxTextureSize, float blurSize, float standardDeviation, int kernelSize)
  {
    BlurKernel kernel = BlurKernel::create(kernelSize);
    kernel.setGauss(blurSize, standardDeviation);
    return new GaussianBlurSignalProcessor(BlurSignalProcessor<AxisX>::create(maxTextureSize, kernel)
      , BlurSignalProcessor<AxisY>::create(maxTextureSize, kernel)
      , kernel);
  }

  static void destroy(GaussianBlurSignalProcessor* processor)
  {
    BlurKernel::destroy(processor->kernel);
    BlurSignalProcessor<AxisX>::destroy(processor->blurX);
    BlurSignalProcessor<AxisY>::destroy(processor->blurY);
  }
};
