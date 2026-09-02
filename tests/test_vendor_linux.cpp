// Compile-only conformance for the Linux vendor backends.
//
// This translation unit is compiled but never linked and never run: the vendor
// entry points it names live in driver libraries that need real hardware, a kernel
// module, or root. What it does check is that engram's backend code still
// type-checks against the real SDK headers -- signatures, defaults, and includes --
// which is what silently rots when a vendor bumps its API.
//
// tests/CMakeLists.txt compiles it once per resolved backend as an OBJECT library,
// so a mismatch shows up as a build failure.
//
// The single header exposes per-vendor factories (arena::create_vulkan, ...); the
// header + source build only exposes the varargs arena::create_custom. Both paths
// are exercised so neither can drift unnoticed.
//
// Nothing here sits in an anonymous namespace: these functions are deliberately
// never called, and external linkage is what keeps -Wunused-function quiet.

#include "engram.h"

#if !defined(ENGRAM_ENABLE_VULKAN) && !defined(ENGRAM_ENABLE_WEBGPU) && \
    !defined(ENGRAM_ENABLE_PMDK) && !defined(ENGRAM_ENABLE_RDMA) && \
    !defined(ENGRAM_ENABLE_DMABUF)
#error "test_vendor_linux.cpp must be compiled with an ENGRAM_ENABLE_<backend> macro"
#endif

#ifndef __linux__
#error "test_vendor_linux.cpp covers the Linux-only vendor backends"
#endif

#include <cstddef>
#include <string_view>
#include <type_traits>

using engram::arena;

#ifdef ENGRAM_ENABLE_VULKAN

void engram_vulkan_conformance(VkDevice device, VkPhysicalDevice physical,
                               const VkAllocationCallbacks* cbs)
{
    auto viaCustom = arena::create_custom(std::size_t{ 1 } << 20, engram::custom::Vulkan,
                                          engram::flags::none, device, physical, cbs,
                                          VkDeviceSize{ 0 }, VkMemoryMapFlags{ 0 });
    static_assert(std::is_same_v<decltype(viaCustom), arena>);

#ifdef ENGRAM_SINGLE_HEADER_BUILD
    auto viaFactory = arena::create_vulkan(std::size_t{ 1 } << 20, device, physical, cbs,
                                           VkDeviceSize{ 0 }, VkMemoryMapFlags{ 0 },
                                           engram::flags::none);
    static_assert(std::is_same_v<decltype(viaFactory), arena>);

    // Everything after the physical device has to stay optional.
    auto viaDefaults = arena::create_vulkan(std::size_t{ 1 } << 20, device, physical);
    (void)viaDefaults.is_valid();
#endif

    (void)viaCustom.error();
    (void)viaCustom.sync();
    (void)viaCustom.data();
    auto& value = viaCustom.push<int>(1);
    (void)value;
}

#endif // ENGRAM_ENABLE_VULKAN

#ifdef ENGRAM_ENABLE_WEBGPU

void engram_webgpu_conformance(WGPUDevice device)
{
    auto viaCustom = arena::create_custom(std::size_t{ 1 } << 20, engram::custom::WebGPU,
                                          engram::flags::none, device);
    static_assert(std::is_same_v<decltype(viaCustom), arena>);

#ifdef ENGRAM_SINGLE_HEADER_BUILD
    auto viaFactory = arena::create_webgpu(std::size_t{ 1 } << 20, device, engram::flags::none);
    (void)viaFactory.is_valid();

    auto viaDefaults = arena::create_webgpu(std::size_t{ 1 } << 20, device);
    (void)viaDefaults.capacity();
#endif

    (void)viaCustom.error();
    auto span = viaCustom.push_array<float>(16);
    (void)span;
}

#endif // ENGRAM_ENABLE_WEBGPU

#ifdef ENGRAM_ENABLE_PMDK

void engram_pmdk_conformance(const char* path)
{
    auto viaCustom = arena::create_custom(std::size_t{ 1 } << 20, engram::custom::PMDK,
                                          engram::flags::none, path,
                                          static_cast<int>(PMEM_FILE_CREATE), mode_t{ 0666 });
    static_assert(std::is_same_v<decltype(viaCustom), arena>);

#ifdef ENGRAM_SINGLE_HEADER_BUILD
    auto viaFactory = arena::create_pmdk(std::size_t{ 1 } << 20, path,
                                         static_cast<int>(PMEM_FILE_CREATE),
                                         mode_t{ 0666 }, engram::flags::none);
    (void)viaFactory.is_valid();

    auto viaDefaults = arena::create_pmdk(std::size_t{ 1 } << 20, path);
    (void)viaDefaults.capacity();
#endif

    // PMDK is the backend whose sync persists an explicit range.
    (void)viaCustom.sync(std::size_t{ 0 }, std::size_t{ 4096 });
    (void)viaCustom.error();
}

#endif // ENGRAM_ENABLE_PMDK

#ifdef ENGRAM_ENABLE_RDMA

void engram_rdma_conformance(void* buffer, ibv_pd* pd)
{
    auto viaCustom = arena::create_custom(std::size_t{ 1 } << 20, engram::custom::RDMA,
                                          engram::flags::none, buffer, pd,
                                          static_cast<int>(IBV_ACCESS_LOCAL_WRITE));
    static_assert(std::is_same_v<decltype(viaCustom), arena>);

#ifdef ENGRAM_SINGLE_HEADER_BUILD
    auto viaFactory = arena::create_rdma(std::size_t{ 1 } << 20, buffer, pd,
                                         static_cast<int>(IBV_ACCESS_LOCAL_WRITE),
                                         engram::flags::none);
    (void)viaFactory.is_valid();

    auto viaDefaults = arena::create_rdma(std::size_t{ 1 } << 20, buffer, pd);
    (void)viaDefaults.capacity();
#endif

    // RDMA prefetch goes out through ibv_advise_mr.
    (void)viaCustom.prefetch(std::size_t{ 0 }, std::size_t{ 4096 });
    (void)viaCustom.error();
}

#endif // ENGRAM_ENABLE_RDMA

#ifdef ENGRAM_ENABLE_DMABUF

void engram_dmabuf_conformance(int deviceFd)
{
    auto viaCustom = arena::create_custom(std::size_t{ 1 } << 20, engram::custom::DmaBuf,
                                          engram::flags::none, deviceFd);
    static_assert(std::is_same_v<decltype(viaCustom), arena>);

#ifdef ENGRAM_SINGLE_HEADER_BUILD
    auto viaFactory = arena::create_dmabuf(std::size_t{ 1 } << 20, deviceFd, engram::flags::none);
    (void)viaFactory.is_valid();

    // A negative fd asks engram to open /dev/dma_heap/system itself.
    auto viaDefaults = arena::create_dmabuf(std::size_t{ 1 } << 20);
    (void)viaDefaults.capacity();
#endif

    (void)viaCustom.error();
    auto& value = viaCustom.push<int>(7);
    (void)value;
}

#endif // ENGRAM_ENABLE_DMABUF

// Whatever the backend, the generic surface has to survive the vendor headers being
// pulled in -- macro collisions out of <infiniband/verbs.h> and friends are exactly
// the sort of breakage this file exists to catch.
void engram_generic_surface_conformance()
{
    static_assert(!std::is_copy_constructible_v<arena>);
    static_assert(std::is_move_constructible_v<arena>);

    std::byte storage[1024];
    auto a = arena::adopt(storage, sizeof(storage), engram::flags::commit);

    auto& one = a.push<int>(1);
    auto many = a.push_array<double>(8);
    auto text = a.push_string(std::string_view{ "vendor" });

    a.pop(text);
    a.pop(many);
    a.pop(one);

    a.reset();
    (void)a.used();
    (void)a.remaining();
    (void)a.count();
    (void)a.source();
}
