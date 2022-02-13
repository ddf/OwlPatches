#include "SignalProcessor.h"
#include "CircularTexture.h"

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

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY)
    : texture(textureData, textureSizeX, textureSizeY)
  {

  }

  float process(float input) override
  {
    return input;
  }

  using SignalProcessor::process;

public:
  static BlurSignalProcessor<AXIS>* create(int maxTextureSize)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize], maxTextureSize, 1);
    else
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize);
  }

  static void destroy(BlurSignalProcessor<AXIS>* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
