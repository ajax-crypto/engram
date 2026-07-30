# engram

A C++20 library that provides a unified arena-allocation API over a
variety of memory targets — stack, heap, external buffers, and GPU / accelerator
device memory. It ships in **two interchangeable layouts** — a drop-in single
header and a header + source (`.h`/`.cpp`) pair — see
[Choosing a layout](#choosing-a-layout-single-header-vs-header--source).

## TL;DR

Grab one block of memory from wherever you need it — the stack, the heap, a
buffer you already own, or GPU / accelerator device memory — then `push` objects
and arrays into it and let the whole thing free at once when the `arena` dies.

```cpp
#include "engram.h"
using namespace engram;

auto a = arena::heap(1 << 20, /*trueContiguous*/ false);   // 1 MiB heap arena

auto& n     = a.push<int>(42);              // one object
auto  data  = a.push_array<float>(1024);    // std::span<float>, 1024 elts
auto  hello = a.push_string("hi");          // std::string_view

// ...use them...

a.pop_string(hello);                        // LIFO unwind (or just let `a` die)
```

With `engram` you can:

- **Bump-allocate** objects, arrays, and strings into a single owned block, with
  optional LIFO `pop`, and hand results back as `std::span` / `std::string_view`.
- **Target many memory sources** through one API: stack (`alloca` with heap
  fallback), heap (aligned / page-aligned / huge-page / shared), an
  **external** buffer you already own, or a **custom** backend.
- **Allocate device / accelerator memory** — Vulkan, DirectX 12, Metal, CUDA,
  ROCm, OpenCL, SYCL, Level Zero, WebGPU, XDNA, DPDK, OP-TEE, PMDK, RDMA,
  GPUDirect, and Linux dma-buf — behind the exact same `push`/`pop` interface.
- **Drop into standard code** via a `std::pmr::monotonic_buffer_resource` view,
  **prefetch / warm** ranges into cache or device, and pin pages into RAM.

See the [Overview](#overview) for the full story.

## Overview

`engram` centers on the `engram::arena` type: a move-only, bump-pointer allocator
that owns a block of memory obtained from one of several backends. Instead of
scattering `new`/`delete`, `alloca`, `cudaMalloc`, and friends throughout your
code, you create an `arena` for the target you want and then `push` objects and
arrays into it. The arena reclaims everything when it is destroyed.

Everything is exposed through the `engram` namespace. Both layouts present the
identical `arena` API, so the include is all that differs:

```cpp
#include "engram.h"     // single_header/engram.h  — or  src/engram.h
using namespace engram;
```

`engram` requires **C++20** (it uses `std::span`, `if constexpr`, fold
expressions, and `std::align_val_t`).

## Choosing a layout: single header vs. header + source

engram is distributed in two forms that share the same `engram::arena` API:

|                     | **Single header** (`single_header/engram.h`)                     | **Header + source** (`src/engram.h` + `src/engram.cpp`)                              |
| ------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| Integration         | Copy one header and `#include` it — no build step                | Add `engram.h` to your includes **and** compile/link `engram.cpp`                   |
| Vendor SDK headers  | Pulled into **every** TU that enables a backend                  | Confined to `engram.cpp`; users of `engram.h` need no vendor SDK on their include path |
| Compile time        | Every including TU recompiles the whole implementation           | Implementation compiled once; consuming TUs stay lightweight                        |
| Encapsulation       | Internals visible in the header                                  | Internals hidden behind the opaque pointer; changing them doesn't force a rebuild    |
| Runtime cost        | State stored inline in `arena` — no indirection, no extra alloc  | One heap allocation for `impl_data` per arena + a pointer hop to reach state         |
| Creation API        | Typed `arena::create_vulkan/_dx12/…` factories **and** `create_custom` | Single `arena::create_custom(size, custom, flags, …)` enum dispatch            |

**Use the single header** for quick integration, small projects, or when you want
zero build configuration and don't mind the extra per-TU compile cost.

**Use the header + source** for larger codebases: faster incremental builds, a
clean public header that doesn't drag vendor SDK headers (CUDA, Vulkan, DX12, …)
into your translation units, and the freedom to change the implementation without
recompiling everything that includes it.

The bundled CMake exposes `ENGRAM_SINGLE_HEADER` (default `ON`) to switch between
them — see [Building](#building).

## Memory Targets

Targets are described by `memory_source`:

| Source                     | Backing                                                |
| -------------------------- | ------------------------------------------------------ |
| `memory_source::stack`     | `alloca` / `_alloca`, with optional heap fallback      |
| `memory_source::heap`      | aligned `new`, or OS primitives for contiguous/shared pages |
| `memory_source::external`  | A user-supplied buffer the arena does not own          |
| `memory_source::custom`    | Any user allocator, including the device backends below |

Optional accelerator/device backends are compiled in via preprocessor switches
(see [Optional Backends](#optional-backends)):
Vulkan, DirectX 12, Metal, CUDA, ROCm/HIP, OpenCL, SYCL, oneAPI Level Zero,
WebGPU, XDNA (AMD AIE), DPDK, OP-TEE (ARM TrustZone), PMDK (persistent memory),
RDMA, CUDA GPUDirect Storage, and Linux dma-buf heaps. Their implementations live
in the nested `engram::vendor` namespace.

## Creating an Arena

The `arena` class exposes static factory functions:

```cpp
// Generic factory.
arena arena::create(memory_source type, std::size_t size, int32_t flags = 0,
                    std::size_t alignment = alignof(std::max_align_t));

// Convenience factories.
arena arena::stack(std::size_t size, bool fallbackToHeap);
arena arena::heap(std::size_t size, bool trueContiguous,
                  std::size_t alignment = alignof(std::max_align_t), int fd = -1);

// Wrap memory you already own (arena will not free it).
template <typename T>
arena arena::adopt(T* storage, std::size_t size, int32_t flags = 0);

// Select a vendor/device backend with the `custom` enum; backend-specific
// parameters follow `flags` as variadic arguments (see the table below).
arena arena::create_custom(std::size_t size, custom type, int32_t flags, ...);
```

The variadic arguments after `flags` depend on the selected `custom` backend:

| `custom` backend            | Variadic parameters after `flags`                                                        |
| --------------------------- | ---------------------------------------------------------------------------------------- |
| `CUDA`, `ROCm`, `GPUDirect` | *(none)*                                                                                  |
| `Vulkan`                    | `VkDevice, VkPhysicalDevice, const VkAllocationCallbacks*, VkDeviceSize offset, VkMemoryMapFlags` |
| `DX12`                      | `ID3D12Device*, D3D12_RESOURCE_FLAGS, D3D12_RESOURCE_STATES`                              |
| `OpenCL`                    | `cl_context, cl_svm_mem_flags, cl_uint alignment`                                        |
| `SYCL`                      | `sycl::queue*`                                                                            |
| `LevelZero`                 | `ze_context_handle_t, ze_device_handle_t`                                                 |
| `WebGPU`                    | `WGPUDevice`                                                                              |
| `XDNA`                      | `xrtDeviceHandle, xrtBufferFlags, xrtMemoryGroup`                                         |
| `DPDK`                      | `const char* name, int align, int socketId, unsigned int dpdkFlags, int useVirtAddr`     |
| `PMDK`                      | `const char* path, int pmdk_flags, mode_t mode`                                           |
| `RDMA`                      | `void* buffer, ibv_pd*, int access`                                                       |
| `OpTee`                     | `uint32_t hint`                                                                           |
| `Metal`                     | `MTL::Device*` (or an Objective-C `id` device)                                            || `DmaBuf`                    | `int deviceFd` (Linux; `deviceFd < 0` opens `/dev/dma_heap/system`)                        |
The optional `alignment` argument on `create`/`heap` controls the alignment of
the underlying heap allocation (via aligned `operator new`). When
`flags::page_aligned` is set, the effective alignment is raised to the page size
if that is larger.

The optional `fd` argument on `heap` (Linux) supplies a file descriptor — e.g. a
`memfd` or `dma-buf` — to `mmap` instead of allocating fresh memory; passing an
`fd` implies `flags::unified`. With `flags::unified` and no `fd`, the heap backend
creates its own `memfd` so the buffer is shareable / exportable.

### Flags

Behavior is tuned with the `int32_t` constants in the `engram::flags` namespace
(bitwise OR-able):

| Flag                     | Effect                                              |
| ------------------------ | --------------------------------------------------- |
| `flags::none`            | No special behavior.                                |
| `flags::heap_fallback`   | Fall back to a heap allocation if the target fails. |
| `flags::true_contiguous` | Request physically contiguous / huge-page memory.   |
| `flags::page_aligned`    | Round the size up to a page boundary.               |
| `flags::commit`          | Zero-initialize (commit) the memory on allocation.  |
| `flags::shared`          | Request shareable memory.                           |
| `flags::no_clear`        | Don't zero the memory when the arena is freed.      |
| `flags::pin_to_physical` | Lock the pages into RAM (`mlock` / `VirtualLock`).  |
| `flags::unified`         | Unified / shareable memory: `memfd` on the Linux heap; managed memory (`cudaMallocManaged` / `hipMallocManaged`) for CUDA / ROCm. |

By default an arena scrubs (zeroes) its memory when it is destroyed; pass
`flags::no_clear` to skip that.

## Using an Arena

`arena` is a bump allocator, so `push`/`pop` behave like a **stack (LIFO)**: each
`push`/`push_array` advances the offset, and each `pop`/`pop_array` must unwind
the **most recent** allocation. You must pop in the exact reverse order of your
pushes — popping out of order corrupts the arena offset and is undefined
behavior.

```cpp
auto a = arena::stack(1 << 12, /*fallbackToHeap=*/true);

// Push in order: (1) then (2).
int&             i  = a.push<int>(42);           // (1) construct a single int
std::span<float> xs = a.push_array<float>(256);  // (2) construct an array

// Pop in reverse order: (2) before (1).
a.pop_array<256, float>();                        // undo (2) first
a.pop<int>();                                     // then undo (1)

// WRONG: a.pop<int>() here (before the array) would corrupt the arena — UB.
```

Strings have dedicated helpers that null-terminate for you and hand back a
`std::string_view` (they follow the same LIFO discipline):

```cpp
auto sv = a.push_string("hello");   // copies "hello\0", returns a view
a.pop_string(sv);                   // pop it back off
```

`pop_array` comes in several forms — pass the element count, a `std::span`, or a
compile-time size:

```cpp
a.pop_array<float>(256);            // runtime element count
a.pop_array(xs);                    // deduce the count from a std::span
a.pop_array<256, float>();          // compile-time count
```

Define `ENGRAM_EASY_POP` and the arena remembers each array/string size (up to
`ENGRAM_MAX_STRING_STACKSZ`, default 64), so you can pop without repeating it:

```cpp
a.pop_array<float>();               // size recalled automatically
```

Introspection helpers:

```cpp
a.is_valid();   // true if the arena holds memory
a.empty();      // true if nothing has been pushed yet
a.used();       // bytes currently allocated
a.capacity();   // total bytes available
a.remaining();  // bytes still free
a.count();      // live allocation count
a.total();      // lifetime allocation count
a.source();     // the memory_source backing this arena
a.data();       // std::span<std::byte> over the whole storage [base, capacity)
a.unpin();      // undo flags::pin_to_physical (munlock / VirtualUnlock)
```

`arena` is **move-only**: the copy constructor and copy assignment are deleted,
so ownership of the underlying memory is never accidentally duplicated.

### Partitioning (sub-arenas)

`partition(start, size, flags)` carves a fixed region of an arena into an
independent **sub-arena** you can hand to a child function or worker thread. The
sub-arena is a non-owning `memory_source::external` view of `[start, start +
size)`; destroying it leaves the parent (and its memory) untouched, so no extra
allocation or ownership transfer is involved. The region is zeroed by default;
pass `flags::no_clear` to skip that. You pick the (non-overlapping) regions.

```cpp
auto pool = arena::heap(4 << 20);              // 4 MiB backing block

// Split the pool into per-worker slices and run them in parallel.
constexpr std::size_t workers = 4;
const std::size_t slice = pool.capacity() / workers;

std::vector<std::thread> threads;
for (std::size_t i = 0; i < workers; ++i)
{
    engram::arena sub = pool.partition(i * slice, slice);   // slice is zeroed
    threads.emplace_back([sub = std::move(sub)]() mutable {
        auto scratch = sub.push_array<float>(256);   // each thread bump-allocates
        // ... work only within this thread's slice ...
    });
}
for (auto& t : threads) t.join();
```

Because each sub-arena owns a disjoint region, the threads never touch the same
bytes and need no locking. The same pattern hands a scratch slice to a callee:

```cpp
void render(engram::arena scratch);            // takes its own sub-arena

auto frame = arena::heap(1 << 20);
render(frame.partition(0, 64 * 1024));         // give the callee a 64 KiB slice
```

> The parent arena must outlive every sub-arena carved from it, since the
> sub-arena points into the parent's memory.

### PMR Integration

Unless `ENGRAM_DISABLE_PMR` is defined, an arena can expose its storage as a
`std::pmr::monotonic_buffer_resource`, so it can back standard PMR containers:

```cpp
auto a = arena::heap(1 << 20, /*trueContiguous=*/false);
std::pmr::vector<int> v{ &a.get_pmr_resource() };

// Or restrict the resource to a sub-range [start, start + size):
auto& r = a.get_pmr_resource(/*start=*/0, /*size=*/4096);
```

### Synchronization & Prefetch

Device-backed arenas expose two C-style variadic helpers, `sync(...)` and
`prefetch(...)`, that both return `bool` (`true` on success). They dispatch on
the owning backend and pull the backend-specific arguments from a `va_list`, so
each vendor takes exactly what it needs:

| Backend   | `sync(...)`                                | `prefetch(...)`                          |
| --------- | ------------------------------------------ | ---------------------------------------- |
| CUDA      | `sync()` — `cudaDeviceSynchronize` *(managed)* | `prefetch(start, end, device)` *(managed)* |
| ROCm      | `sync()` — `hipDeviceSynchronize` *(managed)*  | `prefetch(start, end, device)` *(managed)* |
| SYCL      | `sync()` — `queue.wait()`                  | `prefetch(start, end)`                   |
| OpenCL    | `sync(cl_command_queue)` — `clFinish`      | `prefetch(start, end, cl_command_queue)` — `clEnqueueSVMMigrateMem` |
| XDNA      | `sync(bool host_to_device)` — `xrtBOSync`  | —                                        |
| PMDK      | `sync(start, end)` — persist the range     | —                                        |
| RDMA      | —                                          | `prefetch(start, end)` — `ibv_advise_mr` |
| GPUDirect | `sync()` — `cudaDeviceSynchronize`         | —                                        |
| Heap      | —                                          | `prefetch(start, size)` — `madvise` / `PrefetchVirtualMemory` |

```cpp
auto a = arena::create_cuda(1 << 20, flags::unified);   // managed memory
if (!a.prefetch(0, a.used(), 0)) { /* handle error */ } // prefetch to device 0
a.sync();                                                // wait for it
```

CUDA/ROCm `sync`/`prefetch` only act on **managed** allocations (created with
`flags::unified`). Because arguments travel through a `va_list`, they are **not**
type-checked — pass exactly what the owning backend expects, and use
`std::size_t`-typed values for the offset slots.

### Cache & Page Prefetch (any memory)

Beyond the arena methods, engram provides two **free functions** in the `engram`
namespace that operate on *any* pointer — a plain heap allocation, an arena's
storage, or memory you got elsewhere:

```cpp
// Emit a CPU prefetch hint for the cache line(s) at ptr.
template <typename T>
void engram::warm_cache(T* ptr, cache_locality locality, int32_t ioflags);

// Fault a [ptr, ptr + size) range into RAM / the page cache. Returns true on success.
template <typename T>
bool engram::prefetch(T* ptr, std::size_t size);
```

`warm_cache` lowers to a hardware prefetch instruction (`_mm_prefetch` on x86,
`__prefetch` on ARM/MSVC, `__builtin_prefetch` on GCC/Clang). The `locality`
argument (`enum class cache_locality`) selects the target cache level, and
`ioflags` hints the access intent with `flags::read` / `flags::write`:

| `cache_locality` | Meaning                                       |
| ---------------- | --------------------------------------------- |
| `L1`             | Keep in L1 (highest temporal locality).       |
| `L2`             | Keep in L2.                                    |
| `L3`             | Keep in L3.                                    |
| `Discard`        | Non-temporal / streaming (don't pollute cache). |

`prefetch` pages a whole range in: `madvise(MADV_WILLNEED)` on Linux,
`PrefetchVirtualMemory` on Windows, a page-touch loop on other Unix (and it also
issues a CPU prefetch hint).

```cpp
std::byte* buf = new std::byte[1 << 20];
engram::prefetch(buf, 1 << 20);                            // page the range in
engram::warm_cache(buf, cache_locality::L1, flags::read);  // pull the head into L1
```

The `arena` mirrors these as members: `a.warm_cache(cache_locality, ioflags,
start = 0, size = 0)` warms the arena's own bytes, and — as shown in the table
above — the variadic `a.prefetch(start, size)` on a plain **heap** arena forwards
to `engram::prefetch` (`madvise` / `PrefetchVirtualMemory`).

### Error Handling

Failures are reported through the `m_error` field using `arena_error`
(`no_error`, `stack_overflow`, `alloc_failed`, `custom`) rather than exceptions.

## Optional Backends

Device and accelerator backends are opt-in. Define the corresponding macro
before including the header (or define `ENGRAM_ALL` to enable every backend
available on the current platform). DirectX 12 is enabled automatically on
Windows (`_WIN32`), and Metal on Apple platforms (`__APPLE__`) — see
[Metal (Apple)](#metal-apple) below.

| Macro                    | Backend                | 
| ------------------------ | ---------------------- |
| `ENGRAM_ENABLE_VULKAN`   | Vulkan device memory   |
| `ENGRAM_ENABLE_DX12`     | DirectX 12 (Windows)   |
| `ENGRAM_ENABLE_CUDA`     | NVIDIA CUDA            |
| `ENGRAM_ENABLE_ROCM`     | AMD ROCm / HIP         |
| `ENGRAM_ENABLE_OPENCL`   | OpenCL SVM             |
| `ENGRAM_ENABLE_SYCL`     | SYCL USM (shared)      |
| `ENGRAM_ENABLE_LEVEL_ZERO`| oneAPI Level Zero USM |
| `ENGRAM_ENABLE_WEBGPU`   | WebGPU mapped buffer   |
| `ENGRAM_ENABLE_XDNA`     | AMD XDNA (XRT)         |
| `ENGRAM_ENABLE_DPDK`     | DPDK memory zones      |
| `ENGRAM_ENABLE_OP_TEE`   | OP-TEE / ARM TrustZone |
| `ENGRAM_ENABLE_PMDK`     | PMDK persistent memory | 
| `ENGRAM_ENABLE_RDMA`     | RDMA-registered memory |
| `ENGRAM_ENABLE_GPUDIRECT`| CUDA GPUDirect Storage |
| `ENGRAM_ENABLE_DMABUF`   | Linux dma-buf heap     |

Example:

```cpp
#define ENGRAM_ENABLE_CUDA
#include "engram.h"
using namespace engram;

auto device_arena = arena::create_cuda(1 << 20);
std::span<float> data = device_arena.push_array<float>(1024);
```

### Metal (Apple)

On Apple platforms the Metal backend is always available (no `ENGRAM_ENABLE_*`
switch needed) and is reached through `arena::create_metal`:

```cpp
auto gpu = arena::create_metal(1 << 20, device);   // device: MTL::Device*
std::span<float> data = gpu.push_array<float>(1024);
```

Metal buffers are allocated with shared storage, so the buffer's CPU-visible
`contents()` pointer becomes the arena's base — you can `push` into it directly.

There are two ways to talk to Metal, selected at compile time:

- **Objective-C runtime (default).** With no extra macros the backend drives
  Metal through `<objc/runtime.h>` / `objc_msgSend`, so it pulls in no
  third-party headers. It creates a private `MTLHeap` plus a shared buffer.
- **metal-cpp (`ENGRAM_METAL_CPP`).** Define this to use Apple's
  [metal-cpp](https://developer.apple.com/metal/cpp/) C++ bindings instead. It
  includes `<Foundation/Foundation.hpp>`, `<Metal/Metal.hpp>`, and
  `<QuartzCore/QuartzCore.hpp>`, and the API works with `MTL::Device*` /
  `MTL::Buffer*`.

#### metal-cpp private implementation

metal-cpp is header-only but requires **exactly one** translation unit to emit
its out-of-line implementation. In that single TU, define
`ENGRAM_METAL_PRIVATE_IMPL` (alongside `ENGRAM_METAL_CPP`) before including
`engram.h` — it expands to metal-cpp's `NS_PRIVATE_IMPLEMENTATION`,
`CA_PRIVATE_IMPLEMENTATION`, and `MTL_PRIVATE_IMPLEMENTATION` macros:

```cpp
// exactly one .cpp / .mm in your program:
#define ENGRAM_METAL_CPP
#define ENGRAM_METAL_PRIVATE_IMPL
#include "engram.h"
```

Every other TU should define only `ENGRAM_METAL_CPP` (or nothing, to use the
Objective-C runtime path). Defining `ENGRAM_METAL_PRIVATE_IMPL` in more than one
TU causes duplicate-symbol link errors.

### OP-TEE (ARM TrustZone)

For Trusted Applications running inside an
[OP-TEE](https://optee.readthedocs.io/) secure world, enable
`ENGRAM_ENABLE_OP_TEE` to allocate from the TEE's internal heap via `TEE_Malloc`
/ `TEE_Free`:

```cpp
#define ENGRAM_ENABLE_OP_TEE
#include "engram.h"
using namespace engram;

auto a = arena::create_op_tee(4096);                       // hint: TEE_MALLOC_FILL_ZERO
auto b = arena::create_op_tee(4096, TEE_MALLOC_NO_FILL);   // custom hint
```

The `hint` argument is forwarded straight to `TEE_Malloc` (defaulting to
`TEE_MALLOC_FILL_ZERO`). `arena::create_arm_trustzone` is provided as an alias
for the same allocator.

### Persistent Memory (PMDK)

Enable `ENGRAM_ENABLE_PMDK` to back an arena with a persistent-memory file via
`libpmem` (`pmem_map_file` / `pmem_unmap`):

```cpp
auto pm = arena::create_pmdk(1 << 20, "/mnt/pmem/engram.pool");
auto& x = pm.push<int>(42);          // written into persistent memory
pm.sync(0, pm.used());               // persist the range (pmem_persist / msync)
```

Signature: `create_pmdk(size, path, pmdk_flags = PMEM_FILE_CREATE, mode = 0666,
flags)`. Use `sync(start, end)` to make writes durable.

### RDMA

Enable `ENGRAM_ENABLE_RDMA` to register a **caller-owned** buffer with an RDMA
device (`ibv_reg_mr`). engram manages the registration lifetime, not the buffer
itself:

```cpp
void* buffer = /* a host buffer you own */;
auto r = arena::create_rdma(size, buffer, pd,
             IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
auto* mr = engram::vendor::get_rdma_mr(r);   // mr->lkey / mr->rkey for work requests
r.prefetch(0, r.used());                     // ibv_advise_mr (ODP prefetch)
```

Signature: `create_rdma(size, void* buffer, ibv_pd* pd, access =
IBV_ACCESS_LOCAL_WRITE, flags)`.

### CUDA GPUDirect Storage

Enable `ENGRAM_ENABLE_GPUDIRECT` to allocate GPU memory (`cudaMalloc`) and
register it for direct storage I/O with cuFile (`cuFileBufRegister`), so files
can be read/written straight into GPU memory:

```cpp
auto g = arena::create_gpudirect(1 << 20);   // GPU buffer registered with cuFile
// ... cuFileRead(handle, gpu_ptr, size, ...) directly into GPU memory ...
g.sync();                                     // cudaDeviceSynchronize
```

This backend pulls in both `<cuda_runtime.h>` and `<cufile.h>`.

### Linux dma-buf

Enable `ENGRAM_ENABLE_DMABUF` (Linux only) to allocate a dma-buf from a kernel
dma-buf heap. engram issues `DMA_HEAP_IOCTL_ALLOC` on the heap device to obtain a
dma-buf fd, then `mmap`s it to get the CPU pointer:

```cpp
auto a = arena::create_dmabuf(1 << 20);           // opens /dev/dma_heap/system
std::span<float> data = a.push_array<float>(1024);

int heap_fd = open("/dev/dma_heap/cma", O_RDWR | O_CLOEXEC);
auto b = arena::create_dmabuf(1 << 20, heap_fd);  // use a caller-supplied heap fd
```

Signature: `create_dmabuf(size, int deviceFd = -1, flags)`. When `deviceFd` is
negative engram opens `/dev/dma_heap/system` (and closes it after the allocation);
otherwise the caller-supplied heap fd is used and left open. On destruction the
arena unmaps the buffer and closes the dma-buf fd. This backend pulls in
`<linux/dma-heap.h>`, `<sys/ioctl.h>`, and `<fcntl.h>`.

### Overriding Backend Headers

Each backend includes its SDK header by default, but you can point it at a custom
path by defining the matching macro before including `engram.h`:

| Macro                       | Default header                    |
| --------------------------- | --------------------------------- |
| `ENGRAM_VULKAN_HEADER`      | `<vulkan/vulkan.h>`               |
| `ENGRAM_CUDA_HEADER`        | `<cuda_runtime.h>`                |
| `ENGRAM_ROCM_HEADER`        | `<hip/hip_runtime.h>`             |
| `ENGRAM_OPENCL_HEADER`      | `<CL/opencl.h>`                   |
| `ENGRAM_XRT_DEVICE_HEADER`  | `<experimental/xrt_device.h>`     |
| `ENGRAM_XRT_BO_HEADER`      | `<experimental/xrt_bo.h>`         |
| `ENGRAM_DPDK_MALLOC_HEADER` | `<rte_malloc.h>`                  |
| `ENGRAM_DPDK_MEMZONE_HEADER`| `<rte_memzone.h>`                 |
| `ENGRAM_OP_TEE_HEADER`      | `<tee_internal_api.h>`            |
| `ENGRAM_SYCL_HEADER`        | `<sycl/sycl.hpp>`                 |
| `ENGRAM_LEVEL_ZERO_HEADER`  | `<level_zero/ze_api.h>`           |
| `ENGRAM_WEBGPU_HEADER`      | `<webgpu/webgpu.h>`               |
| `ENGRAM_PMDK_HEADER`        | `<libpmem.h>`                     |
| `ENGRAM_RDMA_HEADER`        | `<infiniband/verbs.h>`            |
| `ENGRAM_GPUDIRECT_HEADER`   | `<cufile.h>`                      |
| `ENGRAM_DMABUF_HEADER`      | `<linux/dma-heap.h>`              |

(DirectX 12 uses fixed system headers and is not overridable. GPUDirect also
includes `<cuda_runtime.h>` unconditionally.)

## Building

Pick a layout (see [Choosing a layout](#choosing-a-layout-single-header-vs-header--source)):

- **Single header** — add `single_header/engram.h` to your include path and
  `#include` it. No build step.
- **Header + source** — add `src/` to your includes and compile `src/engram.cpp`
  into your target (or link the library the bundled `CMakeLists.txt` builds).

The provided `CMakeLists.txt` exposes `ENGRAM_SINGLE_HEADER` (default `ON`) to
switch between the two, plus an `ENGRAM_ENABLE_*` option per backend. Either way,
enable optional backends by defining the relevant `ENGRAM_ENABLE_*` macro and
linking against that backend's SDK (CUDA, HIP, Vulkan, OpenCL, SYCL, Level Zero,
WebGPU, DPDK, PMDK `libpmem`, RDMA `libibverbs`, GPUDirect `cufile`, etc.). For
the header + source layout, define those macros for the `engram.cpp` build too.

On Apple platforms, link the `Metal`, `Foundation`, and `QuartzCore` frameworks.
When using metal-cpp (`ENGRAM_METAL_CPP`), define `ENGRAM_METAL_PRIVATE_IMPL` in
exactly one translation unit (see [Metal (Apple)](#metal-apple)).

### Large / huge pages (Windows)

`flags::true_contiguous` on Windows requests large-page memory
(`MEM_LARGE_PAGES` via `VirtualAlloc2`), which requires the **"Lock pages in
memory"** user right (`SeLockMemoryPrivilege`). Grant it once:

1. Run **`gpedit.msc`** (Local Group Policy Editor) — or **`secpol.msc`** (Local
   Security Policy) on Windows editions that don't ship `gpedit`.
2. Go to **Computer Configuration → Windows Settings → Security Settings →
   Local Policies → User Rights Assignment**.
3. Open **"Lock pages in memory"**, click **Add User or Group…**, and add the
   account (or group) that will run the process.
4. **Log off and back on** (or reboot) for the assignment to take effect.

Additional runtime requirements:

- The process must run **elevated** (as Administrator). engram enables the
  privilege in the process token at runtime via `AdjustTokenPrivileges`; the
  policy right above is what makes that succeed.
- Large-page allocations must be a multiple of `GetLargePageMinimum()` (commonly
  2 MB), so size requests are rounded up accordingly by the OS.

If the right is not granted (or the process isn't elevated), large-page
allocation fails and the arena reports `arena_error::alloc_failed`. On Linux the
equivalent path uses `MAP_HUGETLB`, which instead depends on hugepages being
reserved (e.g. `vm.nr_hugepages`) and does not need a policy change.

### Locking pages into RAM

`flags::pin_to_physical` locks pages via `VirtualLock` (Windows) or `mlock`
(Linux). On Linux this is bounded by the `RLIMIT_MEMLOCK` ulimit; on Windows it
counts against the process working-set minimum. Call `arena::unpin()` to release
the lock.

## Python bindings

A [pybind11](https://github.com/pybind/pybind11) wrapper for engram's public
`arena` API lives in [`bindings/python`](bindings/python). Because the C++
`push`/`pop` helpers are templates, they are surfaced as byte-oriented operations
that return `memoryview`s aliasing the arena's storage.

Build and install (needs a C++20 compiler; `pip` pulls in pybind11 and
scikit-build-core):

```bash
cd bindings/python
pip install .
```

```python
import engram

a = engram.Arena.heap(1 << 20)                  # 1 MiB heap arena

mv = a.alloc(1024)                              # writable memoryview (1024 bytes)
mv[:5] = b"hello"
a.push_bytes(b"world")                          # copy a bytes-like object in
s = a.push_str("greetings")                     # NUL-terminated string

print(a.used, a.capacity, a.remaining, a.source)

# adopt a buffer you already own (the arena will not free it)
buf = bytearray(4096)
b = engram.Arena.adopt(buf, engram.Flag.COMMIT)

# cache / page warm-up helpers work on any buffer
engram.warm_cache(mv, engram.CacheLocality.L1, engram.IO.READ)
engram.prefetch(buf)
```

What's exposed: the `Arena` factories (`create`, `stack`, `heap`, `adopt`),
byte allocation (`alloc`, `push_bytes`, `push_str`, `pop_bytes`, `partition`),
introspection
(`used`, `capacity`, `remaining`, `count`, `total`, `source`, `data`, `is_valid`,
`empty`), `prefetch` / `warm_cache` / `sync` / `unpin`, the `MemorySource`,
`CacheLocality`, and `ArenaError` enums, and `Flag` / `IO` flag sets.

Device/accelerator backends (CUDA, Vulkan, DirectX 12, …) are **not** exposed to
Python — they need native device handles; use the C++ API for those. Returned
`memoryview`s stay valid only until you `pop` past them or drop the arena.

## License

Distributed under the terms of the [MIT License](LICENSE).
