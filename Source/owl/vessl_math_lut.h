#pragma once

#include "../vessicle/vessl/vessl.h"

// declaration for lut math function from basicmaths.h
#ifdef __cplusplus
 extern "C" {
#endif
  // we've got these covered in vessl already
  // float arm_sqrtf(float in);
  // void arm_srand32(uint32_t s);
  // uint32_t arm_rand32();

  // fast lookup-based exponentials
  float fast_powf(float x, float y);
  float fast_expf(float x);
  float fast_exp2f(float x);
  float fast_exp10f(float x);

  // fast lookup-based logarithmics
  float fast_logf(float x);
  float fast_log2f(float x);
  float fast_log10f(float x);
  uint32_t fast_log2i(uint32_t x);

  // fast approximations
  float fast_atan2f(float a, float b);

  /** generate a random number between 0 and 1 */
  float randf();

  float fast_fmodf(float x, float y);
#ifdef __cplusplus
}
#endif

// vessl::math specializations 
namespace vessl
{
namespace math
{
  template<>
  VESSL_INLINE float pow<float>(float x, float y) { return fast_powf(x, y); }

  template<>
  VESSL_INLINE float exp<float>(float x) { return fast_expf(x); }

  template<>
  VESSL_INLINE float exp2<float>(float x) { return fast_exp2f(x); }

  template<>
  VESSL_INLINE float exp10<float>(float x) { return fast_exp10f(x); }

  template<>
  VESSL_INLINE float log<float>(float x) { return fast_logf(x); }

  // template<>
  // VESSL_INLINE float log2<float>(float x) { return fast_log2f(x); }

  template<>
  VESSL_INLINE float log10<float>(float x) { return fast_log10f(x); }

  template<>
  VESSL_INLINE float atan2<float>(float a, float b) { return fast_atan2f(a, b); }
}
} // namespace vessl