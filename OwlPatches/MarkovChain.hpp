#include "SignalGenerator.h"
#include "basicmaths.h"

typedef int16_t Sample;
#define MEMORY_PER_SAMPLE 8 // must be power of 2

template<int SIZE>
struct SampleMemory
{
  Sample   samples[SIZE];
  uint16_t writePosition;

  void write(Sample sample)
  {
    samples[writePosition] = sample;
    writePosition = (writePosition + 1) & (SIZE - 1);
  }

  Sample generate()
  {
    return samples[arm_rand32()&(SIZE - 1)];
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
    : memory(0), lastLearn(0), lastGenerate(0)
  {
    memory = new MemType[65535];
    memset(memory, 0, 65535 * sizeof(MemType));
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
