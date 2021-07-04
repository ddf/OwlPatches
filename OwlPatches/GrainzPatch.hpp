#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"

#include "Grain.hpp"

// TODO
// * add a position param on the panel
// * move envelope to a midi parameter
// * add spread midi param for random panning
// * modify speed mapping so 1 is in the middle of the knob
// * add dry/wet midi param (or maybe param E?)

// TODO: want more than 16 grains, but not sure how
static const int MAX_GRAINS = 16;

class GrainzPatch : public Patch
{
  // panel controls
  const PatchParameterId inPosition = PARAMETER_A;
  const PatchParameterId inSize = PARAMETER_B;
  const PatchParameterId inSpeed = PARAMETER_C;
  const PatchParameterId inDensity = PARAMETER_D;
  const PatchButtonId    inFreeze = BUTTON_1;
  const PatchButtonId    inTrigger = BUTTON_2;

  // midi controls
  const PatchParameterId inEnvelope = PARAMETER_AA;

  // outputs
  const PatchButtonId outGrainPlayed = PUSHBUTTON;
  const PatchParameterId outGrainChance = PARAMETER_F;
  const PatchParameterId outGrainEnvelope = PARAMETER_G;

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
  SmoothFloat grainPosition;
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
    registerParameter(outGrainEnvelope, "Envelope>");
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
    grainPosition = getParameterValue(inPosition)*0.25f;
    grainSize = (0.001f + getParameterValue(inSize)*0.124f);
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
      startGrain = grainChance < grainDensity || grainTriggered;
      samplesUntilNextGrain += (grainSpacing * grainSampleLength) / grainSpeed;
      grainTriggered = false;
    }

    float avgEnvelope = 0;
    int activeGrains = 0;
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (startGrain && g->isDone())
      {
        float grainEndPos = (float)bufferLeft->getWriteIndex() / bufferSize;
        g->trigger(grainEndPos - grainPosition, grainSize, grainSpeed, grainEnvelope, 0.5f);
        startGrain = false;
        lastGrain = g;
      }

      if (!g->isDone())
      {
        avgEnvelope += g->envelope();
        ++activeGrains;
      }

      g->generate(audio);
    }
    if (activeGrains > 0)
    {
      avgEnvelope /= activeGrains;
    }

    uint16_t gate = lastGrain != nullptr && !lastGrain->isDone() && lastGrain->progress() < 0.25f;
    setButton(outGrainPlayed, gate);
    setParameterValue(outGrainChance, grainChance);
    setParameterValue(outGrainEnvelope, avgEnvelope);
  }
};
