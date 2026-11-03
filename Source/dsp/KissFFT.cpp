#include "KissFFT.h"

#include "KissFFT/kiss_fft.c"

KissFFT::KissFFT() {}

KissFFT::KissFFT(size_t aSize) {
  init(aSize);
}

KissFFT::~KissFFT() {
  free(cfgfft);
  free(cfgifft);
  ComplexFloatArray::destroy(temp);
}

void KissFFT::init(size_t aSize) {
  ASSERT(aSize == 32 || aSize == 64 || aSize == 128 || aSize == 256 || aSize == 512 || aSize == 1024 || aSize == 2048 || aSize == 4096, "Unsupported FFT size");
  cfgfft = kiss_fft_alloc(aSize, 0, 0, 0);
  cfgifft = kiss_fft_alloc(aSize, 1, 0, 0);
  temp = ComplexFloatArray::create(aSize);
}

void KissFFT::fft(FloatArray input, ComplexFloatArray output) {
  ASSERT(input.getSize() >= getSize(), "Input array too small");
  ASSERT(output.getSize() >= getSize(), "Output array too small");
  for (size_t n = 0; n < getSize(); n++) {
    temp[n].re = input[n];
    temp[n].im = 0;
  }
  kiss_fft(cfgfft, (kiss_fft_cpx*)(float*)temp.getData(), (kiss_fft_cpx*)(float*)output.getData());
}

void KissFFT::ifft(ComplexFloatArray input, FloatArray output) {
  ASSERT(input.getSize() >= getSize(), "Input array too small");
  ASSERT(output.getSize() >= getSize(), "Output array too small");
  kiss_fft(cfgifft, (kiss_fft_cpx*)(float*)input.getData(), (kiss_fft_cpx*)(float*)temp.getData());
  float scale = 1.0f / getSize();
  for (size_t n = 0; n < getSize(); n++) {
    output[n] = temp[n].re*scale;
  }
}

size_t KissFFT::getSize() {
  return temp.getSize();
}

KissFFT* KissFFT::create(size_t blocksize) {
  return new KissFFT(blocksize);
}

void KissFFT::destroy(KissFFT* obj) {
  delete obj;
}
