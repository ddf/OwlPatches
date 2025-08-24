#pragma once

#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "BlurKernel.h"

enum BlurAxis : uint8_t
{
  AxisX,
  AxisY
};

// note: TextureSizeType is used to specialize on float to support textures with non-integral dimensions
// probably there is a less confusing way to do this.
template<BlurAxis AXIS, typename TextureSizeType = std::size_t>
class BlurSignalProcessor : public SignalProcessor
{
  using TextureType = CircularTexture<float>;
protected:
  TextureType texture;
  TextureSizeType texSize;

public:
  BlurKernel kernel;

  BlurSignalProcessor() : texSize(0), kernel() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX*textureSizeY, textureSizeX, textureSizeY)
    , texSize(textureSizeX), kernel(kernel)
  {
  }

  void setTextureSize(TextureSizeType textureSize)
  {
    texSize = textureSize;
  }

  float process(float input) override
  {
    texture.write(input);

    float v = 0;
    const float c = kernel.blurSize * 0.5f;
    const int samples = kernel.getSize();
    if (AXIS == AxisX)
    {
      TextureType tex = texture.subtexture(texSize, 1);
      for (int s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];
        v += tex.readBilinear(c + samp.offset, 0) * samp.weight;
      }
    }
    else
    {
      TextureType tex = texture.subtexture(texSize, texSize);
      for (int s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];
        v += tex.readBilinear(0, c + samp.offset) * samp.weight;
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
  static BlurSignalProcessor* create(std::size_t maxTextureSize, float maxBlurSize, BlurKernel blurKernel)
  {
    // HACK: extra memory to ensure we don't read outside the bounds
    maxTextureSize += 2;
    if (AXIS == AxisX)
    {
      return new BlurSignalProcessor(new float[maxTextureSize], maxTextureSize, 1, maxBlurSize, blurKernel);
    }

    return new BlurSignalProcessor(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, maxBlurSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};

template<BlurAxis AXIS>
class BlurSignalProcessor<AXIS, float> : public SignalProcessor 
{
protected:
  using TextureType = CircularTexture<float>;
  TextureType texture;
  float texSize;
  int   texSizeLow, texSizeHi;
  float texSizeBlend;

public:
  BlurKernel kernel;

  BlurSignalProcessor() : texSize(0), texSizeLow(0), texSizeHi(0), texSizeBlend(0), kernel() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX*textureSizeY, textureSizeX, textureSizeY)
    , texSize(static_cast<float>(textureSizeX)), texSizeLow(textureSizeX)
    , texSizeHi(textureSizeX), texSizeBlend(0), kernel(kernel)
  {
  }

  void setTextureSize(float textureSize)
  {
    texSize = textureSize;
    texSizeLow = static_cast<int>(textureSize);
    texSizeHi = texSizeLow + 1;
    texSizeBlend = textureSize - static_cast<float>(texSizeLow);
  }

  float process(float input) override
  {
    texture.write(input);

    float v = 0;
    const float c = kernel.blurSize * 0.5f;
    std::size_t samples = kernel.getSize();
    if (AXIS == AxisX)
    {
      for (std::size_t s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];

        //v += texture.readBilinear(c + samp.offset, 0) * samp.weight;

        float x = (c + samp.offset) * texSize;
        int x1 = static_cast<int>(x);
        int x2 = x1 + 1;
        float xt = x - static_cast<float>(x1);

        v += vessl::easing::interp(texture.read(x1, 0), texture.read(x2, 0), xt) * samp.weight;
      }
    }
    else
    {
      // read from our two integral texture sizes centered on the same position in the big texture
      // adding texSize to the blur-based offset prevents reading past the write head,
      // which introduces a delay-like echo.
      float readOffset = (texSize) * (texSize) * c + texSize;
      int x1 = static_cast<int>(readOffset);
      int x2 = x1 + 1;
      float xt = readOffset - static_cast<float>(x1);
      TextureType texA = texture.subtexture(texSizeLow, texSizeLow);
      TextureType texB = texture.subtexture(texSizeHi, texSizeHi);
      for (std::size_t s = 0; s < samples; ++s)
      {
        BlurKernelSample samp = kernel[s];

        //v += Interpolator::linear(texA.readBilinear(u1, coord), texB.readBilinear(u2, coord), texSizeBlend) * samp.weight;

        // this is essentially the same as the line commented out above,
        // but instead of generating two different u coordinates that align the reads,
        // we just calculate the x offset, which will be bigger than the texture dimension,
        // so the y coordinate can be +/- around that.
        float ya = samp.offset * static_cast<float>(texSizeLow);
        int ya1 = ya < 0 ? static_cast<int>(ya) - 1 : static_cast<int>(ya);
        int ya2 = ya1 + 1;
        float yat = ya - static_cast<float>(ya1);

        float yb = samp.offset * static_cast<float>(texSizeHi);
        int yb1 = yb < 0 ? static_cast<int>(yb) - 1 : static_cast<int>(yb);
        int yb2 = yb1 + 1;
        float ybt = yb - static_cast<float>(yb1);

        float xa1 = vessl::easing::interp(texA.read(x1, ya1), texA.read(x2, ya1), xt);
        float xa2 = vessl::easing::interp(texA.read(x1, ya2), texA.read(x2, ya2), xt);
        float va  = vessl::easing::interp(xa1, xa2, yat);

        float xb1 = vessl::easing::interp(texB.read(x1, yb1), texB.read(x2, yb1), xt);
        float xb2 = vessl::easing::interp(texB.read(x1, yb2), texB.read(x2, yb2), xt);
        float vb  = vessl::easing::interp(xb1, xb2, ybt);

        v += vessl::easing::interp(va, vb, texSizeBlend) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

  void process(FloatArray input, FloatArray output, SimpleArray<float> textureSize, BlurKernel kernelStep)
  {
    std::size_t size = input.getSize();
    std::size_t samples = kernel.getSize();
    for (std::size_t i = 0; i < size; ++i)
    {
      setTextureSize(textureSize[i]);
      output[i] = process(input[i]);
      for (std::size_t s = 0; s < samples; ++s)
      {
        kernel[s].offset += kernelStep[s].offset;
        kernel[s].weight += kernelStep[s].weight;
      }
      kernel.blurSize += kernelStep.blurSize;
    }
  }

public:
  static BlurSignalProcessor* create(int maxTextureSize, float maxBlurSize, BlurKernel blurKernel)
  {
    // HACK: extra memory to ensure we don't read outside the bounds
    maxTextureSize += 2;
    if (AXIS == AxisX)
    {
      return new BlurSignalProcessor(new float[maxTextureSize], maxTextureSize, 1, maxBlurSize, blurKernel);
    }

    std::size_t sz = static_cast<std::size_t>(maxTextureSize)*maxTextureSize;
    return new BlurSignalProcessor(new float[sz], maxTextureSize, maxTextureSize, maxBlurSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
