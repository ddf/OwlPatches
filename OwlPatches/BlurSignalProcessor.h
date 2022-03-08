#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "BlurKernel.h"
#include "basicmaths.h"

enum BlurAxis
{
  AxisX,
  AxisY
};

template<BlurAxis AXIS, typename TextureSizeType = size_t>
class BlurSignalProcessor : public SignalProcessor 
{
protected:
  CircularTexture<float, int> texture;
  float texSize;
  int   texSizeLow, texSizeHi;
  float texSizeBlend;

public:
  BlurKernel kernel;

  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX*textureSizeY, textureSizeX, textureSizeY)
    , kernel(kernel), texSize(textureSizeX)
    , texSizeLow(textureSizeX), texSizeHi(textureSizeX), texSizeBlend(0)
  {
  }

  void setTextureSize(TextureSizeType textureSize)
  {
    texSize = textureSize;
    texSizeLow = (int)textureSize;
    texSizeHi = texSizeLow + 1;
    texSizeBlend = textureSize - texSizeLow;
  }

  float process(float input) override
  {
    texture.write(input);

    float v = 0;
    const float c = kernel.blurSize * 0.5f;
    const int samples = kernel.getSize();
    if (AXIS == AxisX)
    {
      for (int s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];

        //v += texture.readBilinear(c + samp.offset, 0) * samp.weight;

        float x = (c + samp.offset) * texSize;
        int x1 = (int)x;
        int x2 = x1 + 1;
        float xt = x - x1;

        v += Interpolator::linear(texture.read(x1, 0), texture.read(x2, 0), xt) * samp.weight;
      }
    }
    else
    {
      // read from our two integral texture sizes centered on the same position in the big texture
      // adding texSize to the blur-based offset prevents reading past the write head,
      // which introduces a delay-like echo.
      const float readOffset = (texSize) * (texSize) * c + texSize;
      CircularTexture texA = texture.subtexture(texSizeLow, texSizeLow);
      CircularTexture texB = texture.subtexture(texSizeHi, texSizeHi);
      for (int s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];

        //v += Interpolator::linear(texA.readBilinear(u1, coord), texB.readBilinear(u2, coord), texSizeBlend) * samp.weight;

        float ya = samp.offset * texSizeLow;
        int ya1 = ya < 0 ? (int)ya - 1 : (int)ya;
        int ya2 = ya1 + 1;
        float yat = ya - ya1;

        float yb = samp.offset * texSizeHi;
        int yb1 = yb < 0 ? (int)yb - 1 : (int)yb;
        int yb2 = yb1 + 1;
        float ybt = yb - yb1;

        float va = Interpolator::linear(texA.read(0, ya1, readOffset), texA.read(0, ya2, readOffset), yat);
        float vb = Interpolator::linear(texB.read(0, yb1, readOffset), texB.read(0, yb2, readOffset), ybt);

        v += Interpolator::linear(va, vb, texSizeBlend) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

  void process(FloatArray input, FloatArray output, SimpleArray<TextureSizeType> textureSize, BlurKernel kernelStep)
  {
    const int size = input.getSize();
    const int samples = kernel.getSize();
    for (int i = 0; i < size; ++i)
    {
      setTextureSize(textureSize[i]);
      output[i] = process(input[i]);
      for (int s = 0; s < samples; ++s)
      {
        kernel[s].offset += kernelStep[s].offset;
        kernel[s].weight += kernelStep[s].weight;
      }
      kernel.blurSize += kernelStep.blurSize;
    }
  }

public:
  static BlurSignalProcessor<AXIS, TextureSizeType>* create(size_t maxTextureSize, float maxBlurSize, BlurKernel blurKernel)
  {
    // HACK: extra memory to ensure we don't read outside the bounds
    maxTextureSize += 2;
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS, TextureSizeType>(new float[maxTextureSize], maxTextureSize, 1, maxBlurSize, blurKernel);
    else
      return new BlurSignalProcessor<AXIS, TextureSizeType>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, maxBlurSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor<AXIS, TextureSizeType>* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
