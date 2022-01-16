#include "SignalGenerator.h"
#include "basicmaths.h"

typedef int16_t Sample;
#define MEMORY_PER_SAMPLE 8

template<int SIZE>
struct SampleMemory
{
  Sample   samples[SIZE];
  uint16_t counts[SIZE];
  uint32_t totalCount;

  void write(Sample sample)
  {
    for (int i = 0; i < SIZE; ++i)
    {
      const Sample s = samples[i];
      const uint16_t c = counts[i];
      if (s == sample)
      {
        if (c < 65535)
        {
          counts[i] = c + 1;
          ++totalCount;
        }
      }
      else if (c == 0)
      {
        samples[i] = sample;
        counts[i] = 1;
        ++totalCount;
      }
    }
  }

  Sample generate()
  {
    if (totalCount == 0) return 0;

    uint32_t threshold = arm_rand32() % totalCount;
    uint32_t accum = 0;
    for (int i = 0; i < SIZE; ++i)
    {
      if (counts[i] > 0)
      {
        accum += counts[i];
        if (accum >= threshold)
        {
          return samples[i];
        }
      }
    }
    return 0;
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
