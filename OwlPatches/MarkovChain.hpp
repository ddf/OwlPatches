#include "SignalGenerator.h"
#include "basicmaths.h"

typedef int16_t Sample;
#define MEMORY_PER_SAMPLE 16

template<int SIZE>
struct SampleMemory
{
  Sample   samples[SIZE];
  uint8_t  count;

  void write(Sample sample)
  {
    if (count < SIZE)
    {
      samples[count++] = sample;
    }
  }

  Sample generate()
  {
    return count > 0 ? samples[arm_rand32() % count] : 0;
  }
};

class MarkovChain : public SignalGenerator
{
  typedef SampleMemory<MEMORY_PER_SAMPLE> MemType;

  MemType* memory;
  Sample lastLearn;
  Sample lastGenerate;

public:
  MarkovChain()
    : memory(0)
  {
    memory = new MemType[65535];
    memset(memory, 0, 65535 * sizeof(MemType));
    lastLearn = toSample(0);
    lastGenerate = toSample(0);
  }

  ~MarkovChain()
  {
    if (memory)
      delete[] memory;
  }

  void setLastLearn(float value)
  {
    lastLearn = toSample(value);
  }

  void setLastGenerate(float value)
  {
    lastGenerate = toSample(value);
  }

  void learn(FloatArray input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      Sample sample = toSample(input[i]);
      memory[toIndex(lastLearn)].write(sample);
      lastLearn = sample;
    }
  }

  float generate() override
  {
    lastGenerate = memory[toIndex(lastGenerate)].generate();
    return toFloat(lastGenerate);
  }

  void generate(FloatArray output) override
  {
    for (int i = 0, sz = output.getSize(); i < sz; ++i)
    {
      output[i] = generate();
    }
  }

private:
  inline Sample toSample(float value) const
  {
    return (Sample)(value * 32767);
  }

  inline float toFloat(Sample value) const
  {
    return value * 0.0000305185f;
  }

  inline uint16_t toIndex(Sample value) const
  {
    return (uint16_t)(value + 32767);
  }

public:
  static MarkovChain* create()
  {
    return new MarkovChain();
  }

  static void destroy(MarkovChain* markov)
  {
    if (markov)
      delete markov;
  }
};
