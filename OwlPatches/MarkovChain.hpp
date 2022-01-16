#include "SignalGenerator.h"
#include "basicmaths.h"

typedef float Sample;
#define MEMORY_SIZE 350000
#define MEMORY_PER_SAMPLE 4

template<int SIZE>
struct SampleMemory
{
  Sample   samples[SIZE];
  uint8_t  writePosition;

  bool write(Sample sample)
  {
    if (writePosition < SIZE)
    {
      // don't write samples we already know about
      for (int i = 0; i < writePosition; ++i)
      {
        if (samples[i] == sample) return false;
      }
      samples[writePosition++] = sample;
      return true;
    }
    return false;
  }

  Sample generate()
  {
    return writePosition > 0 ? samples[arm_rand32() % writePosition] : 0;
  }
};

class MarkovChain : public SignalGenerator
{
  typedef SampleMemory<MEMORY_PER_SAMPLE> MemType;

  MemType* memory;
  uint32_t totalWrites;
  Sample   lastLearn;
  Sample   lastGenerate;

public:
  MarkovChain()
    : memory(0)
  {
    memory = new MemType[MEMORY_SIZE];
    memset(memory, 0, MEMORY_SIZE * sizeof(MemType));
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
      if (memory[toIndex(lastLearn)].write(sample))
      {
        ++totalWrites;
      }
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

  float getAverageChainLength()
  {
    return (float)totalWrites / MEMORY_SIZE;
  }

private:
  inline Sample toSample(float value) const
  {
    return value;
  }

  inline float toFloat(Sample value) const
  {
    return value;
  }

  inline int toIndex(Sample value) const
  {
    return (int)((value*0.5f + 0.5f)*(MEMORY_SIZE - 1) + 0.5f);
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
