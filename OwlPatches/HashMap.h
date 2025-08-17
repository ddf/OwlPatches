#pragma once
#include <cstdint>

template<typename K, typename V>
class HashNode
{
public:
  K     key;
  V     value;
  HashNode* next;

  HashNode() : next(nullptr)
  {

  }

  HashNode(K k)
    : key(k), next(nullptr)
  {
  }
};

template<typename K>
struct HashFunc
{
  uint32_t operator()(const K& key) const
  {
    return reinterpret_cast<uint32_t>(key);
  }
};

template<>
struct HashFunc<int16_t>
{
  uint32_t operator()(const int16_t& key) const
  {
    return static_cast<int32_t>(key) + 32767;
  }
};

template<typename K, typename V, int TABLE_SIZE, int MAX_NODES, typename H = HashFunc<K>>
class HashMap
{
public:
  typedef HashNode<K, V> Node;

private:

  Node* nodeTable[TABLE_SIZE];
  Node* nodePool[MAX_NODES];
  size_t nodeCount;
  H hash;

public:
  HashMap() : nodeCount(0)
  {
    memset(static_cast<void*>(nodeTable), 0, TABLE_SIZE * sizeof(Node*));
    for (int i = 0; i < MAX_NODES; ++i)
    {
      nodePool[i] = new Node();
    }
  }

  ~HashMap()
  {
    // delete all nodes in the table
    for (int i = 0; i < TABLE_SIZE; ++i)
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
    for (size_t i = nodeCount; i < MAX_NODES; ++i)
    {
      delete nodePool[i];
    }
  }

  Node* get(const K& key) const
  {
    uint32_t idx = hash(key) & (TABLE_SIZE - 1);
    Node* node = nodeTable[idx];
    while (node && node->key != key)
    {
      node = node->next;
    }

    return node;
  }

  Node* put(const K& key)
  {
    if (nodeCount < MAX_NODES)
    {
      uint32_t idx = hash(key) & (TABLE_SIZE - 1);
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
    return nullptr;
  }

  Node* put(const K& key, const V& value)
  {
    Node* node = put(key);
    if (node)
    {
      node->value = value;
    }
    return node;
  }

  void remove(K key)
  {
    uint32_t idx = hash(key) & (TABLE_SIZE - 1);
    Node* prevNode = nullptr;
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
  
  size_t size() const { return nodeCount; }

private:

  Node* allocateNode(const K& key)
  {
    Node* node = nodeCount < MAX_NODES ? nodePool[nodeCount] : nullptr;
    if (node)
    {
      nodePool[nodeCount] = 0;
      node->key = key;
      node->value = V();
      node->next = 0;
      ++nodeCount;
    }
    return node;
  }

  void deallocateNode(Node* node)
  {
    if (nodeCount > 0)
    {
      --nodeCount;
      nodePool[nodeCount] = node;
    }
  }

  // iterator for ranged-for support
  class Iterator
  {
    const HashMap* map;
    size_t tableIdx;
    Node* node;

  public:
    explicit Iterator(const HashMap* map, const size_t idx) : map(map), tableIdx(idx)
    {
      node = map->nodeTable[tableIdx];
    }

    const Iterator& operator++()
    {
      if (tableIdx == TABLE_SIZE) return *this;
      
      if (node)
      {
        node = node->next;
      }

      while (!node)
      {
        ++tableIdx;
        if (tableIdx == TABLE_SIZE) break;
        node = map->nodeTable[tableIdx];
      }

      return *this;
    }

    Node*& operator*() { return node; }
    
    bool operator==(const Iterator& other)
    {
      return this->map == other.map && this->tableIdx == other.tableIdx && this->node == other.node;
    }

    bool operator!=(const Iterator& other)
    {
      return !(*this == other);
    }
  };

  Iterator iterEnd = Iterator(this, TABLE_SIZE);

public:
  Iterator begin() const
  {
    for (int i = 0; i < TABLE_SIZE; ++i)
    {
      if (nodeTable[i])
      {
        return Iterator(this, i);
      }
    }
    return iterEnd;
  }

  const Iterator& end() const
  {
    return iterEnd;
  }
};

