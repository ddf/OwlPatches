#include "Patch.h"
#include "DcBlockingFilter.h"
#include "CircularBuffer.h"
#include "VoltsPerOctave.h"
#include "BiquadFilter.h"
#include "Grain.hpp"
#include "custom_dsp.h" // for SoftLimit

#define PROFILE

#ifdef PROFILE
#include <string.h>
#endif

using namespace daisysp;

typedef CircularBuffer<Sample> RecordBuffer;

// TODO: want more grains, but not sure how much more optimizing can be done
static const int MAX_GRAINS = 20;
// must be power of two
static const int RECORD_BUFFER_SIZE = 1 << 19; // approx 11 seconds at 48k

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

  RecordBuffer* recordBuffer;

  Grain* grains[MAX_GRAINS];
  int availableGrains[MAX_GRAINS];
  int activeGrains;
  uint16_t  freeze;
  AudioBuffer* grainBuffer;
  float grainRatePhasor;
  bool grainTriggered;
  float grainTriggerDelay;

  // these are expressed as a percentage of the total buffer size
  const float minGrainSize;
  const float maxGrainSize;

  const int playedGateSampleLength;
  int   playedGate;

  AudioBuffer* feedbackBuffer;
  BiquadFilter* feedbackFilterLeft;
  BiquadFilter* feedbackFilterRight;

  SmoothFloat grainOverlap;
  SmoothFloat grainPosition;
  SmoothFloat grainSize;
  SmoothFloat grainSpeed;
  SmoothFloat grainEnvelope;
  SmoothFloat grainSpread;
  SmoothFloat grainVelocity;
  SmoothFloat feedback;
  SmoothFloat dryWet;
  float norms[MAX_GRAINS + 1];

public:
  GrainzPatch()
    : recordBuffer(0), grainBuffer(0)
    , grainRatePhasor(0), grainTriggered(false), activeGrains(0), freeze(OFF)
    , minGrainSize(getSampleRate()*0.008f / RECORD_BUFFER_SIZE) // 8ms
    , maxGrainSize(getSampleRate()*1.0f / RECORD_BUFFER_SIZE) // 1 second
    , playedGateSampleLength(10 * getSampleRate() / 1000), playedGate(0)
    , voct(-0.5f, 4)
  {
    norms[0] = 1;
    for (int i = 1; i < MAX_GRAINS + 1; i++) {
      norms[i] = 1 / sqrtf(i);
    }
    voct.setTune(-4);
    dcFilter = StereoDcBlockingFilter::create(0.995f);
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    recordBuffer = RecordBuffer::create(RECORD_BUFFER_SIZE);
    grainBuffer = AudioBuffer::create(2, getBlockSize());

    for (int i = 0; i < MAX_GRAINS; ++i)
    {
      grains[i] = Grain::create(recordBuffer->getData(), RECORD_BUFFER_SIZE);
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
    BiquadFilter::destroy(feedbackFilterLeft);
    BiquadFilter::destroy(feedbackFilterRight);
    AudioBuffer::destroy(feedbackBuffer);

    RecordBuffer::destroy(recordBuffer);
    AudioBuffer::destroy(grainBuffer);

    for (int i = 0; i < MAX_GRAINS; i+=2)
    {
      Grain::destroy(grains[i]);
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
#ifdef PROFILE
    char debugMsg[64];
    char* debugCpy = debugMsg;
    const float processStart = getElapsedBlockTime();
#endif
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
    grainSize = (minGrainSize + getParameterValue(inSize)*(maxGrainSize - minGrainSize));
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

    if (freeze == OFF)
    {
      // Note: the way feedback is applied is based on how Clouds does it
      float cutoff = (20.0f + 100.0f * feedback * feedback);
      feedbackFilterLeft->setHighPass(cutoff, 1);
      feedbackFilterLeft->process(feedLeft);
      feedbackFilterRight->setHighPass(cutoff, 1);
      feedbackFilterRight->process(feedRight);
      float softLimitCoeff = feedback * 1.4f;
      for (int i = 0; i < size; ++i)
      {
        float left = inOutLeft[i];
        float right = inOutRight[i];
        left += feedback * (SoftLimit(softLimitCoeff * feedLeft[i] + left) - left);
        right += feedback * (SoftLimit(softLimitCoeff * feedRight[i] + right) - right);
        recordBuffer->write(Sample(left*FloatToSample, right*FloatToSample));
      }
    }

    float grainSampleLength = (grainSize*RECORD_BUFFER_SIZE);
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

    int numAvailableGrains = updateAvailableGrains();
    const int readIdx = recordBuffer->getWriteIndex() - size;
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
        int head = readIdx + i;
        float grainEndPos = (float)head / RECORD_BUFFER_SIZE;
        float pan = 0.5f + (randf() - 0.5f)*grainSpread;
        float vel = 1.0f + (randf() * 2 - 1.0f)*grainVelocity;
        g->trigger(grainDelay, grainEndPos - grainPosition, grainSize, grainSpeed, grainEnvelope, pan, vel);
        grainTriggered = false;
        grainTriggerDelay = 0;
        grainRatePhasor = 0;
        playedGate = playedGateSampleLength;
      }
    }

#ifdef PROFILE
    const float genStart = getElapsedBlockTime();
#endif
    float avgProgress = 0;
    float avgEnvelope = 0;
    int prevActiveGrains = activeGrains;
    activeGrains = 0;
    bool clear = true;

    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      auto g = grains[gi];

      if (!g->isDone)
      {
        avgEnvelope += g->envelope();
        avgProgress += g->progress();
        ++activeGrains;
        if (clear) {
          g->generate<true>(grainLeft, grainRight, size);
          clear = false;
        }
        else {
          g->generate<false>(grainLeft, grainRight, size);
        }
      }
    }
    if (clear) {
      grainLeft.clear();
      grainRight.clear();
    }
    float fromGainAdjust = norms[prevActiveGrains];
    float toGainAdjust = norms[activeGrains];
    grainLeft.scale(fromGainAdjust, toGainAdjust);
    grainRight.scale(fromGainAdjust, toGainAdjust);
    grainLeft.copyTo(feedLeft);
    grainRight.copyTo(feedRight);

    if (activeGrains > 0)
    {
      avgEnvelope /= activeGrains;
      avgProgress /= activeGrains;
    }
#ifdef PROFILE
    const float genTime = getElapsedBlockTime() - genStart;
    debugCpy = stpcpy(debugCpy, " gen ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(genTime * 1000), 10));
#endif

    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    for (int i = 0; i < size; ++i)
    {
      inOutLeft[i]  = inOutLeft[i]*dryAmt  + grainLeft[i]*wetAmt;
      inOutRight[i] = inOutRight[i]*dryAmt + grainRight[i]*wetAmt;
    }

    setButton(inFreeze, freeze);
    setButton(outGrainPlayed, playedGate > 0);
    setParameterValue(outGrainPlayback, avgProgress);
    setParameterValue(outGrainEnvelope, avgEnvelope);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - processStart - genTime;
    debugCpy = stpcpy(debugCpy, " proc ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debugMsg);
#endif
  }
private:

  int updateAvailableGrains()
  {
    int count = 0;
    for (int gi = 0; gi < MAX_GRAINS; ++gi)
    {
      if (grains[gi]->isDone)
      {
        availableGrains[count] = gi;
        ++count;
      }
    }
    return count;
  }
};
