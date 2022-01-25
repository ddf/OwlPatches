#include "SignalGenerator.h"
#include "ComplexShortArray.h"
#include "basicmaths.h"

#define MEMORY_SIZE (1<<16)
#define MEMORY_MAX_NODES MEMORY_SIZE*1
#define MEMORY_PER_NODE 8

template<class Sample, int channels>
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

      Node() : next(0), valuesLength(0)
      {

      }

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
        nodePool[i] = new Node();
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

    uint32_t hash(ComplexShort x)
    {
      return hash(x.re) ^ hash(x.im);
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

protected:
  template<class S, int C>
  struct Frame
  {
    S data[C];

    Frame()
    {
      memset(data, 0, C * sizeof(S));
    }

    S key() const
    {
      S k = 0;
      for (int i = 0; i < C; ++i)
      {
        k += data[i];
      }
      return k;
    }
  };

  template<class S>
  struct Frame<S, 1>
  {
    S x;

    Frame() : x(0) {}
    Frame(S v) : x(v) {}
    operator S() const { return x; }
    S key() const { return x; }
  };

  template<class S>
  struct Frame<S, 2>
  {
    S x, y;

    Frame() : x(0), y(0) {}
    Frame(S _x) : x(_x), y(_x) {}
    Frame(S _x, S _y) : x(_x), y(_y) {}
    S left() const { return x; }
    S right() const { return y; }
    S key() const { return x*0.5f + y*0.5f; }
  };

  typedef Frame<Sample, channels> SampleFrame;

public:
  struct Stats
  {
    int memorySize;
    int minChainLength;
    int minChainCount;
    int maxChainLength;
    int maxChainCount;
    float avgChainLength;
  };

private:
  SampleFrame* buffer;
  int bufferSize;
  int bufferWritePos;
  SampleMemory*  memory;
  typename SampleMemory::Node* zeroNode;
  SampleFrame lastLearn;
  SampleFrame lastGenerate;
  int      lastWordBegin;
  int      maxWordSize;
  int      currentWordSize;
  int      letterCount;

  int nodeLengthCounts[MEMORY_PER_NODE+1];

public:
  MarkovChain(int inBufferSize)
    : buffer(0), bufferWritePos(0), memory(0), bufferSize(inBufferSize)
    , lastWordBegin(0), maxWordSize(1), currentWordSize(1), letterCount(0)
    , lastLearn(0), lastGenerate(0)
  {
    buffer = new SampleFrame[bufferSize];
    memory = new SampleMemory();

    for (int i = 0; i < MEMORY_PER_NODE + 1; ++i)
    {
      nodeLengthCounts[i] = 0;
    }

    zeroNode = memory->put(lastLearn.key());
    zeroNode->write(0);

    nodeLengthCounts[1] = 1;
  }

  ~MarkovChain()
  {
    delete[] buffer;

    if (memory)
      delete memory;
  }

  void resetGenerate()
  {
    lastGenerate = SampleFrame(0);
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

  void learn(SampleFrame sampleFrame)
  {
    const int nextWritePosition = (bufferWritePos + 1) % bufferSize;

    // erase the position we are about to write to from the next sample list of what's already there
    SampleFrame prevSampleFrame = buffer[bufferWritePos];
    typename SampleMemory::Node* node = memory->get(prevSampleFrame.key());
    // we do not want the zero node to be removed from memory
    // and since we wrote a zero as the first value to the zero node,
    // we do not erase it.
    if (node && !(node == zeroNode && nextWritePosition == 0))
    {
      int prevLen = node->valuesLength;
      if (node->erase(nextWritePosition))
      {
        nodeLengthCounts[prevLen] = nodeLengthCounts[prevLen] - 1;

        if (node->valuesLength == 0)
        {
          memory->remove(prevSampleFrame.key());
        }
        else
        {
          nodeLengthCounts[node->valuesLength] = nodeLengthCounts[node->valuesLength] + 1;
        }
      }
    }

    buffer[bufferWritePos] = sampleFrame;

    node = memory->get(lastLearn.key());
    if (!node)
    {
      node = memory->put(lastLearn.key());
    }
    if (node)
    {
      int prevLen = node->valuesLength;
      if (node->write(bufferWritePos))
      {
        // we don't keep track of zero length nodes because they get removed from memory
        if (prevLen != 0)
          nodeLengthCounts[prevLen] = nodeLengthCounts[prevLen] - 1;

        nodeLengthCounts[node->valuesLength] = nodeLengthCounts[node->valuesLength] + 1;
      }
    }
    bufferWritePos = nextWritePosition;
    lastLearn = sampleFrame;
  }

  void learn(SimpleArray<SampleFrame> input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      learn(input[i]);
    }
  }

  SampleFrame generate()
  {
    if (letterCount == 0)
    {
      typename SampleMemory::Node* node = memory->get(lastGenerate.key());
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
          SampleFrame next = buffer[nextIdx];
          if (node->key != next.key())
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
      // start a new word when we finish this one
      // or if the next read would cross from our last recorded sample
      // to where the next one will be written
      if (letterCount == currentWordSize || lastWordBegin + letterCount == bufferWritePos)
      {
        letterCount = 0;
      }
    }
    return lastGenerate;
  }

  Stats getStats() const
  {
    int memSize = 0;
    int minLength = 0;
    int minCount = 0;
    int maxLength = 0;
    int maxCount = 0;
    int totalCount = 0;
    for (int i = 1; i < MEMORY_PER_NODE + 1; ++i)
    {
      int nodeLength = i;
      int nodeCountWithLength = nodeLengthCounts[i];
      memSize += nodeCountWithLength;
      
      if (nodeCountWithLength > 0 && minLength == 0)
      {
        minLength = nodeLength;
        minCount = nodeCountWithLength;
      }

      if (nodeCountWithLength > 0 && nodeLength > maxLength)
      {
        maxLength = nodeLength;
        maxCount = nodeCountWithLength;
      }

      totalCount += nodeCountWithLength * nodeLength;
    }
    float avg = memSize > 0 ? (float)totalCount / memSize : 0;
    return Stats{ memSize, minLength, minCount, maxLength, maxCount, avg };
  }

public:
  static MarkovChain<Sample, channels>* create(int bufferSize)
  {
    return new MarkovChain<Sample, channels>(bufferSize);
  }

  static void destroy(MarkovChain<Sample, channels>* markov)
  {
    if (markov)
      delete markov;
  }
};

class ShortMarkovGenerator : public MarkovChain<int16_t, 1>, SignalGenerator
{
  ShortMarkovGenerator(int bufferSize) : MarkovChain(bufferSize)
  {

  }

public:
  void learn(float value)
  {
    MarkovChain::learn(SampleFrame((int16_t)(value * 32767)));
  }

  float generate() override 
  {
    return MarkovChain::generate() * 0.0000305185f;
  }

  static ShortMarkovGenerator* create(int bufferSize)
  {
    return new ShortMarkovGenerator(bufferSize);
  }

  static void destroy(ShortMarkovGenerator* markov)
  {
    if (markov)
      delete markov;
  }
};

class ComplexShortMarkovGenerator : public MarkovChain<int16_t, 2>, ComplexSignalGenerator
{
  ComplexShortMarkovGenerator(int bufferSize) : MarkovChain(bufferSize)
  {

  }

public:
  void learn(ComplexFloat value)
  {
    const SampleFrame frame((int16_t)(value.re * 32767), (int16_t)(value.im * 32767));
    MarkovChain::learn(frame);
  }

  ComplexFloat generate() override
  {
    SampleFrame frame = MarkovChain::generate();
    return ComplexFloat(frame.left() * 0.0000305185f, frame.right() * 0.0000305185f);
  }

  static ComplexShortMarkovGenerator* create(int bufferSize)
  {
    return new ComplexShortMarkovGenerator(bufferSize);
  }

  static void destroy(ComplexShortMarkovGenerator* markov)
  {
    if (markov)
      delete markov;
  }
};
