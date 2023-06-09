/* This is the Murmur3 hash algorithm.
 *
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain.  The author hereby disclaims copyright to this source code.
 */

#include <stdint.h>
#include <stdio.h>

#define FORCE_INLINE static inline __attribute__((always_inline))

static inline uint32_t rotl32 ( uint32_t x, int8_t r )
{
  return (x << r) | (x >> (32 - r));
}

#define ROTL32(x,y)     rotl32(x,y)

/* Block read - if your platform needs to do endian-swapping or can
   only handle aligned reads, do the conversion here.  */

FORCE_INLINE uint32_t getblock32 ( const uint8_t * p, int i )
{
  return p[i] + (p[i+1]<<8) + (p[i+2]<<16) + (((uint32_t) p[i+3])<<24);
}

/* Finalization mix - force all bits of a hash block to avalanche.  */

FORCE_INLINE uint32_t fmix32 ( uint32_t h )
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

/* The Murmur3 hash function.  */
static uint32_t
MurmurHash3_x86_32 (const void *key, size_t len, uint32_t seed)
{
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = (int) (len / 4);

  uint32_t h1 = seed;

  const uint32_t c1 = 0xcc9e2d51;
  const uint32_t c2 = 0x1b873593;

  /* body */

  const uint8_t * tail = data + nblocks*4;

  for(int i = -nblocks; i; i++)
  {
    uint32_t k1 = getblock32(tail,i*4);

    k1 *= c1;
    k1 = ROTL32(k1,15);
    k1 *= c2;

    h1 ^= k1;
    h1 = ROTL32(h1,13);
    h1 = h1*5+0xe6546b64;
  }

  /* tail */

  uint32_t k1 = 0;

  switch(len & 3)
  {
  case 3: k1 ^= tail[2] << 16;
  case 2: k1 ^= tail[1] << 8;
  case 1: k1 ^= tail[0];
          k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  /* finalization */

  h1 ^= len;

  h1 = fmix32(h1);

  return h1;
}

uint32_t hurd_ihash_hash32 (const void *, size_t, uint32_t)
  __attribute__ ((weak, alias ("MurmurHash3_x86_32")));
