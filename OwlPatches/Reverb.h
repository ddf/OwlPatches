// Based on the reverb from Clouds https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/fx/reverb.h
// Which is itself based on a paper by Dattoro.

#include "SignalProcessor.h"
#include "SineOscillator.h"
#include "AudioBuffer.h"
#include "AllpassNetwork.h"
#include "CircularBuffer.h"
#include "InterpolatingCircularBuffer.h"

class Reverb : public MultiSignalProcessor
{
  using LFO = SineOscillator;

  AllpassNetwork* diffuser;
  AllpassNetwork* dap1;
  AllpassNetwork* dap2;
  LFO* lfo1;
  LFO* lfo2;
  CircularFloatBuffer* delay1;
  CircularFloatBuffer* delay2;
  float lpDecay1;
  float lpDecay2;

  float reverbTime;
  float lpAmount;
  float wetAmount;

  Reverb(AllpassNetwork* diffuser, AllpassNetwork* dap1, AllpassNetwork* dap2,
         LFO* lfo1, LFO* lfo2, CircularFloatBuffer* delay1, CircularFloatBuffer* delay2)
    : diffuser(diffuser), dap1(dap1), dap2(dap2), lfo1(lfo1), lfo2(lfo2), delay1(delay1), delay2(delay2)
    , lpDecay1(0), lpDecay2(0), reverbTime(0), lpAmount(0.7f), wetAmount(0)
  {
    lfo1->setFrequency(0.5f);
    lfo2->setFrequency(0.3f);
    delay1->setDelay(delay1->getSize() - 1);
    delay2->setDelay(delay2->getSize() - 1);
    setDiffusion(0.625f);
  }

public:

  static Reverb* create(float sr)
  {
    static size_t diffuseTimes[4] { 113, 162, 241, 399 };
    static size_t dap1Times[2]{ 1653, 2038 };
    static size_t dap2Times[2]{ 1913, 1663 };

    return new Reverb(AllpassNetwork::create(diffuseTimes, 4, 0.625f),
      AllpassNetwork::create(dap1Times, 2, 0.625f),
      AllpassNetwork::create(dap2Times, 2, 0.625f),
      LFO::create(sr), LFO::create(sr),
      CircularFloatBuffer::create(3411),
      CircularFloatBuffer::create(4782));
  }

  static void destroy(Reverb* reverb)
  {
    AllpassNetwork::destroy(reverb->diffuser);
    AllpassNetwork::destroy(reverb->dap1);
    AllpassNetwork::destroy(reverb->dap2);
    LFO::destroy(reverb->lfo1);
    LFO::destroy(reverb->lfo2);
    CircularFloatBuffer::destroy(reverb->delay1);
    CircularFloatBuffer::destroy(reverb->delay2);
    delete reverb;
  }

  void setDiffusion(float amt)
  {
    diffuser->setDiffusion(amt);
    dap1->setDiffusion(amt);
    dap2->setDiffusion(amt);
  }

  void setReverbTime(float rvt)
  {
    reverbTime = rvt;
  }

  void setLowPass(float lp)
  {
    lpAmount = lp;
  }

  void setAmount(float amt)
  {
    wetAmount = amt;
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    float* inL = input.getSamples(0);
    float* inR = input.getSamples(1);
    float* outL = output.getSamples(0);
    float* outR = output.getSamples(1);
    int size = input.getSize();
    float lp1 = lpDecay1;
    float lp2 = lpDecay2;
    while (size--)
    {
      float left = *inL++;
      float right = *inR++;
      float m = (left + right) * 0.2f;

      // TODO smearing first allpass filter in the diffuser with lfo1

      float d = diffuser->process(m);

      float accum = d;
      // interpolated read from delay2
      {
        float df = 4680.0f + lfo2->generate()*100.0f;
        int da = (int)df;
        int db = da + 1;
        float t = df - da;
        delay2->setDelay(da);
        float a = delay2->read();
        delay2->setDelay(db);
        float b = delay2->read();
        accum += Interpolator::linear(a, b, t) * reverbTime;
      }
      // low pass filter
      lp1 += lpAmount * (accum - lp1);
      accum = lp1;
      // through two allpass filters
      accum = dap1->process(accum);
      delay1->write(accum);
      accum *= 2;

      *outL++ = left + (accum - left) * wetAmount;

      accum = d;
      accum += delay1->read() * reverbTime;
      lp2 += lpAmount * (accum - lp2);
      accum = lp2;
      accum = dap2->process(accum);
      delay2->write(accum);
      accum *= 2;

      *outR++ = right + (accum - right) * wetAmount;
    }

    lpDecay1 = lp1;
    lpDecay2 = lp2;
  }

};
