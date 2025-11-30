# kernel-wrappers
A small header file containing RAII wrappers for the windows kernel.

> [!WARNING]
> This library is NOT production ready, feel free to create PRs that would make it stable and cleaner.

## Usage
First, you have to override placement new/delete. It's fairly easy, and the signature should be likewise this:
```cpp
void* __cdecl operator new(size_t size, const bool& paged, ULONG tag = detail::DRIVER_TAG);
void* __cdecl operator new[](size_t size, const bool& paged, ULONG tag = detail::DRIVER_TAG);

void __cdecl operator delete(void* ptr) noexcept;
void __cdecl operator delete[](void* ptr) noexcept;

void __cdecl operator delete(void* ptr, size_t size) noexcept;
void __cdecl operator delete[](void* ptr, size_t size) noexcept;
```

Then, include Safe.h on your project, and you're ready to go.

## Examples
### global-spinlock
```cpp
//
// Define it

static SpinLock* g_lock;

//
// Initialize it
g_lock = new (true) SpinLock(); // Using true, this lock won't be available on high IRQL.

//
// Use it
ScopedLock lock(detail::g_lock);
```
