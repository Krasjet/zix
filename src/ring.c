// Copyright 2011-2020 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "zix/ring.h"

#include "zix_config.h"

#include <stdlib.h>
#include <string.h>

#if USE_MLOCK
#  include <sys/mman.h>
#  define ZIX_MLOCK(ptr, size) mlock((ptr), (size))
#elif defined(_WIN32)
#  include <windows.h>
#  define ZIX_MLOCK(ptr, size) VirtualLock((ptr), (size))
#else
#  pragma message("warning: No memory locking, possible RT violations")
#  define ZIX_MLOCK(ptr, size)
#endif

#if defined(_MSC_VER)
#  if defined(_M_AMD64) || defined(_M_IX86) || defined(_M_X64)
    /* acquire and release fences are only necessary for
     * non-x86 systems. In fact, gcc will generate no
     * instructions for acq/rel fences on x86. We only need
     * to prevent compiler reordering. */
#    include <intrin.h>
#    define ZIX_READ_BARRIER() _ReadBarrier()
#    define ZIX_WRITE_BARRIER() _WriteBarrier()
#  else
#    include <windows.h>
#    define ZIX_READ_BARRIER() MemoryBarrier()
#    define ZIX_WRITE_BARRIER() MemoryBarrier()
#  endif
#elif defined(__GNUC__)
#  define ZIX_READ_BARRIER() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#  define ZIX_WRITE_BARRIER() __atomic_thread_fence(__ATOMIC_RELEASE)
#else
#  pragma message("warning: No memory barriers, possible SMP bugs")
#  define ZIX_READ_BARRIER()
#  define ZIX_WRITE_BARRIER()
#endif

struct ZixRingImpl {
  ZixAllocator* allocator;  ///< User allocator
  uint32_t      write_head; ///< Read index into buf
  uint32_t      read_head;  ///< Write index into buf
  uint32_t      size;       ///< Size (capacity) in bytes
  uint32_t      size_mask;  ///< Mask for fast modulo
  char*         buf;        ///< Contents
};

static inline uint32_t
next_power_of_two(uint32_t size)
{
  // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
  size--;
  size |= size >> 1U;
  size |= size >> 2U;
  size |= size >> 4U;
  size |= size >> 8U;
  size |= size >> 16U;
  size++;
  return size;
}

ZixRing*
zix_ring_new(ZixAllocator* const allocator, uint32_t size)
{
  ZixRing* ring = (ZixRing*)zix_malloc(allocator, sizeof(ZixRing));

  if (ring) {
    ring->allocator  = allocator;
    ring->write_head = 0;
    ring->read_head  = 0;
    ring->size       = next_power_of_two(size);
    ring->size_mask  = ring->size - 1;

    if (!(ring->buf = (char*)zix_malloc(allocator, ring->size))) {
      zix_free(allocator, ring);
      return NULL;
    }
  }

  return ring;
}

void
zix_ring_free(ZixRing* ring)
{
  if (ring) {
    zix_free(ring->allocator, ring->buf);
    zix_free(ring->allocator, ring);
  }
}

void
zix_ring_mlock(ZixRing* ring)
{
  ZIX_MLOCK(ring, sizeof(ZixRing));
  ZIX_MLOCK(ring->buf, ring->size);
}

void
zix_ring_reset(ZixRing* ring)
{
  ring->write_head = 0;
  ring->read_head  = 0;
}

static inline uint32_t
read_space_internal(const ZixRing* ring, uint32_t r, uint32_t w)
{
  if (r < w) {
    return w - r;
  }

  return (w - r + ring->size) & ring->size_mask;
}

uint32_t
zix_ring_read_space(const ZixRing* ring)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head; ZIX_READ_BARRIER();
  return read_space_internal(ring, r, w);
}

static inline uint32_t
write_space_internal(const ZixRing* ring, uint32_t r, uint32_t w)
{
  if (r == w) {
    return ring->size - 1;
  }

  if (r < w) {
    return ((r - w + ring->size) & ring->size_mask) - 1;
  }

  return (r - w) - 1;
}

uint32_t
zix_ring_write_space(const ZixRing* ring)
{
  const uint32_t r = ring->read_head; ZIX_READ_BARRIER();
  const uint32_t w = ring->write_head;
  return write_space_internal(ring, r, w);
}

uint32_t
zix_ring_capacity(const ZixRing* ring)
{
  return ring->size - 1;
}

static inline uint32_t
peek_internal(const ZixRing* ring,
              uint32_t       r,
              uint32_t       w,
              uint32_t       size,
              void*          dst)
{
  if (read_space_internal(ring, r, w) < size) {
    return 0;
  }

  if (r + size < ring->size) {
    memcpy(dst, &ring->buf[r], size);
  } else {
    const uint32_t first_size = ring->size - r;
    memcpy(dst, &ring->buf[r], first_size);
    memcpy((char*)dst + first_size, &ring->buf[0], size - first_size);
  }

  return size;
}

uint32_t
zix_ring_peek(ZixRing* ring, void* dst, uint32_t size)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head; ZIX_READ_BARRIER();
  return peek_internal(ring, r, w, size, dst);
}

uint32_t
zix_ring_read(ZixRing* ring, void* dst, uint32_t size)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head; ZIX_READ_BARRIER();

  if (peek_internal(ring, r, w, size, dst)) {
    ZIX_WRITE_BARRIER();
    ring->read_head = (r + size) & ring->size_mask;
    return size;
  }

  return 0;
}

uint32_t
zix_ring_skip(ZixRing* ring, uint32_t size)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head; ZIX_READ_BARRIER();
  if (read_space_internal(ring, r, w) < size) {
    return 0;
  }

  ZIX_WRITE_BARRIER();
  ring->read_head = (r + size) & ring->size_mask;
  return size;
}

uint32_t
zix_ring_write(ZixRing* ring, const void* src, uint32_t size)
{
  const uint32_t r = ring->read_head; ZIX_READ_BARRIER();
  const uint32_t w = ring->write_head;
  if (write_space_internal(ring, r, w) < size) {
    return 0;
  }

  if (w + size <= ring->size) {
    memcpy(&ring->buf[w], src, size);
    ZIX_WRITE_BARRIER();
    ring->write_head = (w + size) & ring->size_mask;
  } else {
    const uint32_t this_size = ring->size - w;
    memcpy(&ring->buf[w], src, this_size);
    memcpy(&ring->buf[0], (const char*)src + this_size, size - this_size);
    ZIX_WRITE_BARRIER();
    ring->write_head = size - this_size;
  }

  return size;
}
