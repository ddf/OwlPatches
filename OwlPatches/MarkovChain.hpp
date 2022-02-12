#include "SignalGenerator.h"
#include "ComplexShortArray.h"
#include "basicmaths.h"
#include "HashMap.h"

#define MEMORY_SIZE (1<<16)
#define MEMORY_MAX_NODES MEMORY_SIZE*1
#define MEMORY_PER_NODE 8

template<class Sample, int channels>
class MarkovChain
{
  class MemorySample
  {
    int      values[MEMORY_PER_NODE];
    uint8_t  valuesLength;

  public:

    MemorySample() : valuesLength(0)
    {
    }

    int get(int idx) const
    {
      return values[idx];
    }

    bool write(int value)
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

    bool erase(int value)
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

    int length() const { return valuesLength; }
  };

  typedef HashMap<Sample, MemorySample, MEMORY_SIZE, MEMORY_MAX_NODES> Memory;
  typedef HashNode<Sample, MemorySample> MemoryNode;

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
  Memory*  memory;
  MemoryNode* zeroNode;
  SampleFrame lastLearn;
  SampleFrame lastGenerate;
  int      maxWordSize;
  int      currentWordBegin;
  int      currentWordSize;
  int      letterCount;

  int nodeLengthCounts[MEMORY_PER_NODE+1];

public:
  MarkovChain(int inBufferSize)
    : buffer(0), bufferWritePos(0), memory(0), bufferSize(inBufferSize)
    , currentWordBegin(0), maxWordSize(1), currentWordSize(1), letterCount(0)
    , lastLearn(0), lastGenerate(0)
  {
    buffer = new SampleFrame[bufferSize];
    memory = new Memory();

    for (int i = 0; i < MEMORY_PER_NODE + 1; ++i)
    {
      nodeLengthCounts[i] = 0;
    }

    zeroNode = memory->put(lastLearn.key());
    nodeLengthCounts[1] = 1;
  }

  ~MarkovChain()
  {
    delete[] buffer;

    if (memory)
      delete memory;
  }

  void resetWord()
  {
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
    maxWordSize = std::max(2, length);
  }

  void learn(SampleFrame sampleFrame)
  {
    const int nextWritePosition = (bufferWritePos + 1) % bufferSize;

    // erase the position we are about to write to from the next sample list of what's already there
    SampleFrame prevSampleFrame = buffer[bufferWritePos];
    MemoryNode* node = memory->get(prevSampleFrame.key());
    if (node)
    {
      int prevLen = node->value.length();
      if (node->value.erase(nextWritePosition))
      {
        nodeLengthCounts[prevLen] = nodeLengthCounts[prevLen] - 1;

        // never remove the zero node so we don't have to check for null
        // when falling back to zeroNode in generate.
        if (node->value.length() == 0 && node != zeroNode)
        {
          memory->remove(prevSampleFrame.key());
        }
        else
        {
          nodeLengthCounts[node->value.length()] = nodeLengthCounts[node->value.length()] + 1;
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
      int prevLen = node->value.length();
      if (node->value.write(bufferWritePos))
      {
        // we don't keep track of zero length nodes because they get removed from memory
        if (prevLen != 0)
          nodeLengthCounts[prevLen] = nodeLengthCounts[prevLen] - 1;

        nodeLengthCounts[node->value.length()] = nodeLengthCounts[node->value.length()] + 1;
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
      MemoryNode* node = memory->get(lastGenerate.key());
      if (!node) node = zeroNode;
      switch (node->value.length())
      {
        // nothing follows, restart at zero
        case 0: 
        {
          beginWordAtZero();
        }
        break;

        // node has only one sample that follows it, do that unless it is the same value as the sample itself
        case 1: 
        {
          int nextIdx = node->value.get(0);
          SampleFrame next = buffer[nextIdx];
          if (node->key != next.key())
          {
            lastGenerate = next;
            currentWordBegin = nextIdx;
          }
          else
          {
            beginWordAtZero();
          }
        }
        break;

        default:
        {
          int idx = rand() % node->value.length();
          int nextIdx = node->value.get(idx);
          if (nextIdx == currentWordBegin)
          {
            beginWordAtZero();
          }
          else
          {
            lastGenerate = buffer[nextIdx];
            currentWordBegin = nextIdx;
          }
        }
        break;
      }

      letterCount = 1;
      currentWordSize = maxWordSize;
    }
    else
    {
      int genIdx = (currentWordBegin + letterCount) % bufferSize;
      lastGenerate = buffer[genIdx];
      ++letterCount;
      if (letterCount == currentWordSize)
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

private:
  void beginWordAtZero()
  {
    lastGenerate = SampleFrame(0);
    if (zeroNode->value.length() > 0)
    {
      int idx = rand() % zeroNode->value.length();
      currentWordBegin = zeroNode->value.get(idx);
    }
    else
    {
      // pick a random position in the buffer, hope for the best?
      currentWordBegin = rand() % bufferSize;
    }
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
