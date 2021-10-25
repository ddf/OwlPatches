#include "Patch.h"
#include "DcBlockingFilter.h"
#include "FractionalCircularBuffer.h"
#include "VoltsPerOctave.h"
#include "BiquadFilter.h"
#include "custom_dsp.h"

#include "Grain.hpp"

#define PROFILE

#ifdef PROFILE
#include <string.h>
#endif

using namespace daisysp;

typedef FractionalCircularFloatBuffer RecordBuffer;

// TODO: want more than 16 grains, but not sure how
#ifdef PROFILE
static const int MAX_GRAINS = 8;
#else
static const int MAX_GRAINS = 16;
#endif

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
  const PatchParameterId outGrainPlayback = PARAMETER_F;
  const PatchParameterId outGrainEnvelope = PARAMETER_G;

  StereoDcBlockingFilter* dcFilter;
  VoltsPerOctave voct;

  const int recordBufferSize;
  RecordBuffer* recordLeft;
  RecordBuffer* recordRight;

  Grain* grains[MAX_GRAINS];
  int availableGrains[MAX_GRAINS];
  int activeGrains;
  uint16_t  freeze;
  AudioBuffer* grainBuffer;
  float grainRatePhasor;
  bool grainTriggered;
  float grainTriggerDelay;

  const int playedGateSampleLength;
  int   playedGate;
  // last grain that was started, could be null if we skipped it.
  Grain* lastGrain;

  AudioBuffer* feedbackBuffer;
  StereoBiquadFilter* feedbackFilter;

  SmoothFloat grainOverlap;
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
    , grainRatePhasor(0), grainTriggered(false), lastGrain(0), activeGrains(0), freeze(OFF)
    , playedGateSampleLength(10 * getSampleRate() / 1000), playedGate(0)
    , voct(-0.5f, 4)
  {
    voct.setTune(-4);
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    feedbackFilter = StereoBiquadFilter::create(getSampleRate());

    recordLeft = RecordBuffer::create(recordBufferSize);
    recordRight = RecordBuffer::create(recordBufferSize);
    grainBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());
    
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
    registerParameter(outGrainPlayback, "Playback>");
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
    StereoBiquadFilter::destroy(feedbackFilter);

    RecordBuffer::destroy(recordLeft);
    RecordBuffer::destroy(recordRight);
    AudioBuffer::destroy(grainBuffer);
    AudioBuffer::destroy(feedbackBuffer);

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
      grainTriggerDelay = samples;
      grainTriggered = true;
    }
    else if (bid == inFreeze && value == ON)
    {
      freeze = freeze == ON ? OFF : ON;
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int size = audio.getSize();
    FloatArray inOutLeft = audio.getSamples(0);
    FloatArray inOutRight = audio.getSamples(1);
    FloatArray grainLeft = grainBuffer->getSamples(0);
    FloatArray grainRight = grainBuffer->getSamples(1);
    FloatArray feedLeft = feedbackBuffer->getSamples(0);
    FloatArray feedRight = feedbackBuffer->getSamples(1);

    // like Clouds, Density describes how many grains we want playing simultaneously at any given time
    float grainDensity = getParameterValue(inDensity);
    float overlap = 0;
    if (grainDensity >= 0.53f)
    {
      overlap = (grainDensity - 0.53f) * 2.12f;
    }
    else if (grainDensity <= 0.47f)
    {
      overlap = (0.47f - grainDensity) * 2.12f;
    }
    grainOverlap = overlap * overlap * overlap;
    grainPosition = getParameterValue(inPosition)*0.25f;
    grainSize = (0.001f + getParameterValue(inSize)*0.124f);
    grainSpeed = voct.getFrequency(getParameterValue(inSpeed)) / 440.0f;
    grainEnvelope = getParameterValue(inEnvelope);
    grainSpread = getParameterValue(inSpread);
    grainVelocity = getParameterValue(inVelocity);
    feedback = getParameterValue(inFeedback);
    dryWet = getParameterValue(inDryWet);

    dcFilter->process(audio, audio);

    if (playedGate > 0)
    {
      playedGate -= getBlockSize();
    }

#ifdef PROFILE
    char debugMsg[64];
    char* debugCpy = debugMsg;
    float t1 = getElapsedBlockTime();
#endif
    if (freeze == OFF)
    {
      // Note: the way feedback is applied is based on how Clouds does it
      float cutoff = (20.0f + 100.0f * feedback * feedback);
      feedbackFilter->setHighPass(cutoff, 1);
      feedbackFilter->process(*feedbackBuffer, *feedbackBuffer);
      for (int i = 0; i < size; ++i)
      {
        recordLeft->write(inOutLeft[i] + feedback * (SoftLimit(feedback * 1.4f * feedLeft[i] + inOutLeft[i]) - inOutLeft[i]));
        recordRight->write(inOutRight[i] + feedback * (SoftLimit(feedback * 1.4f * feedRight[i] + inOutRight[i]) - inOutRight[i]));
      }
    }
#ifdef PROFILE
    float t2 = getElapsedBlockTime();
    debugCpy = stpcpy(debugCpy, "fb ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)((t2 - t1) * 1000), 10));
#endif

    float grainSampleLength = (grainSize*recordBufferSize);
    float targetGrains = MAX_GRAINS * grainOverlap;
    float grainProb = targetGrains / grainSampleLength;
    float grainSpacing = grainSampleLength / targetGrains;

    if (grainDensity < 0.5f)
    {
      grainProb = -1.0f;
    }
    else
    {
      grainRatePhasor = -getBlockSize();
    }

#ifdef PROFILE
    t1 = getElapsedBlockTime();
#endif
    int numAvailableGrains = updateAvailableGrains();
    for (int i = 0; i < size; ++i)
    {
      grainRatePhasor += 1.0f;
      bool startProb = randf() < grainProb && targetGrains > activeGrains;
      bool startSteady = grainRatePhasor >= grainSpacing;
      bool startGrain = startProb || startSteady || grainTriggered;
      if (startGrain && numAvailableGrains)
      {
        --numAvailableGrains;
        int gidx = availableGrains[numAvailableGrains];
        Grain* g = grains[gidx];
        float grainDelay = i > grainTriggerDelay ? i : grainTriggerDelay;
        int head = recordLeft->getWriteIndex() - size + i;
        float grainEndPos = (float)head / recordBufferSize;
        float pan = 0.5f + (randf() - 0.5f)*grainSpread;
        float vel = 1.0f + (randf() * 2 - 1.0f)*grainVelocity;
        g->trigger(grainDelay, grainEndPos - grainPosition, grainSize, grainSpeed, grainEnvelope, pan, vel);
        grainTriggered = false;
        grainTriggerDelay = 0;
        grainRatePhasor = 0;
        playedGate = playedGateSampleLength;
        lastGrain = g;
      }
    }
#ifdef PROFILE
    t2 = getElapsedBlockTime();
    debugCpy = stpcpy(debugCpy, " trig ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)((t2 - t1) * 1000), 10));
#endif

#ifdef PROFILE
    t1 = getElapsedBlockTime();
#endif
    //grainBuffer->clear();
    float avgProgress = 0;
    float avgEnvelope = 0;
    activeGrains = 0;
    
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (!g->isDone())
      {
        avgEnvelope += g->envelope();
        avgProgress += g->progress();
        ++activeGrains;
      }

      g->generate(grainLeft, grainRight, size);
    }

    if (activeGrains > 0)
    {
      avgEnvelope /= activeGrains;
      avgProgress /= activeGrains;
    }
#ifdef PROFILE
    t2 = getElapsedBlockTime();
    debugCpy = stpcpy(debugCpy, " gen ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)((t2 - t1) * 1000), 10));
#endif

#ifdef PROFILE
    t1 = getElapsedBlockTime();
#endif
    // feedback wet signal
    grainLeft.copyTo(feedLeft);
    grainRight.copyTo(feedRight);

    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    //inOutLeft.multiply(dryAmt);
    //inOutRight.multiply(dryAmt);

    //grainLeft.multiply(wetAmt);
    //grainRight.multiply(wetAmt);

    //inOutLeft.add(grainLeft);
    //inOutRight.add(grainRight);
    for (int i = 0; i < size; ++i)
    {
      inOutLeft[i]  = inOutLeft[i]*dryAmt  + grainLeft[i]*wetAmt;
      inOutRight[i] = inOutRight[i]*dryAmt + grainRight[i]*wetAmt;
      grainLeft[i]  = grainRight[i] = 0;
    }
#ifdef PROFILE
    t2 = getElapsedBlockTime();
    debugCpy = stpcpy(debugCpy, " mix ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)((t2 - t1)*1000), 10));
    debugMessage(debugMsg);
#endif

    setButton(inFreeze, freeze);
    setButton(outGrainPlayed, playedGate > 0);
    setParameterValue(outGrainPlayback, avgProgress);
    setParameterValue(outGrainEnvelope, avgEnvelope);
  }
private:

  int updateAvailableGrains()
  {
    int count = 0;
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      if (grains[gi]->isDone())
      {
        availableGrains[count] = gi;
        ++count;
      }
    }
    return count;
  }
};
