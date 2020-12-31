/*
  Copyright 2011 David Robillard <http://drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "zix/common.h"
#include "zix/ring.h"
#include "zix/thread.h"

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MSG_SIZE 20

static ZixRing* ring       = 0;
static unsigned n_writes   = 0;
static bool     read_error = false;

ZIX_LOG_FUNC(1, 2)
static int
failure(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "error: ");
  vfprintf(stderr, fmt, args);
  va_end(args);
  return 1;
}

static int
gen_msg(int* msg, int start)
{
  for (int i = 0; i < MSG_SIZE; ++i) {
    msg[i] = start;
    start  = (start + 1) % INT_MAX;
  }
  return start;
}

static int
cmp_msg(int* msg1, int* msg2)
{
  for (int i = 0; i < MSG_SIZE; ++i) {
    if (msg1[i] != msg2[i]) {
      return !failure("%d != %d @ %d\n", msg1[i], msg2[i], i);
    }
  }

  return 1;
}

static void*
reader(void* ZIX_UNUSED(arg))
{
  printf("Reader starting\n");

  int      ref_msg[MSG_SIZE];  // Reference generated for comparison
  int      read_msg[MSG_SIZE]; // Read from ring
  unsigned count = 0;
  int      start = gen_msg(ref_msg, 0);
  for (unsigned i = 0; i < n_writes; ++i) {
    if (zix_ring_read_space(ring) >= MSG_SIZE * sizeof(int)) {
      if (zix_ring_read(ring, read_msg, MSG_SIZE * sizeof(int))) {
        if (!cmp_msg(ref_msg, read_msg)) {
          printf("FAIL: Message %u is corrupt\n", count);
          read_error = true;
          return NULL;
        }
        start = gen_msg(ref_msg, start);
        ++count;
      }
    }
  }

  printf("Reader finished\n");
  return NULL;
}

static void*
writer(void* ZIX_UNUSED(arg))
{
  printf("Writer starting\n");

  int write_msg[MSG_SIZE]; // Written to ring
  int start = gen_msg(write_msg, 0);
  for (unsigned i = 0; i < n_writes; ++i) {
    if (zix_ring_write_space(ring) >= MSG_SIZE * sizeof(int)) {
      if (zix_ring_write(ring, write_msg, MSG_SIZE * sizeof(int))) {
        start = gen_msg(write_msg, start);
      }
    }
  }

  printf("Writer finished\n");
  return NULL;
}

int
main(int argc, char** argv)
{
  if (argc > 1 && argv[1][0] == '-') {
    printf("Usage: %s SIZE N_WRITES\n", argv[0]);
    return 1;
  }

  unsigned size = 1024;
  if (argc > 1) {
    size = (unsigned)atoi(argv[1]);
  }

  n_writes = size * 1024;
  if (argc > 2) {
    n_writes = (unsigned)atoi(argv[2]);
  }

  printf("Testing %u writes of %d ints to a %u int ring...\n",
         n_writes,
         MSG_SIZE,
         size);

  ring = zix_ring_new(size);
  if (zix_ring_read_space(ring) != 0) {
    return failure("New ring is not empty\n");
  }
  if (zix_ring_write_space(ring) != zix_ring_capacity(ring)) {
    return failure("New ring write space != capacity\n");
  }

  zix_ring_mlock(ring);

  ZixThread reader_thread; // NOLINT
  if (zix_thread_create(&reader_thread, MSG_SIZE * 4, reader, NULL)) {
    return failure("Failed to create reader thread\n");
  }

  ZixThread writer_thread; // NOLINT
  if (zix_thread_create(&writer_thread, MSG_SIZE * 4, writer, NULL)) {
    return failure("Failed to create writer thread\n");
  }

  zix_thread_join(reader_thread, NULL);
  zix_thread_join(writer_thread, NULL);

  if (read_error) {
    return failure("Read error\n");
  }

  zix_ring_reset(ring);
  if (zix_ring_read_space(ring) > 0) {
    return failure("Reset did not empty ring.\n");
  }
  if (zix_ring_write_space(ring) != zix_ring_capacity(ring)) {
    return failure("Empty write space != capacity\n");
  }

  zix_ring_write(ring, "a", 1);
  zix_ring_write(ring, "b", 1);

  char     buf = 0;
  uint32_t n   = zix_ring_peek(ring, &buf, 1);
  if (n != 1) {
    return failure("Peek n (%u) != 1\n", n);
  }
  if (buf != 'a') {
    return failure("Peek error: '%c' != 'a'\n", buf);
  }

  n = zix_ring_skip(ring, 1);
  if (n != 1) {
    return failure("Skip n (%u) != 1\n", n);
  }

  if (zix_ring_read_space(ring) != 1) {
    return failure("Read space %u != 1\n", zix_ring_read_space(ring));
  }

  n = zix_ring_read(ring, &buf, 1);
  if (n != 1) {
    return failure("Peek n (%u) != 1\n", n);
  }
  if (buf != 'b') {
    return failure("Peek error: '%c' != 'b'\n", buf);
  }

  if (zix_ring_read_space(ring) != 0) {
    return failure("Read space %u != 0\n", zix_ring_read_space(ring));
  }

  n = zix_ring_peek(ring, &buf, 1);
  if (n > 0) {
    return failure("Successful underrun peak\n");
  }

  n = zix_ring_read(ring, &buf, 1);
  if (n > 0) {
    return failure("Successful underrun read\n");
  }

  n = zix_ring_skip(ring, 1);
  if (n > 0) {
    return failure("Successful underrun read\n");
  }

  char* big_buf = (char*)calloc(size, 1);
  n             = zix_ring_write(ring, big_buf, size - 1);
  if (n != (uint32_t)size - 1) {
    free(big_buf);
    return failure("Maximum size write failed (wrote %u)\n", n);
  }

  n = zix_ring_write(ring, big_buf, size);
  if (n != 0) {
    free(big_buf);
    return failure("Successful overrun write (size %u)\n", n);
  }

  free(big_buf);
  zix_ring_free(ring);
  return 0;
}
