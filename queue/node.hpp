#pragma once

#include "concurrent_queue.hpp"

#include <memory>

/**
 * Owning pointer for Node<T>. Performs better than raw unique_ptr when destructing linked-list.
 * @tparam T
 */

template <typename T>
class Node;
template <typename T>
using DeleterType = void(*)(Node<T>*);
template <typename T>
class NodePtr : public std::unique_ptr<Node<T>, DeleterType<T>> {
 public:
  using NodeDeleterType = DeleterType<T>;
  NodePtr();
  explicit NodePtr(Node<T> *ptr);
 private:
  static void queue_node_deleter(Node<T> *ptr);
};

template <typename T>
class Node {
 public:
  NodePtr<T> next_;
  std::unique_ptr<T> data_;
};

template<typename T>
NodePtr<T>::NodePtr(Node<T> *ptr)
    :std::unique_ptr<Node<T>, NodeDeleterType>(ptr, queue_node_deleter) { }
template<typename T>
NodePtr<T>::NodePtr()
    :std::unique_ptr<Node<T>, NodeDeleterType>(nullptr, queue_node_deleter) { }
template<typename T>
void NodePtr<T>::queue_node_deleter(Node<T> *ptr) {
  auto current = ptr;
  while (current) {
    auto next = current->next_.release();
    delete current;
    current = next;
  }
}
