#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"
#include "VoltsPerOctave.h"

#include "Grain.hpp"

// TODO
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
  const PatchParameterId inSpread = PARAMETER_AB;
  const PatchParameterId inVelocity = PARAMETER_AC;

  // outputs
  const PatchButtonId outGrainPlayed = PUSHBUTTON;
  const PatchParameterId outGrainChance = PARAMETER_F;
  const PatchParameterId outGrainEnvelope = PARAMETER_G;

  StereoDcBlockingFilter* dcFilter;
  VoltsPerOctave voct;

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
  SmoothFloat grainSpread;
  SmoothFloat grainVelocity;

public:
  GrainzPatch()
    : bufferSize(getSampleRate()*8), bufferLeft(0), bufferRight(0)
    , samplesUntilNextGrain(0), grainChance(0), grainTriggered(false), lastGrain(0)
    , voct(-0.5f, 4)
  {
    voct.setTune(-4);
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    bufferLeft = CircularFloatBuffer::create(bufferSize);
    bufferRight = CircularFloatBuffer::create(bufferSize);
    
    for (int i = 0; i < MAX_GRAINS; ++i)
    {
      grains[i] = Grain::create(bufferLeft->getData(), bufferRight->getData(), bufferSize, getSampleRate());
    }

    registerParameter(inPosition, "Position");
    registerParameter(inSize, "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDensity, "Density");
    registerParameter(inEnvelope, "Envelope");
    registerParameter(inSpread, "Spread");
    registerParameter(inVelocity, "Velocity Variation");
    registerParameter(outGrainChance, "Random>");
    registerParameter(outGrainEnvelope, "Envelope>");

    // default to triangle window
    setParameterValue(inEnvelope, 0.5f);
    setParameterValue(inSpread, 0);
    setParameterValue(inVelocity, 0);
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
    grainSpeed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    grainEnvelope = getParameterValue(inEnvelope);
    grainSpread = getParameterValue(inSpread);
    grainVelocity = getParameterValue(inVelocity);

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
        float pan = 0.5f + (randf() - 0.5f)*grainSpread;
        float vel = 1.0f + (randf() * 2 - 1.0f)*grainVelocity;
        g->trigger(grainEndPos - grainPosition, grainSize, grainSpeed, grainEnvelope, pan, vel);
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
