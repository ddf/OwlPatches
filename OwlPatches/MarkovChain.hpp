#include "SignalGenerator.h"
#include "basicmaths.h"

typedef float Sample;
#define MEMORY_SIZE (1<<15)
#define MEMORY_MAX_NODES MEMORY_SIZE*4
#define MEMORY_PER_SAMPLE 4

class MarkovChain : public SignalGenerator
{
  class MemoryNode
  {
  public:
    MemoryNode* nextNode;

    Sample   thisSample;
    Sample   nextSample[MEMORY_PER_SAMPLE];
    uint8_t  writePosition;

    MemoryNode(Sample sample)
      : nextNode(0), thisSample(sample), writePosition(0)
    {

    }

    bool write(Sample sample)
    {
      if (writePosition < MEMORY_PER_SAMPLE)
      {
        // don't write samples we already know about
        for (int i = 0; i < writePosition; ++i)
        {
          if (nextSample[i] == sample) return false;
        }
        nextSample[writePosition++] = sample;
        return true;
      }
      return false;
    }

    Sample generate()
    {
      return writePosition > 0 ? nextSample[arm_rand32() % writePosition] : 0;
    }
  };

  class Memory
  {
    MemoryNode* nodeTable[MEMORY_SIZE];
    MemoryNode* nodePool[MEMORY_MAX_NODES];
    int nodeCount;

  public:
    Memory() : nodeCount(0)
    {
      memset(nodeTable, 0, MEMORY_SIZE * sizeof(MemoryNode*));
      for (int i = 0; i < MEMORY_MAX_NODES; ++i)
      {
        nodePool[i] = new MemoryNode(0);
      }
    }

    ~Memory()
    {
      for (int i = 0; MEMORY_MAX_NODES; ++i)
      {
        delete nodePool[i];
      }
    }

    MemoryNode* get(Sample sample)
    {
      uint32_t idx = hash(sample) % MEMORY_SIZE;
      MemoryNode* node = nodeTable[idx];
      if (node == 0)
      {
        node = allocateNode(sample);
        nodeTable[idx] = node;
      }
      else
      {
        while (node->thisSample != sample)
        {
          if (node->nextNode == 0)
          {
            node->nextNode = allocateNode(sample);
            return node->nextNode;
          }

          node = node->nextNode;
        }
      }

      return node;
    }

    int size() const { return nodeCount; }

  private:

    uint32_t hash(float f)
    {
      return (f * 32767) + 32767;
    }

    MemoryNode* allocateNode(float sample)
    {
      MemoryNode* node = 0;
      if (nodeCount < MEMORY_MAX_NODES)
      {
        node = nodePool[nodeCount];
        node->thisSample = sample;
        ++nodeCount;
      }

      return node;
    }
  };

  Memory*  memory;
  uint32_t totalWrites;
  Sample   lastLearn;
  Sample   lastGenerate;

public:
  MarkovChain()
    : memory(0)
  {
    memory = new Memory();
    lastLearn = toSample(0);
    lastGenerate = toSample(0);
  }

  ~MarkovChain()
  {
    if (memory)
      delete memory;
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
      MemoryNode* node = memory->get(lastLearn);
      if (node == 0) break;

      Sample sample = toSample(input[i]);
      if (node->write(sample))
      {
        ++totalWrites;
      }
      lastLearn = sample;
    }
  }

  float generate() override
  {
    MemoryNode* node = memory->get(lastGenerate);
    lastGenerate = node != 0 ? node->generate() : 0;
    return toFloat(lastGenerate);
  }

  void generate(FloatArray output) override
  {
    for (int i = 0, sz = output.getSize(); i < sz; ++i)
    {
      output[i] = generate();
    }
  }

  int getMemorySize() const
  {
    return memory->size();
  }

  float getAverageChainLength() const
  {
    int memSize = memory->size();
    return memSize > 0 ? (float)totalWrites / memSize : 0;
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
