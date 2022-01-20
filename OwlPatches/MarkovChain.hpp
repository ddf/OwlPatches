#include "SignalGenerator.h"
#include "basicmaths.h"

#define MEMORY_SIZE (1<<16)
#define MEMORY_MAX_NODES MEMORY_SIZE*1
#define MEMORY_PER_NODE 4

template<class Sample>
class MarkovChain
{
  template<class K, class V>
  class Memory
  {
  public:
    class Node
    {
    public:
      Node* next;

      K   key;
      V   values[MEMORY_PER_NODE];
      uint8_t  valuesLength;

      Node(K k)
        : next(0), key(k), valuesLength(0)
      {
        memset(values, 0, MEMORY_PER_NODE * sizeof(V));
      }

      bool write(V value)
      {
        if (valuesLength < MEMORY_PER_NODE)
        {
          // don't write samples we already know about
          for (int i = 0; i < valuesLength; ++i)
          {
            if (values[i] == value) return false;
          }
          values[valuesLength++] = value;
          return true;
        }
        return false;
      }

      bool erase(V value)
      {
        for (int i = 0; i < valuesLength; ++i)
        {
          // when we find the value in the list, swap with value at end of list, reduce list length
          if (values[i] == value)
          {
            values[i] = values[valuesLength - 1];
            --valuesLength;
            return true;
          }
        }
        return false;
      }
    };

  private:

    Node* nodeTable[MEMORY_SIZE];
    Node* nodePool[MEMORY_MAX_NODES];
    int nodeCount;

  public:
    Memory() : nodeCount(0)
    {
      memset(nodeTable, 0, MEMORY_SIZE * sizeof(Node*));
      for (int i = 0; i < MEMORY_MAX_NODES; ++i)
      {
        nodePool[i] = new Node(K(0));
      }
    }

    ~Memory()
    {
      // delete all nodes in the table
      for (int i = 0; i < MEMORY_SIZE; ++i)
      {
        Node* node = nodeTable[i];
        while (node)
        {
          Node* next = node->next;
          delete node;
          node = next;
        }
      }

      // delete all nodes that weren't allocated to the table
      for (int i = nodeCount; MEMORY_MAX_NODES; ++i)
      {
        delete nodePool[i];
      }
    }

    Node* get(K key)
    {
      uint32_t idx = hash(key) & (MEMORY_SIZE - 1);
      Node* node = nodeTable[idx];
      while (node && node->key != key)
      {
        node = node->next;
      }

      return node;
    }

    Node* put(K key)
    {
      if (nodeCount < MEMORY_MAX_NODES)
      {
        uint32_t idx = hash(key) & (MEMORY_SIZE - 1);
        Node* node = nodeTable[idx];
        if (node)
        {
          while (node->next)
          {
            node = node->next;
          }
          node->next = allocateNode(key);
          node = node->next;
        }
        else
        {
          nodeTable[idx] = allocateNode(key);
          node = nodeTable[idx];
        }
        return node;
      }
      return 0;
    }

    void remove(K key)
    {
      uint32_t idx = hash(key) & (MEMORY_SIZE - 1);
      Node* prevNode = 0;
      Node* node = nodeTable[idx];
      while (node && node->key != key)
      {
        prevNode = node;
        node = node->next;
      }
      if (node)
      {
        if (prevNode)
        {
          prevNode->next = node->next;
        }
        else
        {
          nodeTable[idx] = node->next;
        }
        deallocateNode(node);
      }
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

    Node* allocateNode(K key)
    {
      Node* node = nodePool[nodeCount];
      nodePool[nodeCount] = 0;
      node->key = key;
      node->valuesLength = 0;
      node->next = 0;
      ++nodeCount;
      return node;
    }

    void deallocateNode(Node* node)
    {
      // this is probably too slow
      //for (int i = 0; i < nodeCount; ++i)
      //{
      //  if (nodePool[i] == node)
      //  {
      //    int swapIdx = nodeCount - 1;
      //    nodePool[i] = nodePool[swapIdx];
      //    nodePool[swapIdx] = node;
      //    --nodeCount;
      //    break;
      //  }
      //}
      if (nodeCount > 0)
      {
        --nodeCount;
        nodePool[nodeCount] = node;
      }
    }
  };

  typedef Memory<Sample, int> SampleMemory;

  Sample* buffer;
  int bufferSize;
  int bufferWritePos;
  SampleMemory*  memory;
  typename SampleMemory::Node* zeroNode;
  uint32_t totalWrites;
  Sample   lastLearn;
  Sample   lastGenerate;
  int      lastWordBegin;
  int      maxWordSize;
  int      currentWordSize;
  int      letterCount;

public:
  MarkovChain()
    : buffer(0), bufferWritePos(0), memory(0)
    , lastWordBegin(0), maxWordSize(1), currentWordSize(1), letterCount(0)
  {
    bufferSize = MEMORY_MAX_NODES;
    buffer = new Sample[bufferSize];
    memset(buffer, 0, bufferSize * sizeof(Sample));
    memory = new SampleMemory();
    lastLearn = Sample(0);
    lastGenerate = Sample(0);
    zeroNode = memory->put(lastLearn);
    zeroNode->write(0);
  }

  ~MarkovChain()
  {
    delete[] buffer;

    if (memory)
      delete memory;
  }

  void resetGenerate()
  {
    lastGenerate = Sample(0);
    letterCount = 0;
  }

  int getLetterCount() const
  {
    return letterCount;
  }

  int getCurrentWordSize() const
  {
    return currentWordSize;
  }

  void setWordSize(int length)
  {
    maxWordSize = std::max(1, length);
  }

  void learn(Sample sample)
  {
    const int nextWritePosition = (bufferWritePos + 1) % bufferSize;

    // erase the position we are about to write to from the next sample list of what's already there
    Sample prevSample = buffer[bufferWritePos];
    typename SampleMemory::Node* node = memory->get(prevSample);
    // we do not want the zero node to be removed from memory
    // and since we wrote a zero as the first value to the zero node,
    // we do not erase it.
    if (node && !(node == zeroNode && nextWritePosition == 0))
    {
      if (node->erase(nextWritePosition))
      {
        --totalWrites;
        if (node->valuesLength == 0)
        {
          memory->remove(prevSample);
        }
      }
    }

    buffer[bufferWritePos] = sample;

    node = memory->get(lastLearn);
    if (!node)
    {
      node = memory->put(lastLearn);
    }
    if (node && node->write(bufferWritePos))
    {
      ++totalWrites;
    }
    bufferWritePos = nextWritePosition;
    lastLearn = sample;
  }

  void learn(SimpleArray<Sample> input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      learn(input[i]);
    }
  }

  Sample generate()
  {
    if (letterCount == 0)
    {
      SampleMemory::Node* node = memory->get(lastGenerate);
      if (!node) node = zeroNode;
      switch (node->valuesLength)
      {
        // nothing follows, restart at zero
        case 0: 
        {
          resetGenerate();
        }
        break;

        // node has only one sample that follows it, do that unless it is the same value as the sample itself
        case 1: 
        {
          int nextIdx = node->values[0];
          Sample next = buffer[nextIdx];
          if (node->key != next)
          {
            lastGenerate = next;
            lastWordBegin = nextIdx;
          }
          else
          {
            resetGenerate();
          }
        }
        break;

        default:
        {
          int idx = arm_rand32() % node->valuesLength;
          int nextIdx = node->values[idx];
          if (nextIdx == lastWordBegin)
          {
            resetGenerate();
          }
          else
          {
            lastGenerate = buffer[nextIdx];
            lastWordBegin = nextIdx;
          }
        }
        break;
      }

      letterCount = 1;
      // random word size with each word within our max bound
      // otherwise longer words can get stuck repeating the same data.
      //currentWordSize += arm_rand32() % 8;
      //if (currentWordSize > maxWordSize)
      //{
      //  currentWordSize = 1 + currentWordSize % maxWordSize;
      //}
      currentWordSize = maxWordSize;
    }
    else
    {
      int genIdx = (lastWordBegin + letterCount) % bufferSize;
      lastGenerate = buffer[genIdx];
      ++letterCount;
      if (letterCount == currentWordSize)
      {
        letterCount = 0;
      }
    }
    return lastGenerate;
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

public:
  static MarkovChain<Sample>* create()
  {
    return new MarkovChain<Sample>();
  }

  static void destroy(MarkovChain<Sample>* markov)
  {
    if (markov)
      delete markov;
  }
};
