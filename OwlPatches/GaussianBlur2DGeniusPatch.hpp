#include "MonochromeScreenPatch.h"
#include "BlurPatch.hpp"
#include "Noise.hpp"
#include "Effects/reverbsc.h"

static const BlurPatchParameterIds geniusBlurParams =
{
  .inTextureSize = PARAMETER_A,
  .inBlurSize = PARAMETER_B,
  .inFeedMag = PARAMETER_C,
  .inWetDry = PARAMETER_D,
  .inTextureTilt = PARAMETER_E,
  .inBlurTilt = PARAMETER_F,
  .inFeedTilt = PARAMETER_G,
  .inBlurBrightness = PARAMETER_H,

  .inCompressionThreshold = PARAMETER_AA,
  .inCompressionRatio = PARAMETER_AB,
  .inCompressionAttack = PARAMETER_AC,
  .inCompressionRelease = PARAMETER_AD,
  .inCompressionMakeupGain = PARAMETER_AE,
  .inCompressionBlend = PARAMETER_AF,

  .outLeftFollow = PARAMETER_BA,
  .outRightFollow = PARAMETER_BB
};

typedef BlurPatch<11, 4, 4, MonochromeScreenPatch> GeniusBlurPatchBase;
typedef daisysp::ReverbSc Reverb;

// It turns out that when running without downsampling,
// the volume of the blurred signal gets much quieter at higher blur amounts
// than when running with 4x downsampling.
// So I need to decide if I want the Genius version of the patch
// to sound the same as the Lich version or not.
// One thing is that with no downsampling this runs at ~90% CPU,
// but with 4x downsampling it runs at ~20%.
// This means we could run with downsampling and add a basic reverb,
// which would be a nice addition to this patch.
class GaussianBlur2DGeniusPatch : public GeniusBlurPatchBase
{
  static const PatchParameterId inReverbFeedback = PARAMETER_AG;
  static const PatchParameterId inReverbCutoff = PARAMETER_AH;

  Reverb reverb;
  // at high feedback values this reverb introduces a DC offset that results in distortion
  StereoDcBlockingFilter* reverbFilter;

  AudioBuffer* reverbBuffer;
  SmoothFloat reverbFbdk;
  SmoothFloat reverbCutoff;

  const float reverbFdbkMax = 0.98f;

public:
  GaussianBlur2DGeniusPatch() : BlurPatch(geniusBlurParams) 
  {
    registerParameter(inReverbFeedback, "Verb Amt");
    registerParameter(inReverbCutoff, "Verb Tone");

    setParameterValue(inReverbFeedback, 0);
    setParameterValue(inReverbCutoff, 1);

    reverb.Init(getSampleRate());
    reverb.SetLpFreq(getSampleRate() / 2.0);

    reverbFilter = StereoDcBlockingFilter::create();
    reverbBuffer = AudioBuffer::create(2, getBlockSize());
  }

  ~GaussianBlur2DGeniusPatch()
  {
    StereoDcBlockingFilter::destroy(reverbFilter);
    AudioBuffer::destroy(reverbBuffer);
  }

protected:

  float reverbInputGain()
  {
    const float thresh = 0.9f;
    float fdbk = reverbFbdk.getValue();
    return fdbk < thresh ? 1.0f : 1.0f - ((fdbk - thresh) / (reverbFdbkMax - thresh))*0.6f;
  }

  float reverbPreFeedbackAmount()
  {
    const float thresh = 0.9f;
    float fdbk = reverbFbdk.getValue();
    return fdbk < thresh ? 0.0f : ((fdbk - thresh) / (reverbFdbkMax - thresh))*0.333f;
  }

  void processBlurPreFeedback(AudioBuffer& blurBuffer) override
  {
    GeniusBlurPatchBase::processBlurPreFeedback(blurBuffer);

    // gets real nasty and glitches out when set to max
    reverbFbdk = getParameterValue(inReverbFeedback)*reverbFdbkMax;
    reverbCutoff = Interpolator::linear(100.0f, getSampleRate() / 4.0f, getParameterValue(inReverbCutoff));

    reverb.SetFeedback(reverbFbdk.getValue());
    reverb.SetLpFreq(reverbCutoff.getValue());

    float* blurLeft = blurBuffer.getSamples(0);
    float* blurRight = blurBuffer.getSamples(1);
    float* verbLeft = reverbBuffer->getSamples(0);
    float* verbRight = reverbBuffer->getSamples(1);
    int size = blurBuffer.getSize();
    float inputGain = reverbInputGain();
    float outputGain = 1.0f;// / inputGain;
    float mixAmt = reverbPreFeedbackAmount() * outputGain;

    for (int i = 0; i < size; ++i)
    {
      const float l = blurLeft[i] * inputGain;
      const float r = blurRight[i] * inputGain;
      // this essentially replaces the dry signal, we have to mix it back in
      reverb.Process(l, r, verbLeft + i, verbRight + i);
    }

    reverbFilter->process(*reverbBuffer, *reverbBuffer);

    for(int i = 0; i < size; ++i)
    {
      blurLeft[i]  += verbLeft[i] * mixAmt;
      blurRight[i] += verbRight[i] * mixAmt;
    }
  }

  void processBlurPostFeedback(AudioBuffer& blurBuffer) override
  {
    GeniusBlurPatchBase::processBlurPostFeedback(blurBuffer);

    float* blurLeft = blurBuffer.getSamples(0);
    float* blurRight = blurBuffer.getSamples(1);
    float* verbLeft = reverbBuffer->getSamples(0);
    float* verbRight = reverbBuffer->getSamples(1);
    int size = blurBuffer.getSize();
    float outputGain = 1.0f; // / reverbInputGain();
    float mixAmt = (1.0f - reverbPreFeedbackAmount()) * outputGain;
    for(int i = 0; i < size; ++i)
    {
      blurLeft[i] = daisysp::SoftLimit(blurLeft[i] + verbLeft[i] * mixAmt);
      blurRight[i] = daisysp::SoftLimit(blurRight[i] + verbRight[i] * mixAmt);
    }
  }

public:
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int displayHeight = screen.getHeight() - 18;
    const int cy = displayHeight / 2;
    const int cxL = screen.getWidth() / 4 - 4;
    const int cxR = screen.getWidth() - screen.getWidth() / 4 + 4;
    const int txLeft = roundf(Interpolator::linear(2, displayHeight, (textureSizeLeft - minTextureSize) / (maxTextureSize - minTextureSize)));
    const int txRight = roundf(Interpolator::linear(2, displayHeight, (textureSizeRight - minTextureSize) / (maxTextureSize - minTextureSize)));
    const int blurR = roundf(txRight * blurSizeRight);   
    const int feedWidth = 6;
    const float feedCross = feedbackAngle * feedbackMagnitude;

    drawTexture(screen, cxL, cy, txLeft, blurSizeLeft);
    drawTexture(screen, cxR, cy, txRight, blurSizeRight);
    drawFeedback<true>(screen, screen.getWidth()/2 - feedWidth - 2, displayHeight-1, feedWidth, feedbackMagnitude - feedCross);
    drawCrossFeedback(screen, screen.getWidth()/2 + 2, displayHeight-1, feedWidth, feedCross);
  }

  void drawTexture(MonochromeScreenBuffer& screen, const int cx, const int cy, const int texDim, const float blurSize)
  {
    const int tx = cx - texDim / 2;
    const int ty = cy - texDim / 2;
    screen.drawRectangle(tx, ty, texDim, texDim, WHITE);
    for (int x = 0, y = 0; x < texDim; x+=2, y+=2)
    {
      screen.drawLine(tx, ty + y, tx+x, ty, WHITE);
      screen.drawLine(tx + texDim - 1, ty + texDim - y - 1, tx + texDim - x - 1, ty + texDim - 1, WHITE);
    }
    for (int x = 2; x < texDim - 2; ++x)
    {
      for (int y = 2; y < texDim - 2; ++y)
      {
        if (perlin2d(x, y, texDim / 4, 1) + 0.001f < blurSize*2)
        {
          screen.invertPixel(tx + x - 1, ty + y - 1);
          screen.invertPixel(tx + x - 1, ty + y);
          screen.invertPixel(tx + x - 1, ty + y + 1);

          screen.invertPixel(tx + x, ty + y - 1);
          screen.invertPixel(tx + x, ty + y);
          screen.invertPixel(tx + x, ty + y + 1);

          screen.invertPixel(tx + x + 1, ty + y - 1);
          screen.invertPixel(tx + x + 1, ty + y);
          screen.invertPixel(tx + x + 1, ty + y + 1);
        }
      }
    }
  }

  template<bool pointLeft>
  void drawFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    const int iconY = y;
    screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
    screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
    screen.drawLine(x + iconDim, iconY - iconDim, x + iconDim, iconY, WHITE);
    if (pointLeft)
    {
      screen.drawLine(x + iconDim, iconY, x + 2, iconY, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY - 2, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY + 2, WHITE);
    }
    else
    {
      screen.drawLine(x, iconY, x + iconDim - 2, iconY, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY - 2, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY + 2, WHITE);
    }

    const int barHeight = 38;
    screen.drawRectangle(x, iconY - iconDim - barHeight - 1, iconDim+1, barHeight, WHITE);
    screen.fillRectangle(x, iconY - iconDim - barHeight*amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }

  void drawCrossFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    const int arrowLY = y - iconDim/2 - 1;
    const int arrowRY = y;
    screen.drawLine(x, arrowLY, x+iconDim, arrowLY, WHITE);
    screen.drawLine(x, arrowLY, x + 2, arrowLY - 2, WHITE);
    screen.drawLine(x, arrowLY, x + 2, arrowLY + 2, WHITE);

    screen.drawLine(x, arrowRY, x + iconDim, arrowRY, WHITE);
    screen.drawLine(x + iconDim, arrowRY, x + iconDim - 2, arrowRY - 2, WHITE);
    screen.drawLine(x + iconDim, arrowRY, x + iconDim - 2, arrowRY + 2, WHITE);

    const int barHeight = 38;
    screen.drawRectangle(x, y - iconDim - barHeight - 1, iconDim + 1, barHeight, WHITE);
    screen.fillRectangle(x, y - iconDim - barHeight * amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }
};
