#include "SignalGenerator.h"
#include "basicmaths.h"

#define FLOAT_TO_UNSIGNED 65535
#define UNSIGNED_TO_FLOAT (1.0f/65535)

#define UNSIGNED(x) ((uint16_t)((x*0.5f + 0.5f)*FLOAT_TO_UNSIGNED))
#define FLOAT(x)    ((x*UNSIGNED_TO_FLOAT)*2 - 1)

#define MEMORY_PER_SAMPLE 8 // must be power of 2

template<int SIZE>
struct SampleMemory
{
  uint16_t samples[SIZE];
  uint16_t writePosition;

  void write(uint16_t sample)
  {
    samples[writePosition] = sample;
    writePosition = (writePosition + 1) & (SIZE - 1);
  }
};

class MarkovChain : public SignalGenerator
{
  typedef SampleMemory<MEMORY_PER_SAMPLE> MemType;

  MemType* memory;
  uint16_t lastLearn;
  uint16_t lastGenerate;

public:
  MarkovChain()
    : memory(0), lastLearn(0), lastGenerate(0)
  {
    memory = new MemType[FLOAT_TO_UNSIGNED];
    memset(memory, 0, FLOAT_TO_UNSIGNED * sizeof(MemType));
  }

  ~MarkovChain()
  {
    if (memory)
      delete[] memory;
  }

  void setLastLearn(float value)
  {
    lastLearn = UNSIGNED(value);
  }

  void setLastGenerate(float value)
  {
    lastGenerate = UNSIGNED(value);
  }

  void learn(FloatArray input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      uint16_t sample = UNSIGNED(input[i]);
      memory[lastLearn].write(sample);
      lastLearn = sample;
    }
  }

  float generate() override
  {
    lastGenerate = memory[lastGenerate].samples[rand()&(MEMORY_PER_SAMPLE - 1)];
    return FLOAT(lastGenerate);
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
