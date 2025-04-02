
#include "./a1cbor.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

////////////////////////////////////////
// Constants
////////////////////////////////////////

static const uint64_t A1C_kNoOpTag = 55799;
static const char *A1C_kEmptyString = "";

////////////////////////////////////////
// Utilities
////////////////////////////////////////

#if defined(A1C_TEST_FALLBACK) || !defined(__has_builtin)
#define __has_builtin(x) 0
#endif

static bool A1C_overflowAdd_fallback(size_t x, size_t y, size_t *result) {
  *result = x + y;
  if (x > SIZE_MAX - y) {
    return true;
  } else {
    return false;
  }
}

/// @returns true on overflow.
static bool A1C_overflowAdd(size_t x, size_t y, size_t *result) {
#if __has_builtin(__builtin_add_overflow)
  return __builtin_add_overflow(x, y, result);
#else
  return A1C_overflowAdd_fallback(x, y, result);
#endif
}

static bool A1C_overflowMul_fallback(size_t x, size_t y, size_t *result) {
  *result = x * y;
  if (y > 0 && x > SIZE_MAX / y) {
    return true;
  } else {
    return false;
  }
}

static bool A1C_overflowMul(size_t x, size_t y, size_t *result) {
#if __has_builtin(__builtin_mul_overflow)
  return __builtin_mul_overflow(x, y, result);
#else
  return A1C_overflowMul_fallback(x, y, result);
#endif
}

static void A1C_byteswap_fallback(void *value, size_t size) {
  assert(size == 2 || size == 4 || size == 8);
  uint8_t *first = (uint8_t *)value;
  uint8_t *last = first + size - 1;
  while (first < last) {
    uint8_t tmp = *first;
    *first = *last;
    *last = tmp;
    first++;
    last--;
  }
}

static uint32_t A1C_byteswap16(uint32_t value) {
#if __has_builtin(__builtin_bswap16)
  return __builtin_bswap16(value);
#else
  A1C_byteswap_fallback(&value, sizeof(value));
  return value;
#endif
}

static uint32_t A1C_byteswap32(uint32_t value) {
#if __has_builtin(__builtin_bswap32)
  return __builtin_bswap32(value);
#else
  A1C_byteswap_fallback(&value, sizeof(value));
  return value;
#endif
}

static uint32_t A1C_byteswap64(uint32_t value) {
#if __has_builtin(__builtin_bswap64)
  return __builtin_bswap64(value);
#else
  A1C_byteswap_fallback(&value, sizeof(value));
  return value;
#endif
}

static void A1C_byteswap(void *value, size_t size) {
  if (size == 2) {
    *(uint16_t *)value = A1C_byteswap16(*(uint16_t *)value);
  } else if (size == 4) {
    *(uint32_t *)value = A1C_byteswap32(*(uint32_t *)value);
  } else if (size == 8) {
    *(uint64_t *)value = A1C_byteswap64(*(uint64_t *)value);
  } else {
    assert(size == 1);
  }
}

////////////////////////////////////////
// Arena
////////////////////////////////////////

static void *A1C_Arena_calloc(A1C_Arena *arena, size_t count, size_t size) {
  size_t bytes;
  if (A1C_overflowMul(count, size, &bytes)) {
    return NULL;
  }
  if (bytes == 0) {
    return (void *)A1C_kEmptyString;
  }
  return arena->calloc(arena->opaque, bytes);
}

typedef struct {
  A1C_Arena backingArena;
  size_t allocatedBytes;
  size_t limitBytes;
} A1C_LimitedArena;

static void *A1C_LimitedArena_calloc(void *opaque, size_t bytes) {
  A1C_LimitedArena *arena = (A1C_LimitedArena *)opaque;
  if (arena == NULL) {
    return NULL;
  }
  assert(arena->limitBytes != 0);
  assert(arena->allocatedBytes <= arena->limitBytes);

  size_t newBytes;
  if (A1C_overflowAdd(arena->allocatedBytes, bytes, &newBytes)) {
    return NULL;
  }
  if (newBytes > arena->limitBytes) {
    return NULL;
  }
  void *result = arena->backingArena.calloc(arena->backingArena.opaque, bytes);
  if (result != NULL) {
    arena->allocatedBytes = newBytes;
  }
  return result;
}

A1C_Arena A1C_LimitedArena_init(A1C_Arena arena, size_t limitBytes) {
  if (limitBytes == 0) {
    return arena;
  }
  A1C_LimitedArena *limitedArena =
      A1C_Arena_calloc(&arena, 1, sizeof(A1C_LimitedArena));
  if (limitedArena != NULL) {
    limitedArena->backingArena = arena;
    limitedArena->allocatedBytes = 0;
    limitedArena->limitBytes = limitBytes;
  }
  arena.calloc = A1C_LimitedArena_calloc;
  arena.opaque = limitedArena;

  return arena;
}

void A1C_LimitedArena_reset(A1C_Arena *arena) {
  A1C_LimitedArena *limitedArena = (A1C_LimitedArena *)arena->opaque;
  if (limitedArena != NULL) {
    assert(limitedArena->limitBytes != 0);
    assert(limitedArena->allocatedBytes <= limitedArena->limitBytes);
    limitedArena->allocatedBytes = 0;
  }
}

////////////////////////////////////////
// Item Helpers
////////////////////////////////////////

static bool A1C_Item_arrayEq(const A1C_Item *a, size_t aSize, const A1C_Item *b,
                             size_t bSize) {
  if (aSize != bSize) {
    return false;
  }
  for (size_t i = 0; i < aSize; i++) {
    if (!A1C_Item_eq(&a[i], &b[i])) {
      return false;
    }
  }
  return true;
}

bool A1C_Item_eq(const A1C_Item *a, const A1C_Item *b) {
  // Special case integers: They allow equality across types.
  if (a->type == A1C_ItemType_int64 && b->type == A1C_ItemType_uint64) {
    if (a->int64 < 0) {
      return false;
    }
    return (uint64_t)a->int64 == b->uint64;
  }
  if (a->type == A1C_ItemType_uint64 && b->type == A1C_ItemType_int64) {
    if (b->int64 < 0) {
      return false;
    }
    return a->uint64 == (uint64_t)b->int64;
  }

  if (a->type != b->type) {
    return false;
  }

  switch (a->type) {
  case A1C_ItemType_uint64:
    return a->uint64 == b->uint64;
  case A1C_ItemType_int64:
    return a->int64 == b->int64;
  case A1C_ItemType_float64:
    return a->float64 == b->float64;
  case A1C_ItemType_float32:
    return a->float32 == b->float32;
  case A1C_ItemType_true:
  case A1C_ItemType_false:
  case A1C_ItemType_null:
  case A1C_ItemType_undefined:
  case A1C_ItemType_simple:
    return a->simple == b->simple;
  case A1C_ItemType_bytes:
    return a->bytes.size == b->bytes.size &&
           memcmp(a->bytes.data, b->bytes.data, a->bytes.size) == 0;
  case A1C_ItemType_string:
    return a->string.size == b->string.size &&
           memcmp(a->string.data, b->string.data, a->string.size) == 0;
  case A1C_ItemType_array:
    return A1C_Item_arrayEq(a->array.items, a->array.size, b->array.items,
                            b->array.size);
  case A1C_ItemType_map:
    return A1C_Item_arrayEq(a->map.keys, a->map.size, b->map.keys,
                            b->map.size) &&
           A1C_Item_arrayEq(a->map.values, a->map.size, b->map.values,
                            b->map.size);
  case A1C_ItemType_tag:
    return a->tag.tag == b->tag.tag && A1C_Item_eq(a->tag.item, b->tag.item);
  case A1C_ItemType_invalid:
    return false;
  }
}

const A1C_Item *A1C_Map_get(const A1C_Map *map, const A1C_Item *key) {
  for (size_t i = 0; i < map->size; i++) {
    if (A1C_Item_eq(&map->keys[i], key)) {
      return &map->values[i];
    }
  }
  return NULL;
}

const A1C_Item *A1C_Map_get_cstr(const A1C_Map *map, const char *key) {
  A1C_Item keyItem;
  A1C_Item_string_ref(&keyItem, (char *)key, strlen(key));
  return A1C_Map_get(map, &keyItem);
}

const A1C_Item *A1C_Map_get_int(const A1C_Map *map, A1C_Int64 key) {
  A1C_Item keyItem;
  A1C_Item_int64(&keyItem, key);
  return A1C_Map_get(map, &keyItem);
}

const A1C_Item *A1C_Array_get(const A1C_Array *array, size_t index) {
  if (index >= array->size) {
    return NULL;
  }
  return &array->items[index];
}

////////////////////////////////////////
// Creation
////////////////////////////////////////

A1C_Item *A1C_Item_root(A1C_Arena *arena, A1C_UInt64 value) {
  A1C_Item *item = A1C_Arena_calloc(arena, 1, sizeof(A1C_Item));
  return item;
}

void A1C_Item_uint64(A1C_Item *item, A1C_UInt64 value) {
  item->type = A1C_ItemType_uint64;
  item->uint64 = value;
}

void A1C_Item_int64(A1C_Item *item, A1C_Int64 value) {
  item->type = A1C_ItemType_int64;
  item->int64 = value;
}

void A1C_Item_float64(A1C_Item *item, A1C_Float64 value) {
  item->type = A1C_ItemType_float64;
  item->float64 = value;
}

void A1C_Item_float32(A1C_Item *item, A1C_Float32 value) {
  item->type = A1C_ItemType_float32;
  item->float32 = value;
}

void A1C_Item_bool(A1C_Item *item, bool value) {
  item->type = value ? A1C_ItemType_true : A1C_ItemType_false;
}

void A1C_Item_null(A1C_Item *item) { item->type = A1C_ItemType_null; }

void A1C_Item_undefined(A1C_Item *item) { item->type = A1C_ItemType_undefined; }

void A1C_Item_simple(A1C_Item *item, A1C_Simple value) {
  item->type = A1C_ItemType_simple;
  item->simple = value;
}

A1C_Tag *A1C_Item_tag(A1C_Item *item, A1C_UInt64 tag, A1C_Arena *arena) {
  A1C_Item *child = A1C_Arena_calloc(arena, 1, sizeof(A1C_Item));
  if (child == NULL) {
    return NULL;
  }
  child->parent = item;

  item->type = A1C_ItemType_tag;
  item->tag.tag = tag;
  item->tag.item = child;

  return &item->tag;
}

uint8_t *A1C_Item_bytes(A1C_Item *item, size_t size, A1C_Arena *arena) {
  uint8_t *data = A1C_Arena_calloc(arena, size, 1);
  if (data == NULL) {
    return NULL;
  }

  A1C_Item_bytes_ref(item, data, size);

  return data;
}

void A1C_Item_bytes_ref(A1C_Item *item, uint8_t *data, size_t size) {
  item->type = A1C_ItemType_bytes;
  item->bytes.data = data;
  item->bytes.size = size;
}

char *A1C_Item_string(A1C_Item *item, size_t size, A1C_Arena *arena) {
  char *data = A1C_Arena_calloc(arena, size, 1);
  if (data == NULL) {
    return NULL;
  }

  A1C_Item_string_ref(item, data, size);

  return data;
}

void A1C_Item_string_ref(A1C_Item *item, char *data, size_t size) {
  item->type = A1C_ItemType_string;
  item->string.data = data;
  item->string.size = size;
}

A1C_Map *A1C_Item_map(A1C_Item *item, size_t size, A1C_Arena *arena) {
  A1C_Item *items = A1C_Arena_calloc(arena, size, 2 * sizeof(A1C_Item));
  if (items == NULL) {
    return NULL;
  }

  item->type = A1C_ItemType_map;
  item->map.keys = items;
  item->map.values = items + size;
  item->map.size = size;

  return &item->map;
}

A1C_Array *A1C_Item_array(A1C_Item *item, size_t size, A1C_Arena *arena) {
  A1C_Item *items = A1C_Arena_calloc(arena, size, sizeof(A1C_Item));
  if (items == NULL) {
    return NULL;
  }

  item->type = A1C_ItemType_array;
  item->array.items = items;
  item->array.size = size;

  return &item->array;
}

////////////////////////////////////////
// Shared Coder Helpers
////////////////////////////////////////

typedef enum {
  A1C_MajorType_uint = 0,
  A1C_MajorType_int = 1,
  A1C_MajorType_bytes = 2,
  A1C_MajorType_string = 3,
  A1C_MajorType_array = 4,
  A1C_MajorType_map = 5,
  A1C_MajorType_tag = 6,
  A1C_MajorType_special = 7,
} A1C_MajorType;

typedef struct {
  uint8_t header;
} A1C_ItemHeader;

A1C_ItemHeader A1C_ItemHeader_make(A1C_MajorType type, uint8_t shortCount) {
  A1C_ItemHeader header;
  header.header = (uint8_t)(type << 5) | shortCount;
  return header;
}

A1C_MajorType A1C_ItemHeader_majorType(A1C_ItemHeader header) {
  return (A1C_MajorType)(header.header >> 5);
}

uint8_t A1C_ItemHeader_shortCount(A1C_ItemHeader header) {
  return header.header & 0x1F;
}

bool A1C_ItemHeader_isBreak(A1C_ItemHeader header) {
  return header.header == 0xFF;
}

bool A1C_ItemHeader_isIndefinite(A1C_ItemHeader header) {
  return A1C_ItemHeader_shortCount(header) == 31;
}

bool A1C_ItemHeader_isLegal(A1C_ItemHeader header) {
  const A1C_MajorType majorType = A1C_ItemHeader_majorType(header);
  const uint8_t shortCount = A1C_ItemHeader_shortCount(header);
  if (shortCount >= 28) {
    if (shortCount < 31) {
      return false;
    }
    assert(shortCount == 31);
    return !(majorType == A1C_MajorType_uint ||
             majorType == A1C_MajorType_int || majorType == A1C_MajorType_tag);
  }
  return true;
}

////////////////////////////////////////
// Decoder
////////////////////////////////////////

void A1C_Decoder_init(A1C_Decoder *decoder, A1C_Arena arena,
                      size_t limitBytes) {
  memset(decoder, 0, sizeof(A1C_Decoder));
  assert(arena.calloc != NULL);
  decoder->arena = A1C_LimitedArena_init(arena, limitBytes);
}

static void A1C_Decoder_reset(A1C_Decoder *decoder, const uint8_t *start) {
  memset(&decoder->error, 0, sizeof(A1C_Error));
  decoder->start = start;
  decoder->parent = NULL;
  decoder->depth = 0;
  if (decoder->maxDepth == 0) {
    decoder->maxDepth = A1C_MAX_DEPTH_DEFAULT;
  }
  A1C_LimitedArena_reset(&decoder->arena);
}

static A1C_Item *A1C_Decoder_decodeOne(A1C_Decoder *decoder, const uint8_t *ptr,
                                       const uint8_t *end);
static A1C_Item *A1C_Decoder_decodeOneInto(A1C_Decoder *decoder,
                                           const uint8_t *ptr,
                                           const uint8_t *end, A1C_Item *item);

#define A1C_Decoder_error(errorType)                                           \
  do {                                                                         \
    decoder->error.type = (errorType);                                         \
    decoder->error.srcPos = ptr - decoder->start;                              \
    decoder->error.item = decoder->parent;                                     \
    return NULL;                                                               \
  } while (0)

#define A1C_Decoder_peek(out, size)                                            \
  do {                                                                         \
    void *const out_ = (out);                                                  \
    const size_t size_ = (size_t)(size);                                       \
    assert(out_ != NULL);                                                      \
    assert(ptr != NULL);                                                       \
    assert(ptr <= end);                                                        \
    assert(decoder->start <= ptr);                                             \
    if ((size_t)(end - ptr) < size_) {                                         \
      A1C_Decoder_error(A1C_ErrorType_truncated);                              \
      return NULL;                                                             \
    }                                                                          \
    if (size_ > 0) {                                                           \
      memcpy(out_, ptr, size_);                                                \
    }                                                                          \
  } while (0)

#define A1C_Decoder_read(out, size)                                            \
  do {                                                                         \
    const size_t size_ = (size_t)(size);                                       \
    A1C_Decoder_peek((out), size_);                                            \
    ptr += size_;                                                              \
  } while (0)

#define A1C_Decoder_readInt(out, size)                                         \
  do {                                                                         \
    void *const out_ = (out);                                                  \
    const size_t size_ = (size);                                               \
    assert(sizeof(*out) >= size_);                                             \
    A1C_Decoder_read(out_, size_);                                             \
    A1C_byteswap(out_, size_);                                                 \
  } while (0)

#define A1C_Decoder_readFloat(out) A1C_Decoder_readInt(out)

#define A1C_Decoder_readCount(header, out)                                     \
  do {                                                                         \
    assert(sizeof(*out) == 8);                                                 \
    const A1C_ItemHeader _header = (header);                                   \
    uint64_t *const out_ = (out);                                              \
    const uint8_t shortCount = A1C_ItemHeader_shortCount(_header);             \
    assert(A1C_ItemHeader_isLegal(_header));                                   \
    if (shortCount < 24 || shortCount == 31) {                                 \
      *out_ = A1C_ItemHeader_shortCount(_header);                              \
    } else if (shortCount == 24) {                                             \
      A1C_Decoder_readInt(out_, 1);                                            \
    } else if (shortCount == 25) {                                             \
      A1C_Decoder_readInt(out_, 2);                                            \
    } else if (shortCount == 26) {                                             \
      A1C_Decoder_readInt(out_, 4);                                            \
    } else if (shortCount == 27) {                                             \
      A1C_Decoder_readInt(out_, 8);                                            \
    } else {                                                                   \
      assert(false);                                                           \
    }                                                                          \
  } while (0)

#define A1C_Decoder_readSize(header, out)                                      \
  do {                                                                         \
    uint64_t tmp_;                                                             \
    A1C_Decoder_readCount(header, &tmp_);                                      \
    if (tmp_ > SIZE_MAX) {                                                     \
      A1C_Decoder_error(A1C_ErrorType_integerOverflow);                        \
    }                                                                          \
    *(out) = (size_t)tmp_;                                                     \
  } while (0)

static A1C_Item *A1C_Decoder_decodeUInt(A1C_Decoder *decoder,
                                        const uint8_t *ptr, const uint8_t *end,
                                        A1C_ItemHeader header, A1C_Item *item) {

  uint64_t value;
  A1C_Decoder_readCount(header, &value);
  A1C_Item_uint64(item, value);
  return item;
}

static A1C_Item *A1C_Decoder_decodeInt(A1C_Decoder *decoder, const uint8_t *ptr,
                                       const uint8_t *end,
                                       A1C_ItemHeader header, A1C_Item *item) {

  uint64_t neg;
  A1C_Decoder_readCount(header, &neg);
  if (neg > ((uint64_t)1 << 63)) {
    A1C_Decoder_error(A1C_ErrorType_integerOverflow);
  }
  A1C_Item_int64(item, (int64_t)-neg);
  return item;
}

static A1C_Item *A1C_Decoder_decodeData(A1C_Decoder *decoder,
                                        const uint8_t *ptr, const uint8_t *end,
                                        A1C_ItemHeader header, A1C_Item *item) {
  size_t singleSize;
  A1C_Decoder_readSize(header, &singleSize);
  uint8_t *data;
  size_t totalSize = 0;
  const A1C_MajorType majorType = A1C_ItemHeader_majorType(header);
  if (singleSize == 31) {
    A1C_Item *previous = NULL;
    for (;;) {
      A1C_ItemHeader childHeader;
      A1C_Decoder_peek(&childHeader, sizeof(childHeader));
      if (A1C_ItemHeader_isBreak(childHeader)) {
        break;
      }

      if (A1C_ItemHeader_majorType(childHeader) != majorType ||
          A1C_ItemHeader_isIndefinite(childHeader)) {
        A1C_Decoder_error(A1C_ErrorType_invalidChunkedString);
      }
      A1C_Item *child = A1C_Decoder_decodeOne(decoder, ptr, end);
      if (child == NULL) {
        return NULL;
      }
      const size_t size = majorType == A1C_MajorType_bytes ? child->bytes.size
                                                           : child->string.size;
      if (A1C_overflowAdd(totalSize, size, &totalSize)) {
        A1C_Decoder_error(A1C_ErrorType_integerOverflow);
      }
      child->parent = previous;
      previous = child;
    }
    data = A1C_Arena_calloc(&decoder->arena, totalSize, 1);
    if (data == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    uint8_t *dataEnd = data + totalSize;
    while (previous != NULL) {
      const uint8_t *chunkPtr = majorType == A1C_MajorType_bytes
                                    ? previous->bytes.data
                                    : (const uint8_t *)previous->string.data;
      const size_t chunkSize = majorType == A1C_MajorType_bytes
                                   ? previous->bytes.size
                                   : previous->string.size;
      if (chunkSize > 0) {
        dataEnd -= chunkSize;
        assert(dataEnd >= data);
        memcpy(dataEnd, chunkPtr, chunkSize);
      }
      previous = previous->parent;
    }
    assert(dataEnd == data);
  } else {
    data = A1C_Arena_calloc(&decoder->arena, singleSize, 1);
    if (data == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    A1C_Decoder_read(data, singleSize);
    totalSize = singleSize;
  }
  if (majorType == A1C_MajorType_bytes) {
    A1C_Item_bytes_ref(item, data, totalSize);
  } else {
    A1C_Item_string_ref(item, (char *)data, totalSize);
  }
  return item;
}

static A1C_Item *A1C_Decoder_decodeArray(A1C_Decoder *decoder,
                                         const uint8_t *ptr, const uint8_t *end,
                                         A1C_ItemHeader header,
                                         A1C_Item *item) {
  size_t size;
  A1C_Decoder_readSize(header, &size);
  if (size == 31) {
    size = 0;
    A1C_Item *previous = NULL;
    for (;;) {
      A1C_ItemHeader childHeader;
      A1C_Decoder_peek(&childHeader, sizeof(childHeader));
      if (A1C_ItemHeader_isBreak(childHeader)) {
        break;
      }
      A1C_Item *child = A1C_Decoder_decodeOne(decoder, ptr, end);
      if (child == NULL) {
        return NULL;
      }
      child->parent = previous;
      ++size;
    }
    A1C_Array *array = A1C_Item_array(item, size, &decoder->arena);
    if (array == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    while (previous != NULL) {
      A1C_Item *child = previous;
      previous = previous->parent;

      array->items[--size] = *child;
      child->parent = item;
    }
    assert(size == 0);
  } else {
    A1C_Array *array = A1C_Item_array(item, size, &decoder->arena);
    if (array == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    for (size_t i = 0; i < size; i++) {
      A1C_Item *child =
          A1C_Decoder_decodeOneInto(decoder, ptr, end, array->items + i);
      if (child == NULL) {
        return NULL;
      }
      child->parent = item;
    }
  }
  return item;
}

static A1C_Item *A1C_Decoder_decodeMap(A1C_Decoder *decoder, const uint8_t *ptr,
                                       const uint8_t *end,
                                       A1C_ItemHeader header, A1C_Item *item) {
  size_t size;
  A1C_Decoder_readSize(header, &size);
  if (size == 31) {
    size = 0;
    A1C_Item *prevKey = NULL;
    A1C_Item *prevVal = NULL;
    for (;;) {
      A1C_ItemHeader keyHeader;
      A1C_Decoder_peek(&keyHeader, sizeof(keyHeader));
      if (A1C_ItemHeader_isBreak(keyHeader)) {
        break;
      }
      A1C_Item *key = A1C_Decoder_decodeOne(decoder, ptr, end);
      if (key == NULL) {
        return NULL;
      }
      A1C_Item *value = A1C_Decoder_decodeOne(decoder, ptr, end);
      if (value == NULL) {
        return NULL;
      }
      key->parent = prevKey;
      prevKey = key;

      value->parent = prevVal;
      prevVal = value;

      ++size;
    }
    A1C_Map *map = A1C_Item_map(item, size, &decoder->arena);
    if (map == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    while (prevKey != NULL) {
      A1C_Item *key = prevKey;
      prevKey = prevKey->parent;

      assert(prevVal != NULL);
      A1C_Item *value = prevVal;
      prevVal = prevVal->parent;

      --size;
      map->keys[size] = *key;
      key->parent = item;
      map->values[size] = *value;
      value->parent = item;
    }
    assert(size == 0);
  } else {
    A1C_Map *map = A1C_Item_map(item, size, &decoder->arena);
    if (map == NULL) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    for (size_t i = 0; i < size; i++) {
      A1C_Item *key =
          A1C_Decoder_decodeOneInto(decoder, ptr, end, map->keys + i);
      if (key == NULL) {
        return NULL;
      }
      key->parent = item;

      A1C_Item *value =
          A1C_Decoder_decodeOneInto(decoder, ptr, end, map->values + i);
      if (value == NULL) {
        return NULL;
      }
      value->parent = item;
    }
  }
  return item;
}

static A1C_Item *A1C_Decoder_decodeTag(A1C_Decoder *decoder, const uint8_t *ptr,
                                       const uint8_t *end,
                                       A1C_ItemHeader header, A1C_Item *item) {
  uint64_t value;
  A1C_Decoder_readCount(header, &value);
  A1C_Tag *tag = A1C_Item_tag(item, value, &decoder->arena);
  if (tag == NULL) {
    A1C_Decoder_error(A1C_ErrorType_badAlloc);
  }
  A1C_Item *child = A1C_Decoder_decodeOneInto(decoder, ptr, end, tag->item);

  if (tag->tag == A1C_kNoOpTag) {
    return child;
  }

  return item;
}

static A1C_Item *A1C_Decoder_decodeSpecial(A1C_Decoder *decoder,
                                           const uint8_t *ptr,
                                           const uint8_t *end,
                                           A1C_ItemHeader header,
                                           A1C_Item *item) {
  const size_t shortCount = A1C_ItemHeader_shortCount(header);
  if (shortCount == 20 || shortCount == 21) {
    A1C_Item_bool(item, shortCount == 21);
  } else if (shortCount == 22) {
    A1C_Item_null(item);
  } else if (shortCount == 23) {
    A1C_Item_undefined(item);
  } else if (shortCount == 24) {
    uint8_t value;
    A1C_Decoder_readInt(&value, sizeof(value));
    if (value < 32) {
      A1C_Decoder_error(A1C_ErrorType_invalidSimpleEncoding);
    }
    A1C_Item_simple(item, value);
  } else if (shortCount == 25) {
    A1C_Decoder_error(A1C_ErrorType_halfPrecisionUnsupported);
  } else if (shortCount == 26) {
    float value;
    A1C_Decoder_readInt(&value, sizeof(value));
    A1C_Item_float32(item, value);
  } else if (shortCount == 27) {
    double value;
    A1C_Decoder_readInt(&value, sizeof(value));
    A1C_Item_float64(item, value);
  } else if (shortCount < 20) {
    A1C_Item_simple(item, shortCount);
  } else if (shortCount == 31) {
    A1C_Decoder_error(A1C_ErrorType_breakNotAllowed);
  } else {
    assert(shortCount >= 28 && shortCount <= 30);
    assert(false);
  }
  return item;
}

static A1C_Item *A1C_Decoder_decodeOne(A1C_Decoder *decoder, const uint8_t *ptr,
                                       const uint8_t *end) {
  A1C_Item *item = A1C_Arena_calloc(&decoder->arena, 1, sizeof(A1C_Item));
  if (item == NULL) {
    A1C_Decoder_error(A1C_ErrorType_badAlloc);
  }
  item->parent = decoder->parent;
  return A1C_Decoder_decodeOneInto(decoder, ptr, end, item);
}

static A1C_Item *A1C_Decoder_decodeOneInto(A1C_Decoder *decoder,
                                           const uint8_t *ptr,
                                           const uint8_t *end, A1C_Item *item) {
  if (++decoder->depth > decoder->maxDepth) {
    A1C_Decoder_error(A1C_ErrorType_maxDepthExceeded);
  }

  A1C_ItemHeader header;
  A1C_Decoder_read(&header, sizeof(header));

  if (!A1C_ItemHeader_isLegal(header)) {
    decoder->error.type = A1C_ErrorType_invalidItemHeader;
    decoder->error.srcPos = ptr - decoder->start;
    decoder->error.item = decoder->parent;
    return NULL;
  }

  switch (A1C_ItemHeader_majorType(header)) {
  case A1C_MajorType_uint:
    return A1C_Decoder_decodeUInt(decoder, ptr, end, header, item);
  case A1C_MajorType_int:
    return A1C_Decoder_decodeInt(decoder, ptr, end, header, item);
  case A1C_MajorType_bytes:
    return A1C_Decoder_decodeData(decoder, ptr, end, header, item);
  case A1C_MajorType_string:
    return A1C_Decoder_decodeData(decoder, ptr, end, header, item);
  case A1C_MajorType_array:
    return A1C_Decoder_decodeArray(decoder, ptr, end, header, item);
  case A1C_MajorType_map:
    return A1C_Decoder_decodeMap(decoder, ptr, end, header, item);
  case A1C_MajorType_tag:
    return A1C_Decoder_decodeTag(decoder, ptr, end, header, item);
  case A1C_MajorType_special:
    return A1C_Decoder_decodeSpecial(decoder, ptr, end, header, item);
  }
}

A1C_Item *A1C_Decoder_decode(A1C_Decoder *decoder, const uint8_t *data,
                             size_t size) {
  A1C_Decoder_reset(decoder, data);
  if (data == NULL) {
    decoder->error.type = A1C_ErrorType_truncated;
    decoder->error.srcPos = 0;
    return NULL;
  }
  return A1C_Decoder_decodeOne(decoder, data, data + size);
}

////////////////////////////////////////
// Encoder
////////////////////////////////////////

void A1C_Encoder_init(A1C_Encoder *encoder, A1C_Encoder_WriteCallback write,
                      void *opaque) {
  memset(encoder, 0, sizeof(*encoder));
  encoder->write = write;
  encoder->opaque = opaque;
}

static void A1C_Encoder_reset(A1C_Encoder *encoder) {
  memset(&encoder->error, 0, sizeof(A1C_Error));
  encoder->bytesWritten = 0;
}

bool A1C_Encoder_encodeOne(A1C_Encoder *encoder, const A1C_Item *item);

#define A1C_Encoder_error(errorType)                                           \
  do {                                                                         \
    encoder->error.type = (errorType);                                         \
    encoder->error.srcPos = encoder->bytesWritten;                             \
    encoder->error.item = encoder->currentItem;                                \
    return false;                                                              \
  } while (0)

static bool A1C_Encoder_write(A1C_Encoder *encoder, const void *data,
                              size_t size) {
  if (size == 0) {
    return true;
  }
  const size_t written = encoder->write(encoder->opaque, data, size);
  encoder->bytesWritten += written;

  if (written < size) {
    A1C_Encoder_error(A1C_ErrorType_writeFailed);
  }
  return true;
}

bool A1C_Encoder_writeInt(A1C_Encoder *encoder, const void *data, size_t size) {
  uint64_t val = 0;
  memcpy(&val, data, size);
  A1C_byteswap(&val, size);
  return A1C_Encoder_write(encoder, data, size);
}

static bool A1C_Encoder_encodeHeaderAndCount(A1C_Encoder *encoder,
                                             A1C_MajorType majorType,
                                             uint64_t count) {
  assert(majorType != A1C_MajorType_special);
  uint8_t shortCount;
  if (count < 24) {
    shortCount = (uint8_t)count;
  } else if (count <= UINT8_MAX) {
    shortCount = 24;
  } else if (count <= UINT16_MAX) {
    shortCount = 25;
  } else if (count <= UINT32_MAX) {
    shortCount = 26;
  } else {
    shortCount = 27;
  }
  A1C_ItemHeader header = A1C_ItemHeader_make(majorType, shortCount);
  if (!A1C_Encoder_write(encoder, &header, sizeof(header))) {
    return false;
  }
  if (shortCount == 24) {
    if (!A1C_Encoder_writeInt(encoder, &count, 1)) {
      return false;
    }
  } else if (shortCount == 25) {
    if (!A1C_Encoder_writeInt(encoder, &count, 2)) {
      return false;
    }
  } else if (shortCount == 26) {
    if (!A1C_Encoder_writeInt(encoder, &count, 4)) {
      return false;
    }
  } else if (shortCount == 27) {
    if (!A1C_Encoder_writeInt(encoder, &count, 8)) {
      return false;
    }
  }
  return true;
}

static bool A1C_Encoder_encodeInt(A1C_Encoder *encoder, const A1C_Item *item) {
  assert(item->type == A1C_ItemType_uint64 || item->type == A1C_ItemType_int64);
  A1C_MajorType majorType;
  uint64_t value;
  if (item->type == A1C_ItemType_uint64 || item->int64 >= 0) {
    majorType = A1C_MajorType_uint;
    value = item->uint64;
  } else {
    majorType = A1C_MajorType_int;
    value = (uint64_t)-item->int64;
  }
  return A1C_Encoder_encodeHeaderAndCount(encoder, majorType, value);
}

static bool A1C_Encoder_encodeData(A1C_Encoder *encoder, const A1C_Item *item) {
  assert(item->type == A1C_ItemType_bytes || item->type == A1C_ItemType_string);
  const A1C_MajorType majorType = item->type == A1C_ItemType_bytes
                                      ? A1C_MajorType_bytes
                                      : A1C_MajorType_string;
  const size_t count =
      item->type == A1C_ItemType_bytes ? item->bytes.size : item->string.size;
  if (!A1C_Encoder_encodeHeaderAndCount(encoder, majorType, count)) {
    return false;
  }
  const void *data = item->type == A1C_ItemType_bytes
                         ? (const void *)item->bytes.data
                         : (const void *)item->string.data;
  A1C_Encoder_write(encoder, data, count);
  return true;
}

static bool A1C_Encoder_encodeArray(A1C_Encoder *encoder,
                                    const A1C_Item *item) {
  assert(item->type == A1C_ItemType_array);
  const size_t count = item->array.size;
  if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_array, count)) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    const A1C_Item *child = &item->array.items[i];
    if (!A1C_Encoder_encodeOne(encoder, child)) {
      return false;
    }
  }
  return true;
}

static bool A1C_Encoder_encodeMap(A1C_Encoder *encoder, const A1C_Item *item) {
  assert(item->type == A1C_ItemType_map);
  const size_t count = item->map.size;
  if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_map, count)) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    const A1C_Item *key = &item->map.keys[i];
    const A1C_Item *value = &item->map.values[i];
    if (!A1C_Encoder_encodeOne(encoder, key)) {
      return false;
    }
    if (!A1C_Encoder_encodeOne(encoder, value)) {
      return false;
    }
  }
  return true;
}

static bool A1C_Encoder_encodeTag(A1C_Encoder *encoder, const A1C_Item *item) {
  assert(item->type == A1C_ItemType_tag);
  const A1C_Tag *tag = &item->tag;
  if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_tag, tag->tag)) {
    return false;
  }
  if (!A1C_Encoder_encodeOne(encoder, tag->item)) {
    return false;
  }
  return true;
}

static bool A1C_Encoder_encodeSimple(A1C_Encoder *encoder, uint8_t value) {
  if (value < 20) {
    A1C_Encoder_error(A1C_ErrorType_invalidSimpleValue);
  } else if (value < 24 || value >= 32) {
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special,
                                            (uint64_t)value);
  } else {
    A1C_Encoder_error(A1C_ErrorType_invalidSimpleValue);
  }
}

static bool A1C_Encoder_encodeSpecial(A1C_Encoder *encoder,
                                      const A1C_Item *item) {
  if (item->type == A1C_ItemType_false) {
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 20);
  } else if (item->type == A1C_ItemType_true) {
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 21);
  } else if (item->type == A1C_ItemType_null) {
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 22);
  } else if (item->type == A1C_ItemType_undefined) {
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 23);
  } else if (item->type == A1C_ItemType_simple) {
    if (item->simple >= 20 && item->simple < 32) {
      A1C_Encoder_error(A1C_ErrorType_invalidSimpleValue);
    }
    return A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special,
                                            item->simple);
  } else if (item->type == A1C_ItemType_float32) {
    if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 25)) {
      return false;
    }
    return A1C_Encoder_writeInt(encoder, &item->float32, sizeof(item->float32));
  } else if (item->type == A1C_ItemType_float64) {
    if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_special, 26)) {
      return false;
    }
    return A1C_Encoder_writeInt(encoder, &item->float64, sizeof(item->float64));
  } else {
    assert(false);
    return false;
  }
}

bool A1C_Encoder_encodeOne(A1C_Encoder *encoder, const A1C_Item *item) {
  encoder->currentItem = item;
  switch (item->type) {
  case A1C_ItemType_uint64:
  case A1C_ItemType_int64:
    return A1C_Encoder_encodeInt(encoder, item);
  case A1C_ItemType_bytes:
  case A1C_ItemType_string:
    return A1C_Encoder_encodeData(encoder, item);
  case A1C_ItemType_array:
    return A1C_Encoder_encodeArray(encoder, item);
  case A1C_ItemType_map:
    return A1C_Encoder_encodeMap(encoder, item);
  case A1C_ItemType_tag:
    return A1C_Encoder_encodeTag(encoder, item);
  case A1C_ItemType_false:
  case A1C_ItemType_true:
  case A1C_ItemType_null:
  case A1C_ItemType_undefined:
  case A1C_ItemType_float32:
  case A1C_ItemType_float64:
  case A1C_ItemType_simple:
    return A1C_Encoder_encodeSpecial(encoder, item);
  case A1C_ItemType_invalid:
    A1C_Encoder_error(A1C_ErrorType_invalidItemType);
    break;
  }
}

bool A1C_Encoder_encode(A1C_Encoder *encoder, const A1C_Item *item) {
  A1C_Encoder_reset(encoder);
  return A1C_Encoder_encodeOne(encoder, item);
}

////////////////////////////////////////
// Simple Encoder
////////////////////////////////////////

static size_t A1C_noopWrite(void *opaque, const uint8_t *data, size_t size) {
  (void)opaque;
  (void)data;
  return size;
}

size_t A1C_Item_encodedSize(const A1C_Item *item) {
  A1C_Encoder encoder;
  A1C_Encoder_init(&encoder, A1C_noopWrite, NULL);
  if (!A1C_Encoder_encode(&encoder, item)) {
    return 0;
  }
  return encoder.bytesWritten;
}

typedef struct {
  uint8_t *ptr;
  uint8_t *end;
} A1C_Buffer;

static size_t A1C_bufferWrite(void *opaque, const uint8_t *data, size_t size) {
  A1C_Buffer *buffer = (A1C_Buffer *)opaque;
  assert(buffer->ptr <= buffer->end);
  const size_t capacity = (size_t)(buffer->end - buffer->ptr);
  if (size > capacity) {
    size = capacity;
  }
  if (size > 0) {
    memcpy(buffer->ptr, data, size);
    buffer->ptr += size;
  }
  return size;
}

size_t A1C_Item_encode(const A1C_Item *item, uint8_t *dst, size_t dstCapacity,
                       A1C_Error *error) {
  A1C_Buffer buf = {
      .ptr = dst,
      .end = dst + dstCapacity,
  };
  A1C_Encoder encoder;
  A1C_Encoder_init(&encoder, A1C_bufferWrite, &buf);
  bool success = A1C_Encoder_encode(&encoder, item);
  assert(encoder.bytesWritten == (size_t)(buf.ptr - dst));
  if (success) {
    return encoder.bytesWritten;
  }
  if (error != NULL) {
    *error = encoder.error;
  }
  return 0;
}