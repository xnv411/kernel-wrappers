#pragma once
#include <wdm.h>
#include <intrin.h>

using i8 = char;
using i16 = short;
using i32 = int;
using i64 = long long;

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;

namespace std {
  using byte = u8;
};

namespace detail {

template <typename T>
struct is_array { static constexpr bool value = false; };

template <typename T>
struct is_array<T[]> { static constexpr bool value = true; };

template <typename T, size_t N>
struct is_array<T[N]> { static constexpr bool value = true; };

template <typename T>
constexpr bool is_array_v = is_array<T>::value;

template <typename T>
struct remove_extent { using type = T; };

template <typename T>
struct remove_extent<T[]> { using type = T; };

template <typename T, size_t N>
struct remove_extent<T[N]> { using type = T; };

template <typename T>
using remove_extent_t = typename remove_extent<T>::type;

template <typename T, unsigned int N = 0>
struct extent { static constexpr size_t value = 0; };

template <typename T, size_t N>
struct extent<T[N], 0> { static constexpr size_t value = N; };

template <typename T, unsigned int N = 0>
constexpr size_t extent_v = extent<T, N>::value;

class NoCopy {
public:
  NoCopy() = default;
  NoCopy(const NoCopy&) = delete;
  NoCopy& operator=(const NoCopy&) = delete;
}; // class NoCopy

class NoMove {
public:
  NoMove() = default;
  NoMove(NoMove&&) = delete;
  NoMove& operator=(NoMove&&) = delete;
}; // class NoMove

template <typename T>
void swap(T& a, T& b) noexcept {
  T temp = a;
  a = b;
  b = temp;
}

}; // namespace detail

class Lock {
public:
  virtual void lock()   = 0;
  virtual void unlock() = 0;
};

class SpinLock : 
  public Lock,
  public detail::NoCopy,
  public detail::NoMove
{
private:
  i64 ref_count;

public:
  SpinLock() : ref_count{ 0 } {}

public:
  i64  ref()    const { 
    return _InterlockedExchangeAdd64((LONG64*)&this->ref_count, 0);
  }
  void lock()   override { _InterlockedDecrement64(&this->ref_count); }
  void unlock() override { _InterlockedIncrement64(&this->ref_count); }
};

class ScopedLock : 
  public detail::NoCopy,
  public detail::NoMove
{
private:
  Lock* lock;

public:
  ScopedLock(Lock* lock) {
    this->lock = lock;
    this->lock->lock();
  }

  ~ScopedLock() {
    if (this->lock) {
      this->lock->unlock();
    }
  }
};

template <
  typename T
>
class UniquePtr :
  public detail::NoCopy,
  public detail::NoMove
{
private:
  using ElementType = 
    detail::remove_extent_t<T>;

  ElementType* data;

public:
  UniquePtr(const bool&& paged) {
    constexpr size_t Size = sizeof(ElementType) * detail::extent_v<T>;

    if constexpr (detail::is_array_v<T>) {
      this->data = new (Size, paged) ElementType;
    }
    else {
      this->data = new (sizeof(T), paged) T();
    }
  }

  UniquePtr(const size_t&& size, const bool&& paged) {
    this->data = new (size, paged) T;
  }

  ~UniquePtr() {
    if constexpr (detail::is_array_v<T>) {
      delete[] this->data;
    }
    else {
      delete this->data;
    }
  }

public:
  ElementType* ptr() {
    return this->data;
  }

  ElementType* operator->() {
    return this->data;
  }

  ElementType& operator*() const {
    return *this->data;
  }
};

namespace detail {

template <
  typename T
>
class SharedPtrState {
private:
  using ElementType =
    detail::remove_extent_t<T>;

  i64 ref_count;
  ElementType* data;

public:
  SharedPtrState(const bool& paged) :
    ref_count{ 1 }
  {
    constexpr size_t Size = sizeof(ElementType) * detail::extent_v<T>;

    if constexpr (detail::is_array_v<T>) {
      this->data = new (Size, paged) ElementType;
    }
    else {
      this->data = new (sizeof(T), paged) T();
    }
  }

  SharedPtrState(const size_t& size, const bool& paged) :
    ref_count{ 1 }
  {
    this->data = new (size, paged) T;
  }

  ~SharedPtrState() {
    if (_InterlockedDecrement64(&this->ref_count)) {
      return;
    }

    if constexpr (detail::is_array_v<T>) {
      delete[] this->data;
    }
    else {
      delete this->data;
    }
  }
public:
  inline ElementType* ptr() const {
    return this->data;
  }

public:
  void ref() {
    _InterlockedIncrement64(&this->ref_count);
  }

  bool deref() {
    return _InterlockedDecrement64(&this->ref_count) == 0;
  }
}; // struct SharedPtrState

}; // namespace detail

template <
  typename T
>
class Atomic :
  public detail::NoCopy,
  public detail::NoMove
{
private:
  T* data;

public:
  Atomic() : 
    data{ nullptr }
  { }

  Atomic(T* data) :
    data{data}
  {}

public:
  void exchange(T* p) {
    _InterlockedCompareExchangePointer(
      (void**)&this->data, (void*)&this->data, p
    );
  }
  
public:
  inline T* load() {
    auto res = _InterlockedCompareExchangePointer(
      (void**)&this->data, (void*)&this->data, (void*)&this->data
    );

    return reinterpret_cast<T*>(res);
  }

  T* operator->() {
    return this->load();
  }
};

template <
  typename T
>
class SharedPtr {
private:
  using ElementType =
    detail::remove_extent_t<T>;

  using State = 
    detail::SharedPtrState<T>;
  
  State* state;

private:
  inline ElementType* get_data() const {
    return this->state->ptr();
  }

public:
  SharedPtr(const bool& paged) {
    this->state = new (sizeof(State), paged) State(paged);
  }

  SharedPtr(const size_t& size, const bool& paged) {
    this->state = new (size, paged) State(size, paged);
  }

  ~SharedPtr() {
    if (this->state == nullptr) {
      return;
    }

    if (this->state->deref()) {
      delete this->state;
    }
  };

  /* 
  * Copy/move constructors
  */

  SharedPtr(const SharedPtr& other) noexcept :
    state{ other.state }
  {
    if (this->state != nullptr) {
      this->state->ref();
    }
  }

  SharedPtr& operator=(const SharedPtr& other) noexcept {
    SharedPtr temp(other);
    
    detail::swap(
      this->state, temp.state);
    
    return *this;
  }

public:
  inline ElementType* ptr() {
    return this->get_data();
  }

  ElementType* operator->() {
    return this->get_data();
  }

  ElementType& operator*() const {
    return *this->get_data();
  }
};
