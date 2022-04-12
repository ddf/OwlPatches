// Based on the Diffuser from Clouds: https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/fx/diffuser.h

#include "SignalProcessor.h"
#include "AudioBuffer.h"
#include "Filters/allpass.h"

class Diffuser : public MultiSignalProcessor
{
  using Allpass = daisysp::Allpass;
  static const int kBufferSize = 2048;

  FloatArray buffer;
  float amount;
  Allpass apl1, apl2, apl3, apl4;
  Allpass apr1, apr2, apr3, apr4;

  // helper to initialize each allpass filter with a different section of our buffer.
  float* initAllPass(Allpass& ap, float sr, float* buff, size_t len)
  {
    ap.Init(sr, buff, kBufferSize);
    // this effectively sets the portion of the buffer to use.
    ap.SetFreq((float)len / sr);
    // this ensures the same coefficient for all delay times.
    // calculated so that it winds up around 0.625 at a sample rate of 48000.
    ap.SetRevTime((float)len * 14.7 / sr);
    return buff + len;
  }

  Diffuser(float sampleRate, float* bufferData, size_t bufferSize)
    : buffer(bufferData, bufferSize), amount(0)
  {
    bufferData = initAllPass(apl1, sampleRate, bufferData, 126);
    bufferData = initAllPass(apl2, sampleRate, bufferData, 180);
    bufferData = initAllPass(apl3, sampleRate, bufferData, 269);
    bufferData = initAllPass(apl4, sampleRate, bufferData, 444);
    bufferData = initAllPass(apr1, sampleRate, bufferData, 151);
    bufferData = initAllPass(apr2, sampleRate, bufferData, 205);
    bufferData = initAllPass(apr3, sampleRate, bufferData, 245);
    bufferData = initAllPass(apr4, sampleRate, bufferData, 405);
  }

public:
  void setAmount(float amt)
  {
    amount = amt;
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    int size = input.getSize();
    float* inL = input.getSamples(0);
    float* inR = input.getSamples(1);
    float* outL = output.getSamples(0);
    float* outR = output.getSamples(1);
    while (size--)
    {
      float il = *inL++;
      float dl = apl4.Process(apl3.Process(apl2.Process(apl1.Process(il))));
      *outL++  = il + amount * (dl - il);

      float ir = *inR++;
      float dr = apr4.Process(apr3.Process(apr2.Process(apr1.Process(ir))));
      *outR++  = ir + amount * (dr - ir);
    }
  }

  static Diffuser* create(float sr)
  {
    float* bufferData = new float[kBufferSize];
    memset(bufferData, 0, sizeof(float) * kBufferSize);
    return new Diffuser(sr, bufferData, kBufferSize);
  }

  static void destroy(Diffuser* diffuser)
  {
    delete[] diffuser->buffer.getData();
    delete diffuser;
  }
};
