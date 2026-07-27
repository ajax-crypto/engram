# engram

A modern C++ library providing a unified API to allocate memory across a
variety of targets — stack, heap, GPU, and more.

## Overview

`engram` abstracts memory allocation behind a single, consistent interface so
that application code can request memory from different backends without being
coupled to the specifics of each one. Whether you need fast scratch space on
the stack, general-purpose heap storage, or device memory on a GPU, `engram`
exposes it through a common set of primitives.

## Goals

- **Unified API** — one allocation interface for many memory targets.
- **Pluggable backends** — stack, heap, GPU (and room to grow).
- **Zero-overhead abstractions** — pay only for what you use.
- **Modern C++** — leverages contemporary C++ features and idioms.

## Supported Targets

| Target | Status      | Description                                    |
| ------ | ----------- | ---------------------------------------------- |
| Stack  | Planned     | Fast, scope-local scratch allocations.         |
| Heap   | Planned     | General-purpose dynamic allocations.           |
| GPU    | Planned     | Device memory on supported accelerators.       |

## Example

```cpp
#include <engram/engram.h>

// Allocate memory from the heap target.
auto buffer = engram::allocate<engram::target::heap>(1024);

// Allocate memory on the GPU.
auto device_buffer = engram::allocate<engram::target::gpu>(1024);
```

> Note: The API shown above is illustrative and subject to change while the
> library is under active development.

## Building

_Build instructions will be added as the project takes shape._

## License

Distributed under the terms of the [MIT License](LICENSE).
