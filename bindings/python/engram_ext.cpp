// pybind11 bindings for the public engram::arena API.
//
// The C++ push/pop helpers are templates that build/destroy C++ objects, which
// don't map onto Python. Instead we expose byte-oriented operations built on
// the same public API (push_array<std::byte> / pop_array<std::byte> /
// push_string) and hand allocated regions back as `memoryview`s that alias the
// arena's storage (kept alive via keep_alive).
//
// Device/accelerator backends (CUDA, Vulkan, DX12, ...) are intentionally not
// exposed: they require native device handles that don't exist in Python.

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include <pybind11/pybind11.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "engram.h"

namespace py = pybind11;

using engram::arena;
using engram::arena_error;
using engram::cache_locality;
using engram::memory_source;

namespace {

// Reserve `nbytes` of raw storage in the arena and return a pointer to it.
std::byte* reserve_bytes(arena& a, std::size_t nbytes)
{
    if (nbytes == 0)
        throw py::value_error("allocation size must be greater than zero");

    // push_array returns an empty span when the (alignment-rounded) request does
    // not fit, rather than overrunning the arena.
    auto span = a.push_array<std::byte>(nbytes);
    if (span.empty())
        throw py::value_error("allocation exceeds the arena's remaining capacity");

    return span.data();
}

} // namespace

PYBIND11_MODULE(_engram, m)
{
    m.doc() = "Python bindings for the engram arena-allocation library";

    py::enum_<memory_source>(m, "MemorySource")
        .value("stack", memory_source::stack)
        .value("heap", memory_source::heap)
        .value("external", memory_source::external)
        .value("custom", memory_source::custom);

    py::enum_<arena_error>(m, "ArenaError")
        .value("no_error", arena_error::no_error)
        .value("stack_overflow", arena_error::stack_overflow)
        .value("alloc_failed", arena_error::alloc_failed)
        .value("custom", arena_error::custom);

    py::enum_<cache_locality>(m, "CacheLocality")
        .value("Discard", cache_locality::Discard)
        .value("L3", cache_locality::L3)
        .value("L2", cache_locality::L2)
        .value("L1", cache_locality::L1);

    // Mirror engram::flags as plain int constants.
    auto flags = m.def_submodule("flags", "Allocation and cache-warm flag bits");
    flags.attr("none") = static_cast<int>(engram::flags::none);
    flags.attr("heap_fallback") = static_cast<int>(engram::flags::heap_fallback);
    flags.attr("true_contiguous") = static_cast<int>(engram::flags::true_contiguous);
    flags.attr("page_aligned") = static_cast<int>(engram::flags::page_aligned);
    flags.attr("commit") = static_cast<int>(engram::flags::commit);
    flags.attr("shared") = static_cast<int>(engram::flags::shared);
    flags.attr("no_clear") = static_cast<int>(engram::flags::no_clear);
    flags.attr("pin_to_physical") = static_cast<int>(engram::flags::pin_to_physical);
    flags.attr("unified") = static_cast<int>(engram::flags::unified);
    flags.attr("read") = static_cast<int>(engram::flags::read);
    flags.attr("write") = static_cast<int>(engram::flags::write);

    py::class_<arena>(m, "Arena")
        // ---- factories -----------------------------------------------------
        .def_static(
            "create",
            [](memory_source type, std::size_t size, int flags, std::size_t alignment) {
                // create() only builds heap/external/custom arenas; stack storage has to be
                // reserved in the caller's frame by the ENGRAM_STACK_ARENA macro, which has
                // no Python equivalent.
                if (type == memory_source::stack)
                    throw py::value_error("stack arenas cannot be created from Python; "
                                          "use Arena.heap() or Arena.adopt()");
                return arena::create(type, size, flags, alignment);
            },
            py::arg("type"), py::arg("size"), py::arg("flags") = 0,
            py::arg("alignment") = alignof(std::max_align_t),
            "Create an arena for the given memory_source (stack is not supported).")
        .def_static(
            "heap",
            [](std::size_t size, bool true_contiguous, std::size_t alignment) {
                return arena::heap(size, true_contiguous, alignment);
            },
            py::arg("size"), py::arg("true_contiguous") = false,
            py::arg("alignment") = alignof(std::max_align_t), "Heap arena.")
#ifdef __linux__
        .def_static(
            "named_heap",
            [](std::size_t size, const std::string& name, bool true_contiguous,
               std::size_t alignment) {
                return arena::heap(size, std::string_view{name}, true_contiguous, alignment);
            },
            py::arg("size"), py::arg("name"), py::arg("true_contiguous") = false,
            py::arg("alignment") = alignof(std::max_align_t),
            "Heap arena whose mapping is named in /proc/<pid>/maps (Linux only).")
        .def_static(
            "heapfile",
            [](std::size_t size, const std::string& name, std::size_t alignment) {
                return arena::heapfile(size, std::string_view{name}, alignment);
            },
            py::arg("size"), py::arg("name"),
            py::arg("alignment") = alignof(std::max_align_t),
            "Heap arena backed by an anonymous memfd (Linux only).")
#endif
        .def_static(
            "adopt",
            [](py::buffer buffer, int flags) {
                py::buffer_info info = buffer.request(/*writable=*/true);
                auto nbytes = static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
                return arena::adopt(reinterpret_cast<std::byte*>(info.ptr), nbytes, flags);
            },
            py::arg("buffer"), py::arg("flags") = 0, py::keep_alive<0, 1>(),
            "Adopt a caller-owned buffer (the arena does not free it).")

        // ---- allocation ----------------------------------------------------
        .def(
            "alloc",
            [](arena& a, std::size_t nbytes) {
                return py::memoryview::from_memory(reserve_bytes(a, nbytes),
                                                   static_cast<py::ssize_t>(nbytes), /*readonly=*/false);
            },
            py::arg("nbytes"), py::keep_alive<0, 1>(),
            "Reserve nbytes of uninitialized storage; returns a writable memoryview.")
        .def(
            "push_bytes",
            [](arena& a, py::buffer data) {
                py::buffer_info info = data.request(/*writable=*/false);
                auto nbytes = static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
                auto* dst = reserve_bytes(a, nbytes);
                std::memcpy(dst, info.ptr, nbytes);
                return py::memoryview::from_memory(dst, static_cast<py::ssize_t>(nbytes), /*readonly=*/false);
            },
            py::arg("data"), py::keep_alive<0, 1>(),
            "Copy a bytes-like object into the arena; returns a memoryview over the copy.")
        .def(
            "push_str",
            [](arena& a, const std::string& text) {
                auto sv = a.push_string(std::string_view{text});
                return py::memoryview::from_memory(sv.data(), static_cast<py::ssize_t>(sv.size()));
            },
            py::arg("text"), py::keep_alive<0, 1>(),
            "Store a NUL-terminated string; returns a read-only memoryview (excluding the NUL).")
        .def(
            "pop_bytes", [](arena& a, std::size_t nbytes) { a.pop_array<std::byte>(nbytes); },
            py::arg("nbytes"), "Release the last nbytes pushed (LIFO).")
        .def(
            "pop_str",
            [](arena& a, const py::object& text) {
                // push_str also stored a NUL, so the reservation is one byte longer than
                // the string itself.
                auto length = py::isinstance<py::str>(text) ? text.cast<std::string>().size()
                                                            : text.cast<std::size_t>();
                a.pop_array<std::byte>(length + 1);
            },
            py::arg("text"),
            "Release a string pushed with push_str. Pass the original str, or its length "
            "in bytes; the trailing NUL is accounted for automatically.")
        .def(
            "partition",
            [](arena& a, std::size_t start, std::size_t size, int flags) {
                return a.partition(start, size, flags);
            },
            py::arg("start"), py::arg("size"), py::arg("flags") = 0, py::keep_alive<0, 1>(),
            "Create a non-owning sub-arena over [start, start+size) of this arena (zeroed by "
            "default; pass flags.no_clear to skip). The parent is kept alive while the sub-arena lives.")

        // ---- prefetch / cache ---------------------------------------------
        .def(
            "prefetch",
            [](arena& a, std::size_t start, py::object size) {
                std::size_t n = size.is_none() ? (a.used() - start) : size.cast<std::size_t>();
                return a.prefetch(start, n);
            },
            py::arg("start") = 0, py::arg("size") = py::none(),
            "Prefetch a range into RAM (madvise / PrefetchVirtualMemory). Returns bool.")
        .def(
            "warm_cache",
            [](arena& a, cache_locality locality, int ioflags, std::size_t start, std::size_t size) {
                a.warm_cache(locality, ioflags, start, size);
            },
            py::arg("locality"), py::arg("ioflags") = 0, py::arg("start") = 0, py::arg("size") = 0,
            "Issue CPU prefetch hints over a range of the arena.")
        .def("sync", [](arena& a) { return a.sync(); },
             "Synchronize a device-backed arena (no-op for host arenas).")
        .def("reset", &arena::reset,
             "Reclaim all storage in O(1), resetting the arena to empty (runs no destructors).")
#ifndef ENGRAM_DISABLE_SAVE_RESTORE
        .def("save", &arena::save,
             "Record the current head so a later restore() rewinds to it. Save points nest "
             "LIFO; returns False if the save stack (ENGRAM_MAX_SAVE_STACKSZ) is full.")
        .def("restore", &arena::restore,
             "Rewind to the most recent save point, zeroing everything pushed since. "
             "Returns False if no save point is pending.")
        .def_property_readonly("save_depth", &arena::save_depth,
                               "Number of save points currently pending.")
#endif
#ifdef ENGRAM_EASY_POP
        .def("pop", [](arena& a) { a.pop(); },
             "Release the most recent push, whatever it was. Runs no destructors and "
             "zeroes the reclaimed bytes unless the arena was created with flags.no_clear.")
        .def_property_readonly("push_depth", &arena::push_depth,
                               "Number of pushes pop() can still rewind through.")
#endif
        .def("unpin", &arena::unpin, "Release pages pinned via flags::pin_to_physical.")

        // ---- introspection -------------------------------------------------
        .def("is_valid", &arena::is_valid)
        .def("empty", &arena::empty)
        .def_property_readonly("used", &arena::used)
        .def_property_readonly("capacity", &arena::capacity)
        .def_property_readonly("remaining", &arena::remaining)
        .def_property_readonly("count", &arena::count,
                               "Live allocation count (0 if built with ENGRAM_DISABLE_TRACKING).")
        .def_property_readonly("total", &arena::total,
                               "Lifetime allocation count (0 if built with ENGRAM_DISABLE_TRACKING).")
        .def_property_readonly("source", &arena::source)
        .def_property_readonly("error", &arena::error,
                               "The ArenaError recorded when the arena was created. Creation "
                               "never raises, so check this (or is_valid()) after a factory call.")
#ifdef ENGRAM_ENABLE_SOURCE_INFO
        .def_property_readonly(
            "origin",
            [](arena& a) {
                const auto& loc = a.origin();
                return py::make_tuple(loc.file_name(), loc.line(), loc.function_name());
            },
            "Where this arena was created, as (file, line, function). Requires a build with "
            "ENGRAM_ENABLE_SOURCE_INFO; the site recorded is inside the bindings.")
#endif
        .def(
            "data",
            [](arena& a) -> py::object {
                auto span = a.data();
                if (span.empty())
                    return py::memoryview(py::bytes());
                return py::memoryview::from_memory(span.data(), static_cast<py::ssize_t>(span.size()),
                                                   /*readonly=*/false);
            },
            py::keep_alive<0, 1>(),
            "Writable memoryview over the arena's whole underlying storage [base, capacity).")
        .def("__len__", [](arena& a) { return a.used(); })
        .def("__bool__", &arena::is_valid)
        .def("__repr__", [](arena& a) {
            return "<engram.Arena used=" + std::to_string(a.used()) + " capacity=" +
                   std::to_string(a.capacity()) + ">";
        });

    // ---- free functions ----------------------------------------------------
    m.def(
        "warm_cache",
        [](py::buffer buffer, cache_locality locality, int ioflags) {
            py::buffer_info info = buffer.request(/*writable=*/false);
            auto nbytes = static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
            engram::warm_cache(reinterpret_cast<std::byte*>(info.ptr), nbytes, locality, ioflags);
        },
        py::arg("buffer"), py::arg("locality"), py::arg("ioflags") = 0,
        "Emit CPU prefetch hints for any host buffer.");

    m.def(
        "prefetch",
        [](py::buffer buffer) {
            py::buffer_info info = buffer.request(/*writable=*/false);
            auto nbytes = static_cast<std::size_t>(info.size) * static_cast<std::size_t>(info.itemsize);
            return engram::prefetch(reinterpret_cast<std::byte*>(info.ptr), nbytes);
        },
        py::arg("buffer"), "Page a host buffer into RAM (madvise / PrefetchVirtualMemory).");
}
