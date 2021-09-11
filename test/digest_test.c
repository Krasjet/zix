/*
  Copyright 2020-2021 David Robillard <d@drobilla.net>

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

#undef NDEBUG

#include "zix/digest.h"

#include <assert.h>
#include <stddef.h>

// Just basic smoke tests to ensure the hash functions are reacting to data

static void
test_digest(void)
{
  static const uint8_t data[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  size_t last = 0u;

  for (size_t offset = 0; offset < 7; ++offset) {
    const size_t len = 8u - offset;
    for (size_t i = offset; i < 8; ++i) {
      const size_t h = zix_digest(0u, &data[i], len);
      assert(h != last);
      last = h;
    }
  }
}

static void
test_digest32(void)
{
  static const uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

  uint32_t last = 0u;

  for (size_t offset = 0; offset < 3; ++offset) {
    for (size_t i = offset; i < 4; ++i) {
      const uint32_t h = zix_digest32(0u, &data[i], 4);
      assert(h != last);
      last = h;
    }
  }
}

static void
test_digest64(void)
{
  static const uint8_t data[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  uint64_t last = 0u;

  for (size_t offset = 0; offset < 7; ++offset) {
    for (size_t i = offset; i < 8; ++i) {
      const uint64_t h = zix_digest64(0u, &data[i], 8);
      assert(h != last);
      last = h;
    }
  }
}

static void
test_digest32_aligned(void)
{
  static const uint32_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

  uint32_t last = 0u;

  for (size_t offset = 0; offset < 3; ++offset) {
    const size_t len = 4u - offset;
    for (size_t i = offset; i < 4; ++i) {
      const uint32_t h =
        zix_digest32_aligned(0u, &data[i], len * sizeof(uint32_t));
      assert(h != last);
      last = h;
    }
  }
}

static void
test_digest64_aligned(void)
{
  static const uint64_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

  uint64_t last = 0u;

  for (size_t offset = 0; offset < 3; ++offset) {
    const size_t len = 4u - offset;
    for (size_t i = offset; i < 4; ++i) {
      const uint64_t h =
        zix_digest64_aligned(0u, &data[i], len * sizeof(uint64_t));
      assert(h != last);
      last = h;
    }
  }
}

static void
test_digest_aligned(void)
{
  static const size_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};

  size_t last = 0u;

  for (size_t offset = 0; offset < 3; ++offset) {
    const size_t len = 4u - offset;
    for (size_t i = offset; i < 4; ++i) {
      const size_t h = zix_digest_aligned(0u, &data[i], len * sizeof(size_t));
      assert(h != last);
      last = h;
    }
  }
}

ZIX_PURE_FUNC int
main(void)
{
  test_digest32();
  test_digest64();
  test_digest();

  test_digest32_aligned();
  test_digest64_aligned();
  test_digest_aligned();

  return 0;
}
