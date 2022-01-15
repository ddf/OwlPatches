#include "SignalGenerator.h"
#include "basicmaths.h"

typedef int16_t Sample;
#define SampleToFloat 0.0000305185f // 1 / 32767
#define FloatToSample 32767
#define SampleToIndex(s) (s+FloatToSample)

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
    lastLearn = value * FloatToSample;
  }

  void setLastGenerate(float value)
  {
    lastGenerate = value * FloatToSample;
  }

  void learn(FloatArray input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      Sample sample = input[i] * FloatToSample;
      memory[SampleToIndex(lastLearn)].write(sample);
      lastLearn = sample;
    }
  }

  float generate() override
  {
    lastGenerate = memory[SampleToIndex(lastGenerate)].samples[rand()&(MEMORY_PER_SAMPLE - 1)];
    return lastGenerate*SampleToFloat;
  }

  void generate(FloatArray output) override
  {
    for (int i = 0, sz = output.getSize(); i < sz; ++i)
    {
      output[i] = generate();
    }
  }

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
