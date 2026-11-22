#pragma once

#include <cstddef>
#include <cstring>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

// Small, fallible blocks: no relocating growth, throwing allocation, implicit
// clone, or per-ID heap allocation. Only reserve/push/clone can allocate.
// Shrinking retains admitted capacity for infallible rollback; clear releases it.
template <typename T>
class CheckedClippingStorage {
  struct Block {
    T rows[16]{};
    Block* next = nullptr;
  };
  Block* head = nullptr;
  Block* tail = nullptr;
  size_t used = 0;
  size_t admitted = 0;

 public:
  CheckedClippingStorage() = default;
  ~CheckedClippingStorage() { clear(); }
  CheckedClippingStorage(const CheckedClippingStorage&) = delete;
  CheckedClippingStorage& operator=(const CheckedClippingStorage&) = delete;
  CheckedClippingStorage(CheckedClippingStorage&& other) noexcept { swap(other); }
  CheckedClippingStorage& operator=(CheckedClippingStorage&& other) noexcept {
    if (this != &other) {
      clear();
      swap(other);
    }
    return *this;
  }
  void swap(CheckedClippingStorage& other) noexcept {
    std::swap(head, other.head);
    std::swap(tail, other.tail);
    std::swap(used, other.used);
    std::swap(admitted, other.admitted);
  }
  void clear() noexcept {
    while (head) {
      Block* next = head->next;
      delete head;
      head = next;
    }
    tail = nullptr;
    used = admitted = 0;
  }
  void resetSize() noexcept { used = 0; }
  size_t size() const { return used; }
  size_t capacity() const { return admitted; }
  bool empty() const { return used == 0; }
  bool reserve(size_t count) {
    while (admitted < count) {
      Block* block = new (std::nothrow) Block;
      if (!block) return false;
      if (tail)
        tail->next = block;
      else
        head = block;
      tail = block;
      admitted += 16;
    }
    return true;
  }
  T& operator[](size_t index) {
    Block* block = head;
    for (size_t n = index / 16; n; --n) block = block->next;
    return block->rows[index % 16];
  }
  const T& operator[](size_t index) const { return const_cast<CheckedClippingStorage*>(this)->operator[](index); }
  bool push_back(const T& value) {
    if (!reserve(used + 1)) return false;
    (*this)[used++] = value;
    return true;
  }
  bool push_back(T&& value) {
    if (!reserve(used + 1)) return false;
    (*this)[used++] = std::move(value);
    return true;
  }
  bool cloneFrom(const CheckedClippingStorage& source) {
    if (!reserve(source.size())) return false;
    used = source.size();
    for (size_t i = 0; i < used; ++i) (*this)[i] = source[i];
    return true;
  }
  void pop_back() { --used; }
  // Caller has already admitted capacity, including rollback reinsertion.
  void resize(size_t count) { used = count; }
  template <bool Const>
  class Iterator {
    using Owner = std::conditional_t<Const, const CheckedClippingStorage, CheckedClippingStorage>;
    Owner* owner = nullptr;
    size_t index = 0;

   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<Const, const T&, T&>;
    using pointer = std::conditional_t<Const, const T*, T*>;
    Iterator() = default;
    Iterator(Owner* o, size_t i) : owner(o), index(i) {}
    reference operator*() const { return (*owner)[index]; }
    pointer operator->() const { return &(*owner)[index]; }
    Iterator& operator++() {
      ++index;
      return *this;
    }
    Iterator operator++(int) {
      auto old = *this;
      ++*this;
      return old;
    }
    Iterator operator+(size_t n) const { return {owner, index + n}; }
    difference_type operator-(Iterator b) const { return static_cast<difference_type>(index) - b.index; }
    bool operator==(Iterator b) const { return owner == b.owner && index == b.index; }
    bool operator!=(Iterator b) const { return !(*this == b); }
  };
  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;
  iterator begin() { return {this, 0}; }
  iterator end() { return {this, used}; }
  const_iterator begin() const { return {this, 0}; }
  const_iterator end() const { return {this, used}; }
  void erase(iterator position) {
    size_t index = position - begin();
    for (size_t i = index + 1; i < used; ++i) (*this)[i - 1] = (*this)[i];
    --used;
  }
  void insert(iterator position, const T& value) {
    const size_t index = position - begin();
    for (size_t i = used; i > index; --i) (*this)[i] = (*this)[i - 1];
    (*this)[index] = value;
    ++used;
  }
  bool operator==(const CheckedClippingStorage& other) const {
    if (used != other.used) return false;
    for (size_t i = 0; i < used; ++i)
      if (!((*this)[i] == other[i])) return false;
    return true;
  }
  bool operator!=(const CheckedClippingStorage& other) const { return !(*this == other); }
};

struct CheckedClippingId {
  char bytes[65]{};
  CheckedClippingId() = default;
  explicit CheckedClippingId(const char* id) {
    size_t n = 0;
    while (n < sizeof(bytes) - 1 && id[n]) ++n;
    std::memcpy(bytes, id, n);
  }
  const char* c_str() const { return bytes; }
  const char* data() const { return bytes; }
  size_t size() const { return std::strlen(bytes); }
  bool empty() const { return !bytes[0]; }
  bool operator==(const char* other) const { return std::strcmp(bytes, other) == 0; }
  bool operator==(const CheckedClippingId& other) const { return *this == other.bytes; }
};
