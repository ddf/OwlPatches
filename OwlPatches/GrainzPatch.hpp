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
  const PatchButtonId    inTrigger = BUTTON_2;

  const PatchButtonId outGrainPlayed = PUSHBUTTON;
  const PatchParameterId outGrainChance = PARAMETER_F;

  StereoDcBlockingFilter* dcFilter;

  const int bufferSize;
  CircularFloatBuffer* bufferLeft;
  CircularFloatBuffer* bufferRight;

  Grain* grains[MAX_GRAINS];
  float samplesUntilNextGrain;
  float grainChance;
  bool grainTriggered;
  // last grain that was started, could be null if we skipped it.
  Grain* lastGrain;

  SmoothFloat grainSpacing;
  SmoothFloat grainSize;
  SmoothFloat grainSpeed;
  SmoothFloat grainEnvelope;

public:
  GrainzPatch()
    : bufferSize(getSampleRate()*4), bufferLeft(0), bufferRight(0)
    , samplesUntilNextGrain(0), grainChance(0), grainTriggered(false), lastGrain(0)
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
    registerParameter(outGrainChance, "Random>");
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

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == inTrigger && value == ON)
    {
      samplesUntilNextGrain = samples;
      grainTriggered = true;
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    dcFilter->process(audio, audio);

    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);
    const int size = audio.getSize();

    float grainDensity = getParameterValue(inDensity);
    grainSpacing = 1.0f + grainDensity*(0.1f - 1.0f);
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

    samplesUntilNextGrain -= getBlockSize();

    bool startGrain = false;
    float grainSampleLength = (grainSize*bufferSize);
    if (samplesUntilNextGrain <= 0)
    {
      grainChance = randf();
      startGrain = grainTriggered; // grainChance < grainDensity || grainTriggered;
      samplesUntilNextGrain += (grainSpacing * grainSampleLength) / grainSpeed;
      grainTriggered = false;
      lastGrain = nullptr;
    }

    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (startGrain && g->isDone())
      {
        float grainEndPos = (float)bufferLeft->getWriteIndex() / bufferSize;
        g->trigger(grainEndPos, grainSize, grainSpeed, grainEnvelope, 0.5f);
        startGrain = false;
        lastGrain = g;
      }

      g->generate(audio);
    }

    uint16_t gate = lastGrain != nullptr && lastGrain->progress() < 0.25f;
    setButton(outGrainPlayed, gate);
    setParameterValue(outGrainChance, grainChance);
  }
};
