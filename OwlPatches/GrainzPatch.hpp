#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "Grain.hpp"

static const int MAX_GRAINS = 24;

class GrainzPatch : public Patch
{
  const PatchParameterId inDensity = PARAMETER_A;
  const PatchParameterId inSize = PARAMETER_B;
  const PatchParameterId inSpeed = PARAMETER_C;

  StereoDcBlockingFilter* dcFilter;

  CircularFloatBuffer* bufferLeft;
  CircularFloatBuffer* bufferRight;

  Grain* grains[MAX_GRAINS];

public:
  GrainzPatch()
    : bufferLeft(0), bufferRight(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferLeft = CircularFloatBuffer::create(getSampleRate());
    bufferRight = CircularFloatBuffer::create(getSampleRate());
    
    for (int i = 0; i < MAX_GRAINS; i+=2)
    {
      grains[i] = Grain::create(bufferLeft->getData(), bufferLeft->getSize(), getSampleRate());
      grains[i + 1] = Grain::create(bufferRight->getData(), bufferRight->getSize(), getSampleRate());
    }

    registerParameter(inDensity, "Density");
    registerParameter(inSize, "Grain Size");
    registerParameter(inSpeed, "Speed");
  }

  ~GrainzPatch()
  {
    StereoDcBlockingFilter::destroy(dcFilter);

    CircularFloatBuffer::destroy(bufferLeft);
    CircularFloatBuffer::destroy(bufferRight);

    for (int i = 0; i < MAX_GRAINS; i+=2)
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

    float density  = (0.001f + getParameterValue(inDensity)*0.999f);
    float grainLen = (0.001f + getParameterValue(inSize)*0.999f);
    float speed = (0.25f + getParameterValue(inSpeed)*(8.0f - 0.25f));

    for (int i = 0; i < size; ++i)
    {
      bufferLeft->write(left[i]);
      bufferRight->write(right[i]);

      // for now, silence incoming audio
      left[i] = 0;
      right[i] = 0;

      for (int gi = 0; gi < MAX_GRAINS; gi += 2)
      {
        grains[gi]->setDensity(density);
        grains[gi]->setSize(grainLen);
        grains[gi]->setSpeed(speed);
        left[i] += grains[gi]->generate();

        grains[gi + 1]->setDensity(density);
        grains[gi + 1]->setSize(grainLen);
        grains[gi + 1]->setSpeed(speed);
        right[i] += grains[gi + 1]->generate();
      }
    }
  }

};
