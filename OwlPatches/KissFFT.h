#ifndef __KISSFFT_H__
#define __KISSFFT_H__

#include "FloatArray.h"
#include "ComplexFloatArray.h"

#include "KissFFT/kiss_fft.h"

/**
 * This class performs direct and inverse Fast Fourier Transform.
 */
class KissFFT {
private:
  kiss_fft_cfg cfgfft;
  kiss_fft_cfg cfgifft;
  ComplexFloatArray temp;

public:
  /**
   * Default constructor.
   * Does **not** initialize the instance.
   * @remarks You need to call init(size_t size) before calling any other method
  */
  KissFFT();

  /**
   * Construct and initialize the instance.
   * @param[in] aSize The size of the FFT
   * @remarks Only sizes of 32, 64, 128, 256, 512, 1024, 2048, 4096 are supported, due to limitations of the CMSIS library.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  */
  KissFFT(size_t aSize);

  ~KissFFT();

  /**
   * Initialize the instance.
   * @param aSize The size of the FFT
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  */
  void init(size_t aSize);

  /**
   * Perform the direct FFT.
   * @param[in] input The real-valued input array
   * @param[out] output The complex-valued output array
   * @remarks Calling this method will mess up the content of the **input** array.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
  */
  void fft(FloatArray input, ComplexFloatArray output);

  /**
   * Perform the inverse FFT.
   * The output is rescaled by 1/fftSize.
   * @param[in] input The complex-valued input array
   * @param[out] output The real-valued output array
   * @remarks Calling this method will mess up the content of the **input** array.
   * @note When built for ARM Cortex-M processor series, this method uses the optimized <a href="http://www.keil.com/pack/doc/CMSIS/General/html/index.html">CMSIS library</a>
   *
  */
  void ifft(ComplexFloatArray input, FloatArray output);

  /**
   * Get the size of the FFT
   * @return The size of the FFT
  */
  size_t getSize();

  static KissFFT* create(size_t blocksize);
  static void destroy(KissFFT* obj);
};

#endif // __KISSFFT_H__
