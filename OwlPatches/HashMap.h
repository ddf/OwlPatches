#ifndef __HASH_MAP__
#define __HASH_MAP__

template<typename K, typename V>
class HashNode
{
public:
  K     key;
  V     value;
  HashNode* next;

  HashNode() : next(0)
  {

  }

  HashNode(K k)
    : next(0), key(k)
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
    return int(key) + 32767;
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
  int nodeCount;
  H hash;

public:
  HashMap() : nodeCount(0)
  {
    memset(nodeTable, 0, TABLE_SIZE * sizeof(Node*));
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
    for (int i = nodeCount; i < MAX_NODES; ++i)
    {
      delete nodePool[i];
    }
  }

  Node* get(K key)
  {
    uint32_t idx = hash(key) & (TABLE_SIZE - 1);
    Node* node = nodeTable[idx];
    while (node && node->key != key)
    {
      node = node->next;
    }

    return node;
  }

  Node* put(K key)
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
    return 0;
  }

  void remove(K key)
  {
    uint32_t idx = hash(key) & (TABLE_SIZE - 1);
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

  Node* allocateNode(K key)
  {
    Node* node = nodePool[nodeCount];
    nodePool[nodeCount] = 0;
    node->key = key;
    node->value = V();
    node->next = 0;
    ++nodeCount;
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
};

#endif // __HASH_MAP__

