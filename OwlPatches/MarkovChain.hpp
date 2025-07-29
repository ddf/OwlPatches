#pragma once

#include "SignalGenerator.h"
#include "ComplexShortArray.h"
#include "basicmaths.h"
#include "HashMap.h"

template<class SAMPLE_T, int CHANNEL_COUNT>
class MarkovChain
{
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
    explicit Frame(S v) : x(v) {}
    explicit operator S() const { return x; }
    S key() const { return x; }
  };

  template<class S>
  struct Frame<S, 2>
  {
    S x, y;

    Frame() : x(0), y(0) {}
    explicit Frame(S x) : x(x), y(x) {}
    Frame(S x, S y) : x(x), y(y) {}
    S left() const { return x; }
    S right() const { return y; }
    S key() const { return x; }
  };

  typedef Frame<SAMPLE_T, CHANNEL_COUNT> SampleFrame;

  struct MemoryNode
  {
    SampleFrame sampleFrame;
    // next node with same sampleFrame key
    MemoryNode* next = nullptr;
    // prev nod with same sampleFrame key
    MemoryNode* prev = nullptr;
  };

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
  MemoryNode* memory;
  MemoryNode* prevGenerateNode;
  int memorySize;
  int memoryWriteIdx;
  int maxWordSize;
  int currentWordBegin;
  int currentWordSize;
  int letterCount;
  
  struct KeyCount
  {
    MemoryNode* nodeList;
    uint32_t count;
  };
  typedef HashMap<SAMPLE_T, KeyCount, 4096, 1<<16> KeyCountMap;
  // keep track of how many nodes in our memory have a particular key
  KeyCountMap sampleFrameKeyCounts;

public:
  explicit MarkovChain(const int inBufferSize)
    : memory(nullptr), memorySize(inBufferSize), memoryWriteIdx(0)
    , maxWordSize(2), currentWordBegin(0), currentWordSize(1), letterCount(0)
  {
    memory = new MemoryNode[memorySize];
    // so that we don't have to deal with nullptr when writing to memory the first time,
    // we assign all prev and next nodes
    MemoryNode* head = memory;
    MemoryNode* tail = memory + memorySize - 1;
    head->prev = tail;
    tail->next = head;
    while (head != tail)
    {
      MemoryNode* next = head + 1;
      head->next = next;
      next->prev = head;
      head = next;
    }

    prevGenerateNode = &memory[0];
  }

  ~MarkovChain()
  {
    delete[] memory;
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
    MemoryNode* writeToNode = &memory[memoryWriteIdx];
    // if the sample frame we are learning has a different key than the one we are overwriting
    if (sampleFrame.key() != writeToNode->sampleFrame.key())
    {
      // remove this node from its linked list
      MemoryNode* prev = writeToNode->prev;
      MemoryNode* next = writeToNode->next;
      prev->next = next;
      next->prev = prev;

      //  update key counts for the frame we are overwriting
      const SAMPLE_T prevKey = writeToNode->sampleFrame.key();
      if (auto prevKeyCount = sampleFrameKeyCounts.get(prevKey))
      {
        KeyCount& value = prevKeyCount->value;
        if (value.count > 0)
        {
          value.count -= 1;
        }
        if (value.count == 0)
        {
          sampleFrameKeyCounts.remove(prevKey);
        }
      }

      const SAMPLE_T newKey = sampleFrame.key();
      // if we already have some nodes with this key, insert it into the list for the new key 
      if (auto newKeyCount = sampleFrameKeyCounts.get(newKey))
      {
        MemoryNode* insertNode = newKeyCount->value.nodeList;
        writeToNode->next = insertNode;
        writeToNode->prev = insertNode->prev;
        insertNode->prev = writeToNode;
        if (newKeyCount->value.count == 1)
        {
          insertNode->next = writeToNode;
        }
        //  update the key count
        newKeyCount->value.count += 1;
      }
      else
      {
        // this is the only node with the new key in memory, make it point to itself and initialize the count
        writeToNode->prev = writeToNode;
        writeToNode->next = writeToNode;
        sampleFrameKeyCounts.put(newKey, { writeToNode, 1 });
      }
    }
    
    // finally, update the sample frame value in the node
    writeToNode->sampleFrame = sampleFrame;

    memoryWriteIdx = (memoryWriteIdx + 1) % memorySize;
  }

  void learn(SimpleArray<SampleFrame> input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      learn(input[i]);
    }
  }

  SampleFrame gen()
  {
    MemoryNode* genNode = nullptr;
    if (letterCount == 0)
    {
      SAMPLE_T prevKey = prevGenerateNode ? prevGenerateNode->sampleFrame.key() : 0;
      auto prevKeyCount = sampleFrameKeyCounts.get(prevKey);
      const uint32_t keyCountValue = prevKeyCount ? prevKeyCount->value.count : 0;
      switch (keyCountValue)
      {
        // there are no samples with this key in memory (this should never happen)
        case 0: 
        {
          genNode = beginWordAtZero();
        }
        break;

        // this is the only sample with this key in memory, so start a new word
        case 1: 
        {
          genNode = beginWordAtZero();
        }
        break;

        // there are at least 2 nodes with this key, randomly choose one of them
        // and start the next word at the sample frame that follows it in memory
        default:
        {
          uint32_t steps = (static_cast<uint32_t>(rand()) % keyCountValue) + 1;
          MemoryNode* node = prevGenerateNode;
          while (steps--)
          {
            node = node ? node->next : nullptr;
          }
          long long nextWordBegin = node ? (node - memory) : currentWordBegin;
          nextWordBegin %= memorySize;
          // don't start a new word from the same place as our previous word,
          // also don't start a new word at the end of the memory buffer
          // because the next sample value did not actually follow the previous one in time.
          if (nextWordBegin == currentWordBegin)
          {
            genNode = beginWordAtZero();
          }
          else
          {
            genNode = &memory[(nextWordBegin+1)%memorySize];
            currentWordBegin = static_cast<int>(nextWordBegin);
          }
        }
        break;
      }

      letterCount = 1;
      currentWordSize = maxWordSize;
    }
    else
    {
      int genIdx = (currentWordBegin + letterCount) % memorySize;
      genNode = &memory[genIdx];
      ++letterCount;
      if (letterCount == currentWordSize)
      {
        letterCount = 0;
      }
    }

    prevGenerateNode = genNode;
    return genNode ? genNode->sampleFrame : SampleFrame();
  }

  Stats getStats() const
  {
    int memSize = 0;
    int minLength = 0;
    int minCount = 0;
    int maxLength = 0;
    int maxCount = 0;
    int totalCount = 0;
    // for (int i = 1; i < MEMORY_PER_NODE + 1; ++i)
    // {
    //   int nodeLength = i;
    //   int nodeCountWithLength = nodeLengthCounts[i];
    //   memSize += nodeCountWithLength;
    //   
    //   if (nodeCountWithLength > 0 && minLength == 0)
    //   {
    //     minLength = nodeLength;
    //     minCount = nodeCountWithLength;
    //   }
    //
    //   if (nodeCountWithLength > 0 && nodeLength > maxLength)
    //   {
    //     maxLength = nodeLength;
    //     maxCount = nodeCountWithLength;
    //   }
    //
    //   totalCount += nodeCountWithLength * nodeLength;
    // }
    float avg = memSize > 0 ? (float)totalCount / memSize : 0;
    return Stats{ memSize, minLength, minCount, maxLength, maxCount, avg };
  }

private:
  MemoryNode* beginWordAtZero()
  {
    // pick a random offset from the oldest sample frame in memory to start
    int nextWordBegin = memoryWriteIdx + 1 + (rand() % memorySize);
    // if there is at least one frame with a key equal to zero, start from the cached node in that list
    if (auto zeroKeyCount = sampleFrameKeyCounts.get(0))
    {
      const KeyCount& value = zeroKeyCount->value;
      nextWordBegin = static_cast<int>(value.nodeList - memory);
    }
    currentWordBegin = nextWordBegin % memorySize;
    return &memory[currentWordBegin];
  }

public:
  static MarkovChain<SAMPLE_T, CHANNEL_COUNT>* create(int bufferSize)
  {
    return new MarkovChain<SAMPLE_T, CHANNEL_COUNT>(bufferSize);
  }

  static void destroy(MarkovChain<SAMPLE_T, CHANNEL_COUNT>* markov)
  {
    if (markov)
      delete markov;
  }
};

class ShortMarkovGenerator final : public MarkovChain<int16_t, 1>, SignalGenerator
{
  explicit ShortMarkovGenerator(const int bufferSize) : MarkovChain(bufferSize)
  {

  }

public:
  void learn(const float value)
  {
    MarkovChain::learn(SampleFrame(static_cast<int16_t>(value * 32767)));
  }

  float generate() override 
  {
    return static_cast<float>(gen().x) * 0.0000305185f;
  }

  static ShortMarkovGenerator* create(const int bufferSize)
  {
    return new ShortMarkovGenerator(bufferSize);
  }

  static void destroy(const ShortMarkovGenerator* markov)
  {
    delete markov;
  }
};

class ComplexShortMarkovGenerator final : public MarkovChain<int16_t, 2>, ComplexSignalGenerator
{
  explicit ComplexShortMarkovGenerator(const int bufferSize) : MarkovChain(bufferSize)
  {

  }

public:
  void learn(const ComplexFloat value)
  {
    const SampleFrame frame(static_cast<int16_t>(value.re * 32767), static_cast<int16_t>(value.im * 32767));
    MarkovChain::learn(frame);
  }

  ComplexFloat generate() override
  {
    const SampleFrame frame = gen();
    return { static_cast<float>(frame.left()) * 0.0000305185f, static_cast<float>(frame.right()) * 0.0000305185f };
  }

  static ComplexShortMarkovGenerator* create(const int bufferSize)
  {
    return new ComplexShortMarkovGenerator(bufferSize);
  }

  static void destroy(const ComplexShortMarkovGenerator* markov)
  {
    delete markov;
  }
};
