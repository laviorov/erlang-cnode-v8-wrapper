#ifndef ATOM_WRAPPER
#define ATOM_WRAPPER

#include <atomic>

template <typename T>
struct AtomWrapper
{
  std::atomic<T> _a;

  AtomWrapper()
    :_a()
  {}

  AtomWrapper(const std::atomic<T> &a)
    :_a(a.load())
  {}

  AtomWrapper(const AtomWrapper &other)
    :_a(other._a.load())
  {}

  AtomWrapper& operator= (const AtomWrapper &other)
  {
    this->_a.store(other._a.load());
    return *this;
  }

  template<typename W>
  AtomWrapper& operator = (const W& other) {
    this->a = other;
    return  *this;
  }

  template<typename W>
  AtomWrapper& operator += (const W& other) {
    this->_a += other;
    return *this;
  }

  operator T() const {
    return this->_a;
  }
};

#endif
