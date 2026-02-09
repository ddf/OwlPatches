#pragma once

#include "vessl/vessl.h"
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
    // we cache the key in the node so that keyFunc can change behavior over time
    // without breaking our existing memory chains.
    KEY_T    key;
    // the actual sample data we use in generate
    SAMPLE_T sample;
    // @todo I think we can get away with a singly-linked list here
    // next node with the same key
    MemoryNode* next = nullptr;
    // prev node with the same key
    MemoryNode* prev = nullptr;
  };

public:
  struct Stats
  {
    int chainCount; // the total number of unique chains (i.e. keys) in our key count map
    int minChainLength; // the shortest list of nodes with the same key
    int minChainCount; // the number of keys with a node count equal to minChainLength
    int maxChainLength; // the longest list of nodes with the same key
    int maxChainCount; // the number of keys with a node count equal to maxChainLength
    float avgChainLength; // the average node list length
  };

private:
  KEY_FUNC    keyFunc;
  MemoryNode* memory;
  const MemoryNode* prevGenerateNode;
  size_t memorySize;
  size_t memoryWriteIdx;
  size_t maxWordSize;
  size_t currentWordBegin;
  size_t currentWordSize;
  size_t letterCount;
  
  struct Chain
  {
    MemoryNode* head;
    size_t length;
  };
  typedef HashMap<KEY_T, Chain, 4096, 1<<16> ChainsMap;
  // map of keys to chains of nodes that share that key
  ChainsMap chainsMap;
  
  MemoryNode* get(size_t idx) const
  {
    return &memory[idx];
  }

public:
  explicit MarkovChain(size_t inBufferSize)
    : memory(nullptr), memorySize(inBufferSize), memoryWriteIdx(0)
    , maxWordSize(2), currentWordBegin(0), currentWordSize(1), letterCount(0)
  {
    memory = new MemoryNode[memorySize];
    // @todo with a singly-linked list it might not be so bad to handle nullptr and we can skip this
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

    prevGenerateNode = get(0);
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
    maxWordSize = vessl::math::max(2, length);
  }

  void learn(const SAMPLE_T& sample)
  {
    MemoryNode* writeToNode = get(memoryWriteIdx);
    const KEY_T prevKey = keyFunc(writeToNode->sample);
    const KEY_T newKey = keyFunc(sample);
    // if the sample frame we are learning has a different key than the one we are overwriting
    if (newKey != prevKey)
    {
      // remove this node from its linked list
      MemoryNode* prev = writeToNode->prev;
      MemoryNode* next = writeToNode->next;
      prev->next = next;
      next->prev = prev;

      //  update key counts for the frame we are overwriting
      if (auto pair = chainsMap.get(prevKey))
      {
        Chain& value = pair->value;
        if (value.length > 0)
        {
          value.length -= 1;
        }
        if (value.length == 0)
        {
          chainsMap.remove(prevKey);
        }
      }
      
      // if we already have some nodes with this key, insert it into the list for the new key 
      if (auto pair = chainsMap.get(newKey))
      {
        Chain& chain = pair->value;
        MemoryNode* head = chain.head;
        writeToNode->next = head;
        writeToNode->prev = head->prev;
        head->prev = writeToNode;
        if (chain.length == 1)
        {
          head->next = writeToNode;
        }
        //  update the chain length
        chain.length += 1;
      }
      else
      {
        // this is the only node with the new key in memory, make it point to itself and initialize the chain length
        writeToNode->prev = writeToNode;
        writeToNode->next = writeToNode;
        chainsMap.put(newKey, { writeToNode, 1 });
      }
    }
    
    // finally, update the sample frame value in the node
    writeToNode->sample = sample;

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
    const MemoryNode* genNode;
    if (letterCount == 0)
    {
      const KEY_T prevKey = prevGenerateNode ? prevGenerateNode->key: KEY_T();
      const auto prevKeyPair = chainsMap.get(prevKey);
      const uint32_t prevKeyChainLength = prevKeyPair ? prevKeyPair->value.length : 0;
      switch (prevKeyChainLength)
      {
        // there are no samples with this key in memory
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
          genNode = beginWordFromChain<true>(prevKeyPair->value);
        }
        break;
      }

      letterCount = 1;
      // @todo we should make sure our word won't read past the end of the circular buffer and shorten then word size if it will.
      // this is also something the beginWord methods could take into account when choosing a node.
      currentWordSize = maxWordSize;
    }
    else
    {
      const int genIdx = (currentWordBegin + letterCount) % memorySize;
      genNode = get(genIdx);
      ++letterCount;
      if (letterCount == currentWordSize)
      {
        letterCount = 0;
      }
    }

    prevGenerateNode = genNode;
    return genNode->sample;
  }

  Stats getStats() const
  {
    const size_t chainCount = chainsMap.size();
    int minLength = memorySize;
    int minLengthCount = 0;
    int maxLength = 0;
    int maxLengthCount = 0;
    int chainLengthAccum = 0;
    
    for (const typename ChainsMap::Node* node : chainsMap)
    {
      const int chainLength = node->value.length;
    
      if (chainLength < minLength)
      {
        minLength = chainLength;
        minLengthCount = 1;
      }
      else if (chainLength == minLength)
      {
        minLengthCount++;
      }
      else if (chainLength > maxLength)
      {
        maxLength = chainLength;
        maxLengthCount = 1;
      }
      else if (chainLength == maxLength)
      {
        maxLengthCount++;
      }
      
      chainLengthAccum += chainLength;
    }
    const float avg = chainCount > 0 ? static_cast<float>(chainLengthAccum) / static_cast<float>(chainCount) : 0;
    return Stats { static_cast<int>(chainCount), minLength, minLengthCount, maxLength, maxLengthCount, avg };
  }
  
  float getWordProgress() const
  {
    return static_cast<float>(letterCount) / static_cast<float>(currentWordSize);
  }

private:
  // always returns a valid node
  MemoryNode* beginWordAtZero()
  {
    // pick a random offset from the oldest sample frame in memory to start
    const int nextWordBegin = memoryWriteIdx + (rand() % memorySize);
    // if there is at least one frame with a key equal to zero, start from a random node in that list
    if (auto zeroKeyPair = chainsMap.get(0))
    {
      if (MemoryNode* node = beginWordFromChain<false>(zeroKeyPair->value))
      {
        return node;
      }
    }
    currentWordBegin = nextWordBegin % memoryWriteIdx;
    return get(currentWordBegin);
  }

  template<bool FALLBACK_TO_ZERO>
  MemoryNode* beginWordFromChain(const Chain& chain)
  {
    int steps = rand() % chain.length;
    MemoryNode* node = chain.head;
    while (steps != 0 && node != nullptr)
    {
      node = node->next;
      --steps;
    }
    const long long nodeIdx = node ? (node - memory) : -1;
    if (nodeIdx == -1)
    {
      if (FALLBACK_TO_ZERO)
      {
        return beginWordAtZero();
      }
      return nullptr;
    }

    // don't start a new word from the same place as our previous word.
    // also don't start a new word at our memory write head
    // because that sample did not actually follow the previous one in time.
    const uint32_t nextWordBegin = static_cast<int>((nodeIdx+1)%memorySize);
    if (nextWordBegin == currentWordBegin || nextWordBegin == memoryWriteIdx)
    {
      if (FALLBACK_TO_ZERO)
      {
        return beginWordAtZero();
      }
      return nullptr;
    }

    currentWordBegin = nextWordBegin;
    return get(nextWordBegin);
  }
};