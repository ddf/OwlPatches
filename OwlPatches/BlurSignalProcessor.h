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
    : texture(textureData, textureSizeX, textureSizeY), kernel(kernel)
  {
  }

  void setKernel(BlurKernel kernel)
  {
    this->kernel = kernel;
  }

  void setTextureSize(int texSize)
  {
    if (AXIS == AxisX)
    {
      texture = texture.subtexture(texSize, 1);
    }
    else
    {
      texture = texture.subtexture(texSize, texSize);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float c = kernel.getBlurSize() * 0.5f;
    float v = 0;
    int samples = kernel.getSize();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        //v += texture.readBilinear(c + samp.offset, 0) * samp.weight;
        float x = (c + samp.offset)*texture.getWidth();
        int x1 = int(x);
        int x2 = x1 + 1;
        float xt = x - x1;
        v += Interpolator::linear(texture.read(x1, 0), texture.read(x2, 0), xt);
      }
      else
      {
        //v += texture.readBilinear(0, c + samp.offset) * samp.weight;
        float y = (c + samp.offset)*texture.getHeight();
        int y1 = int(y);
        int y2 = y1 + 1;
        float yt = y - y1;
        v += Interpolator::linear(texture.read(0, y1), texture.read(0, y2), yt);
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
    delete[] blur->texture.getData();
    delete blur;
  }
};
