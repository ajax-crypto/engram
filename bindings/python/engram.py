"""Pythonic front-end for the compiled ``_engram`` pybind11 extension.

This re-exports the native ``Arena`` type, the enums, and the free ``warm_cache``
/ ``prefetch`` helpers, and adds :class:`Flag` / :class:`IO` ``IntFlag`` wrappers
around the raw ``flags`` bit constants.

Example
-------
>>> import engram
>>> a = engram.Arena.heap(1 << 20)
>>> mv = a.alloc(1024)                 # writable memoryview into the arena
>>> mv[:5] = b"hello"
>>> a.used
1024
"""

from enum import IntFlag

try:  # installed as a top-level module (default wheel layout)
    from _engram import (
        Arena,
        MemorySource,
        ArenaError,
        CacheLocality,
        flags as _flags,
        warm_cache,
        prefetch,
    )
except ImportError:  # imported as part of a package
    from ._engram import (  # type: ignore[no-redef]
        Arena,
        MemorySource,
        ArenaError,
        CacheLocality,
        flags as _flags,
        warm_cache,
        prefetch,
    )


class Flag(IntFlag):
    """Allocation flags accepted by the arena factories (``create``/``heap``/...)."""

    NONE = _flags.none
    HEAP_FALLBACK = _flags.heap_fallback
    TRUE_CONTIGUOUS = _flags.true_contiguous
    PAGE_ALIGNED = _flags.page_aligned
    COMMIT = _flags.commit
    SHARED = _flags.shared
    NO_CLEAR = _flags.no_clear
    PIN_TO_PHYSICAL = _flags.pin_to_physical
    UNIFIED = _flags.unified


class IO(IntFlag):
    """Access-intent hint for :func:`warm_cache` / ``Arena.warm_cache``."""

    READ = _flags.read
    WRITE = _flags.write


__all__ = [
    "Arena",
    "MemorySource",
    "ArenaError",
    "CacheLocality",
    "Flag",
    "IO",
    "warm_cache",
    "prefetch",
]
