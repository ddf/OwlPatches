#include "SignalGenerator.h"
#include "basicmaths.h"

typedef float Sample;
#define MEMORY_SIZE (1<<15)
#define MEMORY_MAX_NODES MEMORY_SIZE*4
#define MEMORY_PER_SAMPLE 4
#define JITTER 0.000001f

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
      memset(nextSample, 0, MEMORY_PER_SAMPLE * sizeof(Sample));
    }

    bool write(Sample sample)
    {
      //if (writePosition < MEMORY_PER_SAMPLE)
      {
        // don't write samples we already know about
        for (int i = 0; i < writePosition; ++i)
        {
          if (nextSample[i] == sample) return false;
        }
        nextSample[writePosition++] = sample;
        return true;
      }
      //return false;
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
      uint32_t idx = hash(sample) & (MEMORY_SIZE - 1);
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
        uint32_t idx = hash(sample) & (MEMORY_SIZE - 1);
        MemoryNode* node = nodeTable[idx];
        if (node)
        {
          while (node->nextNode)
          {
            node = node->nextNode;
          }
          node->nextNode = allocateNode(sample);
          node = node->nextNode;
        }
        else
        {
          nodeTable[idx] = allocateNode(sample);
          node = nodeTable[idx];
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
      return int(x) + 32767;
    }

    MemoryNode* allocateNode(Sample sample)
    {
      MemoryNode* node = nodePool[nodeCount];
      node->thisSample = sample;
      node->writePosition = 0;
      node->nextNode = 0;
      ++nodeCount;
      return node;
    }
  };

  Memory*  memory;
  MemoryNode* zeroNode;
  uint32_t totalWrites;
  Sample   lastLearn;
  Sample   lastGenerate;
  Sample   lastWordBegin;
  int      maxWordSize;
  int      currentWordSize;
  int      letterCount;

public:
  MarkovChain()
    : memory(0), maxWordSize(1), currentWordSize(1), letterCount(1)
  {
    memory = new Memory();
    lastLearn = toSample(0);
    lastGenerate = toSample(0);
    lastWordBegin = lastGenerate;
    zeroNode = memory->put(lastLearn);
  }

  ~MarkovChain()
  {
    if (memory)
      delete memory;
  }

  void resetGenerate()
  {
    lastGenerate = toSample(0);
    letterCount = currentWordSize;
  }

  void setWordSize(int length)
  {
    maxWordSize = std::max(1, length);
  }

  void setLastGenerate(float value)
  {
    lastGenerate = toSample(value);
  }

  void learn(float value)
  {
    if (value != 0) value += -JITTER + randf()*JITTER * 2;

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
    lastLearn = sample;
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
    if (!node) node = zeroNode;
    if (letterCount < currentWordSize)
    {
      lastGenerate = node->nextSample[0];
      ++letterCount;
    }
    else
    {
      switch (node->writePosition)
      {
        // nothing follows, restart at zero
        case 0: 
        {
          lastGenerate = toSample(0);
        }
        break;

        // node has only one sample that follows it, do that unless it is the same value as the sample itself
        case 1: 
        {
          lastGenerate = node->thisSample != node->nextSample[0] ? node->nextSample[0] : toSample(0);
        }
        break;

        default:
        {
          int idx = 1 + (arm_rand32() % (node->writePosition - 1));
          Sample next = node->nextSample[idx];
          if (next == lastWordBegin)
          {
            next = toSample(0);
          }
          lastGenerate = next;
        }
        break;
      }

      letterCount = 1;
      lastWordBegin = lastGenerate;
      // random word size with each word within our max bound
      // otherwise longer words can get stuck repeating the same data.
      currentWordSize += arm_rand32() % 8;
      if (currentWordSize > maxWordSize)
      {
        currentWordSize = 1 + currentWordSize % maxWordSize;
      }
    }
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
