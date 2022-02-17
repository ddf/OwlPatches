#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "BlurKernel.h"
#include "basicmaths.h"

enum BlurAxis
{
  AxisX,
  AxisY
};

template<BlurAxis AXIS>
class BlurSignalProcessor : public SignalProcessor 
{
protected:
  CircularFloatTexture texture;
  BlurKernel kernel;

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, BlurKernel kernel)
    : texture(textureData, textureSizeX, textureSizeY)
    , kernel(kernel)
  {
  }

  void setKernel(BlurKernel kernel)
  {
    this->kernel = kernel;
  }

  void setTextureSize(int textureSize)
  {
    if (AXIS == AxisX)
    {
      texture = texture.subtexture(textureSize, 1);
    }
    else
    {
      texture = texture.subtexture(textureSize, textureSize);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float c = kernel.getBlurSize() * 0.5f;
    float v = 0;
    const int samples = kernel.getSize();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      const float coord = c + samp.offset;
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        v += texture.readBilinear(coord, 0) * samp.weight;
      }
      else
      {
        v += texture.readBilinear(0, coord) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

public:
  static BlurSignalProcessor<AXIS>* create(int maxTextureSize, BlurKernel blurKernel)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize], maxTextureSize, 1, blurKernel);
    else
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor<AXIS>* blur)
  {
    delete[] blur->textureA.getData();
    delete blur;
  }
};
