#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "Grain.hpp"

static const int MAX_GRAINS = 24;

class GrainzPatch : public Patch
{
  const PatchParameterId inDensity = PARAMETER_A;
  const PatchParameterId inSize = PARAMETER_B;

  int densityMin, densityMax;
  int sizeMin, sizeMax;

  StereoDcBlockingFilter* dcFilter;

  CircularFloatBuffer* bufferLeft;
  CircularFloatBuffer* bufferRight;

  Grain* grains[MAX_GRAINS];

public:
  GrainzPatch()
    : densityMin(getBlockSize()), densityMax(getSampleRate() / 2)
    , sizeMin(getBlockSize()), sizeMax(sizeMin * 10)
    , bufferLeft(0), bufferRight(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferLeft = CircularFloatBuffer::create(getSampleRate());
    bufferRight = CircularFloatBuffer::create(getSampleRate());
    
    for (int i = 0; i < MAX_GRAINS/2; i+=2)
    {
      grains[i] = Grain::create(bufferLeft->getData(), bufferLeft->getSize(), getSampleRate());
      grains[i + 1] = Grain::create(bufferRight->getData(), bufferRight->getSize(), getSampleRate());
    }

    registerParameter(inDensity, "Density");
    registerParameter(inSize, "Grain Size");
  }

  ~GrainzPatch()
  {
    CircularFloatBuffer::destroy(bufferLeft);
    CircularFloatBuffer::destroy(bufferRight);

    for (int i = 0; i < MAX_GRAINS / 2; i+=2)
    {
      Grain::destroy(grains[i]);
      Grain::destroy(grains[i + 1]);
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    dcFilter->process(audio, audio);

    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);
    const int size = audio.getSize();

    int density  = (int)(densityMin + getParameterValue(inDensity)*densityMax);
    int grainLen = (int)(sizeMin + getParameterValue(inSize)*sizeMax);

    for (int i = 0; i < size; ++i)
    {
      bufferLeft->write(left[i]);
      bufferRight->write(right[i]);

      // for now, silence incoming audio
      //left[i] = 0;
      //right[i] = 0;

      for (int gi = 0; gi < MAX_GRAINS/2; gi+=2)
      {
        left[i] += grains[gi]->generate();
        right[i] += grains[gi+1]->generate();
      }
    }


  }

};
