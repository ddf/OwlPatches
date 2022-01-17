#include "SignalGenerator.h"
#include "basicmaths.h"

typedef int16_t Sample;
#define MEMORY_SIZE (1<<15)
#define MEMORY_MAX_NODES MEMORY_SIZE*5
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
        //for (int i = 0; i < writePosition; ++i)
        //{
        //  if (nextSample[i] == sample) return false;
        //}
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
      uint32_t idx = hash(sample) & (MEMORY_SIZE-1);
      MemoryNode* node = nodeTable[idx];
      while (node && node->thisSample != sample)
      {
        node = node->nextNode;
      }

      return node;
    }

    MemoryNode* put(Sample sample)
    {
      if (nodeCount < MEMORY_MAX_NODES)
      {
        uint32_t idx = hash(sample) & (MEMORY_SIZE-1);
        MemoryNode* node = nodeTable[idx];
        if (node)
        {
          while (node->nextNode)
          {
            node = node->nextNode;
          }
          node->nextNode = allocateNode(sample);
        }
        else
        {
          nodeTable[idx] = allocateNode(sample);
        }
        return node;
      }
      return 0;
    }

    int size() const { return nodeCount; }

  private:

    uint32_t hash(float x)
    {
      //uint32_t ui;
      //memcpy(&ui, &f, sizeof(float));
      //return ui & 0xfffff000;

      union
      {
        float f;
        uint32_t u;
      };
      f = x;
      return u;
    }

    uint32_t hash(int16_t x)
    {
      union
      {
        int16_t i;
        uint32_t u;
      };
      i = x;
      return u;
    }

    MemoryNode* allocateNode(Sample sample)
    {
      MemoryNode* node = nodePool[nodeCount];
      node->thisSample = sample;
      ++nodeCount;
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

  void setLastGenerate(float value)
  {
    lastGenerate = toSample(value);
  }

  void learn(float value)
  {
    Sample sample = toSample(value);
    MemoryNode* node = memory->get(lastLearn);
    if (!node)
    {
      node = memory->put(lastLearn);
    }
    if (node && node->write(sample))
    {
      ++totalWrites;
    }
    lastLearn = value;
  }

  void learn(FloatArray input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      learn(input[i]);
    }
  }

  float generate() override
  {
    MemoryNode* node = memory->get(lastGenerate);
    if (!node) node = memory->get(0);
    lastGenerate = node ? node->generate() : 0;
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
    return value * 32767;
  }

  inline float toFloat(Sample value) const
  {
    return value * 0.0000305185f;
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
