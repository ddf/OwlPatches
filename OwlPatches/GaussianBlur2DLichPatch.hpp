#include "BlurPatch.hpp"

static const BlurPatchParameterIds lichBlurParams =
{
  .inTextureSize = PARAMETER_A,
  .inBlurSize = PARAMETER_B,
  .inFeedMag = PARAMETER_C,
  .inWetDry = PARAMETER_D,

  .inTextureTilt = PARAMETER_A,
  .inBlurTilt = PARAMETER_B,
  .inFeedTilt = PARAMETER_E,

  .inBlurBrightness = PARAMETER_H,

  .inCompressionThreshold = PARAMETER_AA,
  .inCompressionRatio = PARAMETER_AB,
  .inCompressionAttack = PARAMETER_AC,
  .inCompressionRelease = PARAMETER_AD,
  .inCompressionMakeupGain = PARAMETER_AE,
  .inCompressionBlend = PARAMETER_AF,

  .outLeftFollow = PARAMETER_F,
  .outRightFollow = PARAMETER_G
};

// Lich isn't fast enough to do processing at 2x downsampled,
// we must use a downsample factor of 4, which requires 4 stages to prevent too much aliasing.
// At that rate we can get away with a kernel size of 11 without maxing out the device.
class GaussianBlur2DLichPatch : public BlurPatch<11, 4, 4>
{
public:
  GaussianBlur2DLichPatch() : BlurPatch(lichBlurParams) {}
};
