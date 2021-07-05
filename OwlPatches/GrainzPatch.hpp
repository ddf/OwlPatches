#include "Patch.h"
#include "DcBlockingFilter.h"
#include "FractionalCircularBuffer.h"
#include "VoltsPerOctave.h"

#include "Grain.hpp"

typedef FractionalCircularFloatBuffer RecordBuffer;

// TODO: want more than 16 grains, but not sure how
static const int MAX_GRAINS = 24;

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
  const PatchParameterId inFeedback = PARAMETER_AD;
  const PatchParameterId inDryWet = PARAMETER_AE;

  // outputs
  const PatchButtonId outGrainPlayed = PUSHBUTTON;
  const PatchParameterId outGrainChance = PARAMETER_F;
  const PatchParameterId outGrainEnvelope = PARAMETER_G;

  StereoDcBlockingFilter* dcFilter;
  VoltsPerOctave voct;

  const int recordBufferSize;
  RecordBuffer* recordLeft;
  RecordBuffer* recordRight;

  Grain* grains[MAX_GRAINS];
  AudioBuffer* grainBuffer;
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
  SmoothFloat feedback;
  SmoothFloat dryWet;

public:
  GrainzPatch()
    : recordBufferSize(getSampleRate()*8), recordLeft(0), recordRight(0), grainBuffer(0)
    , samplesUntilNextGrain(0), grainChance(0), grainTriggered(false), lastGrain(0)
    , voct(-0.5f, 4)
  {
    voct.setTune(-4);
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    recordLeft = RecordBuffer::create(recordBufferSize);
    recordRight = RecordBuffer::create(recordBufferSize);
    grainBuffer = AudioBuffer::create(2, getBlockSize());
    grainBuffer->clear();
    
    for (int i = 0; i < MAX_GRAINS; ++i)
    {
      grains[i] = Grain::create(recordLeft->getData(), recordRight->getData(), recordBufferSize, getSampleRate());
    }

    registerParameter(inPosition, "Position");
    registerParameter(inSize, "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDensity, "Density");
    registerParameter(inEnvelope, "Envelope");
    registerParameter(inSpread, "Spread");
    registerParameter(inVelocity, "Velocity Variation");
    registerParameter(inFeedback, "Feedback");
    registerParameter(inDryWet, "Dry/Wet");
    registerParameter(outGrainChance, "Random>");
    registerParameter(outGrainEnvelope, "Envelope>");

    // default to triangle window
    setParameterValue(inEnvelope, 0.5f);
    setParameterValue(inSpread, 0);
    setParameterValue(inVelocity, 0);
    setParameterValue(inFeedback, 0);
    setParameterValue(inDryWet, 1);
    setParameterValue(inFeedback, 0);
  }

  ~GrainzPatch()
  {
    StereoDcBlockingFilter::destroy(dcFilter);

    RecordBuffer::destroy(recordLeft);
    RecordBuffer::destroy(recordRight);
    AudioBuffer::destroy(grainBuffer);

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
    float lastGrainSampleLength = grainSize * recordBufferSize;
    float grainDensity = getParameterValue(inDensity);
    grainSpacing = 1.0f + grainDensity*(0.1f - 1.0f);
    grainPosition = getParameterValue(inPosition)*0.25f;
    grainSize = (0.001f + getParameterValue(inSize)*0.124f);
    grainSpeed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    grainEnvelope = getParameterValue(inEnvelope);
    grainSpread = getParameterValue(inSpread);
    grainVelocity = getParameterValue(inVelocity);
    feedback = getParameterValue(inFeedback);
    dryWet = getParameterValue(inDryWet);

    samplesUntilNextGrain -= getBlockSize();

    bool startGrain = false;
    float grainSampleLength = (grainSize*recordBufferSize);
    if (samplesUntilNextGrain <= 0)
    {
      grainChance = randf();
      startGrain = grainChance < grainDensity || grainTriggered;
      samplesUntilNextGrain += (grainSpacing * grainSampleLength) / grainSpeed;
      grainTriggered = false;
    }

    grainBuffer->clear();
    float avgEnvelope = 0;
    int activeGrains = 0;
    
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (startGrain && g->isDone())
      {
        float grainEndPos = (float)recordLeft->getWriteIndex() / recordBufferSize;
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

      g->generate(*grainBuffer);
    }

    if (activeGrains > 0)
    {
      avgEnvelope /= activeGrains;
    }

    dcFilter->process(audio, audio);

    const int size = audio.getSize();
    FloatArray inOutLeft = audio.getSamples(0);
    FloatArray inOutRight = audio.getSamples(1);
    FloatArray grainLeft = grainBuffer->getSamples(0);
    FloatArray grainRight = grainBuffer->getSamples(1);

    if (!isButtonPressed(inFreeze))
    {
      for (int i = 0; i < size; ++i)
      {
        float x1 = i / (float)size, x0 = 1.0f - x1;
        float delay = lastGrainSampleLength * x0 + grainSampleLength * x1;
        int writeIdx = recordLeft->getWriteIndex() + recordBufferSize;
        float readIdx = writeIdx - delay;
        float dleft = recordLeft->readAt(readIdx);
        float dright = recordRight->readAt(readIdx);
        recordLeft->write(inOutLeft[i] + dleft*feedback);
        recordRight->write(inOutRight[i] + dright*feedback);
      }
    }

    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inOutLeft.multiply(dryAmt);
    inOutRight.multiply(dryAmt);

    grainLeft.multiply(wetAmt);
    grainRight.multiply(wetAmt);

    inOutLeft.add(grainLeft);
    inOutRight.add(grainRight);

    uint16_t gate = lastGrain != nullptr && !lastGrain->isDone() && lastGrain->progress() < 0.25f;
    setButton(outGrainPlayed, gate);
    setParameterValue(outGrainChance, grainChance);
    setParameterValue(outGrainEnvelope, avgEnvelope);
  }
};
