#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "Grain.hpp"

static const int MAX_GRAINS = 16;

class GrainzPatch : public Patch
{
  const PatchParameterId inDensity = PARAMETER_A;
  const PatchParameterId inSize = PARAMETER_B;
  const PatchParameterId inSpeed = PARAMETER_C;
  const PatchParameterId inEnvelope = PARAMETER_D;
  const PatchButtonId    inFreeze = BUTTON_1;

  StereoDcBlockingFilter* dcFilter;

  const int bufferSize;
  CircularFloatBuffer* bufferLeft;
  CircularFloatBuffer* bufferRight;

  Grain* grains[MAX_GRAINS];
  float samplesUntilNextGrain;

  SmoothFloat grainSpacing;
  SmoothFloat grainSize;
  SmoothFloat grainSpeed;
  SmoothFloat grainEnvelope;

public:
  GrainzPatch()
    : bufferSize(getSampleRate()), bufferLeft(0), bufferRight(0)
    , samplesUntilNextGrain(0)
  {
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferLeft = CircularFloatBuffer::create(bufferSize);
    bufferRight = CircularFloatBuffer::create(bufferSize);
    
    for (int i = 0; i < MAX_GRAINS; ++i)
    {
      grains[i] = Grain::create(bufferLeft->getData(), bufferRight->getData(), bufferSize, getSampleRate());
    }

    registerParameter(inDensity, "Density");
    registerParameter(inSize, "Grain Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inEnvelope, "Envelope");
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

    grainSpacing = 8.0f + getParameterValue(inDensity)*(0.1f - 8.0f);
    grainSize = (0.01f + getParameterValue(inSize)*0.24f);
    grainSpeed = (0.25f + getParameterValue(inSpeed)*(8.0f - 0.25f));
    grainEnvelope = getParameterValue(inEnvelope);

    bool freeze = isButtonPressed(inFreeze);

    if (!freeze)
    {
      for (int i = 0; i < size; ++i)
      {
        bufferLeft->write(left[i]);
        bufferRight->write(right[i]);
      }
    }

    // for now, silence incoming audio
    audio.clear();

    samplesUntilNextGrain -= getBlockSize() * grainSpeed;

    bool startGrain = samplesUntilNextGrain <= 0;
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (startGrain && g->isDone())
      {
        float grainEndPos = (float)bufferLeft->getWriteIndex() / bufferSize;
        g->startGrain(grainEndPos, grainSize, grainSpeed, grainEnvelope);
        startGrain = false;
        float grainSampleLength = (grainSize*bufferSize);
        samplesUntilNextGrain += grainSpacing * grainSampleLength + grainSampleLength*8.0f*randf();
      }

      g->generate(audio);
    }
  }

};
