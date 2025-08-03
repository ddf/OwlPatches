#pragma once

#include "SignalGenerator.h"
#include "ComplexShortArray.h"
#include "basicmaths.h"
#include "HashMap.h"

template<typename S, typename K>
struct KeyFunc
{
  K operator()(const S& sample) const
  {
    return reinterpret_cast<K>(sample);
  }
};

template<typename SAMPLE_T, typename KEY_T = SAMPLE_T, typename KEY_FUNC = KeyFunc<SAMPLE_T, KEY_T>>
class MarkovChain  // NOLINT(cppcoreguidelines-special-member-functions)
{
  struct MemoryNode
  {
    SAMPLE_T sampleFrame;
    // next node with same sampleFrame key
    MemoryNode* next = nullptr;
    // prev node with same sampleFrame key
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
  uint32_t memorySize;
  uint32_t memoryWriteIdx;
  uint32_t maxWordSize;
  uint32_t currentWordBegin;
  uint32_t currentWordSize;
  uint32_t letterCount;
  
  struct KeyCount
  {
    MemoryNode* nodeList;
    uint32_t count;
  };
  typedef HashMap<KEY_T, KeyCount, 4096, 1<<16> KeyCountMap;
  KEY_FUNC keyFunc;
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

  void setWordSize(const int length)
  {
    maxWordSize = std::max(2, length);
  }

  void learn(const SAMPLE_T& sampleFrame)
  {
    MemoryNode* writeToNode = &memory[memoryWriteIdx];
    // if the sample frame we are learning has a different key than the one we are overwriting
    if (keyFunc(sampleFrame) != keyFunc(writeToNode->sampleFrame))
    {
      // remove this node from its linked list
      MemoryNode* prev = writeToNode->prev;
      MemoryNode* next = writeToNode->next;
      prev->next = next;
      next->prev = prev;

      //  update key counts for the frame we are overwriting
      const KEY_T prevKey = keyFunc(writeToNode->sampleFrame);
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

      const KEY_T newKey = keyFunc(sampleFrame);
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

  void learn(const SimpleArray<SAMPLE_T> input)
  {
    for (int i = 0, sz = input.getSize(); i < sz; ++i)
    {
      learn(input[i]);
    }
  }

  SAMPLE_T generate()
  {
    MemoryNode* genNode;
    if (letterCount == 0)
    {
      const KEY_T prevKey = prevGenerateNode ? keyFunc(prevGenerateNode->sampleFrame) : KEY_T();
      auto prevKeyCount = sampleFrameKeyCounts.get(prevKey);
      const uint32_t keyCountValue = prevKeyCount ? prevKeyCount->value.count : 0;
      switch (keyCountValue)
      {
        // there are no samples with this key in memory (this should never happen)
        case 0: 
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
          uint32_t steps = arm_rand32() % keyCountValue + 1;
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
    return genNode ? genNode->sampleFrame : SAMPLE_T();
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
    const float avg = memSize > 0 ? static_cast<float>(totalCount) / static_cast<float>(memSize) : 0;
    return Stats{ memSize, minLength, minCount, maxLength, maxCount, avg };
  }
  
  float getWordProgress() const
  {
    return static_cast<float>(letterCount) / static_cast<float>(currentWordSize);
  }

private:
  MemoryNode* beginWordAtZero()
  {
    // pick a random offset from the oldest sample frame in memory to start
    int nextWordBegin = memoryWriteIdx + 1 + (arm_rand32() % memorySize);
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
  static MarkovChain* create(int bufferSize)
  {
    return new MarkovChain(bufferSize);
  }

  static void destroy(const MarkovChain* markov)
  {
    if (markov) delete markov;
  }
};

class ShortMarkovGenerator final : public SignalGenerator
{
public:
  typedef MarkovChain<int16_t> Chain;

private:
  Chain markovChain;
  
  explicit ShortMarkovGenerator(const int bufferSize) : markovChain(bufferSize)
  {

  }

public:
  Chain& chain() { return markovChain; }
  
  void learn(const float value)
  {
    markovChain.learn(static_cast<int16_t>(value * 32767));
  }

  float generate() override 
  {
    return static_cast<float>(markovChain.generate()) * 0.0000305185f;
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

class ComplexShortMarkovGenerator final : ComplexSignalGenerator
{
public:
  struct KeyFunc
  {
    int16_t operator()(const ComplexShort& value) const
    {
      return value.re;
    }
  };
  
  typedef MarkovChain<ComplexShort, int16_t, KeyFunc> Chain;

private:
  Chain markovChain;
  
  explicit ComplexShortMarkovGenerator(const int bufferSize) : markovChain(bufferSize)
  {

  }

public:
  Chain& chain() { return markovChain; }
  
  void learn(const ComplexFloat value)
  {
    markovChain.learn( { static_cast<int16_t>(value.re * 32767), static_cast<int16_t>(value.im * 32767) } );
  }

  ComplexFloat generate() override
  {
    const ComplexShort frame = markovChain.generate();
    return { static_cast<float>(frame.re) * 0.0000305185f, static_cast<float>(frame.im) * 0.0000305185f };
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

class ComplexFloatMarkovGenerator final : ComplexSignalGenerator
{
public:
  struct KeyFunc
  {
    uint32_t operator()(const ComplexFloat& value) const
    {
      // generate a unique key for this stereo frame
      // not sure what's best here - if frames are too unique, we wind up restarting words at zero all the time
      static constexpr double SCALE = (1<<16) / (2.0f * M_PI);
      return static_cast<uint32_t>((value.getPhase()+M_PI)*SCALE);
    }
  };
  
  typedef MarkovChain<ComplexFloat, uint32_t, KeyFunc> Chain;

private:
  Chain markovChain;
  
  explicit ComplexFloatMarkovGenerator(const int bufferSize) : markovChain(bufferSize)
  {

  }

public:
  Chain& chain() { return markovChain; }
  
  void learn(const ComplexFloat& value)
  {
    markovChain.learn(value);
  }

  ComplexFloat generate() override
  {
    return markovChain.generate();
  }

  static ComplexFloatMarkovGenerator* create(const int bufferSize)
  {
    return new ComplexFloatMarkovGenerator(bufferSize);
  }

  static void destroy(const ComplexFloatMarkovGenerator* markov)
  {
    delete markov;
  }
};
