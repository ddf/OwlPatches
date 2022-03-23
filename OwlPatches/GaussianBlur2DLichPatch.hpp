#include "BlurPatch.hpp"

// Lich isn't fast enough to do processing at 2x downsampled,
// we must use a downsample factor of 4, which requires 4 stages to prevent too much aliasing.
// At that rate we can get away with a kernel size of 11 without maxing out the device.
class GaussianBlur2DLichPatch : public BlurPatch<11, 4, 4>
{

};
