
#include "./a1cbor.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

////////////////////////////////////////
// Constants
////////////////////////////////////////

static const uint64_t A1C_kNoOpTag = 55799;

////////////////////////////////////////
// Utilities
////////////////////////////////////////

#ifndef __has_builtin
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

static void *A1C_Arena_calloc(A1C_Arena *arena, size_t count, size_t size) {
  size_t bytes;
  if (A1C_overflowMul(count, size, &bytes)) {
    return NULL;
  }
  if (A1C_overflowAdd(arena->allocatedBytes, bytes, &arena->allocatedBytes)) {
    return NULL;
  }
  if (arena->limitBytes > 0 && arena->allocatedBytes > arena->limitBytes) {
    return NULL;
  }
  return arena->calloc(arena->opaque, bytes);
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

A1C_Bytes *A1C_Item_bytes(A1C_Item *item, size_t size, A1C_Arena *arena) {
  uint8_t *data = A1C_Arena_calloc(arena, size, 1);
  if (data == NULL && size > 0) {
    return NULL;
  }

  A1C_Item_bytes_ref(item, data, size);

  return &item->bytes;
}

A1C_Bytes *A1C_Item_bytes_copy(A1C_Item *item, const uint8_t *data, size_t size,
                               A1C_Arena *arena) {
  A1C_Bytes *bytes = A1C_Item_bytes(item, size, arena);
  if (bytes != NULL && size > 0) {
    memcpy(bytes->data, data, size);
  }
  return bytes;
}

void A1C_Item_bytes_ref(A1C_Item *item, uint8_t *data, size_t size) {
  item->type = A1C_ItemType_bytes;
  item->bytes.data = data;
  item->bytes.size = size;
}

A1C_String *A1C_Item_string(A1C_Item *item, size_t size, A1C_Arena *arena) {
  char *data = A1C_Arena_calloc(arena, size, 1);
  if (data == NULL && size > 0) {
    return NULL;
  }

  A1C_Item_string_ref(item, data, size);

  return &item->string;
}

A1C_String *A1C_Item_string_copy(A1C_Item *item, const char *data, size_t size,
                                 A1C_Arena *arena) {
  A1C_String *string = A1C_Item_string(item, size, arena);
  if (string != NULL && size > 0) {
    memcpy(string->data, data, size);
  }
  return string;
}

void A1C_Item_string_ref(A1C_Item *item, char *data, size_t size) {
  item->type = A1C_ItemType_string;
  item->string.data = data;
  item->string.size = size;
}

A1C_Map *A1C_Item_map(A1C_Item *item, size_t size, A1C_Arena *arena) {
  A1C_Item *items = A1C_Arena_calloc(arena, size, 2 * sizeof(A1C_Item));
  if (items == NULL && size > 0) {
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
  if (items == NULL && size > 0) {
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

void A1C_Decoder_init(A1C_Decoder *decoder, A1C_Arena arena) {
  memset(decoder, 0, sizeof(A1C_Decoder));
  assert(arena.calloc != NULL);
  decoder->arena = arena;
}

static void A1C_Decoder_reset(A1C_Decoder *decoder, const uint8_t *start) {
  memset(&decoder->error, 0, sizeof(A1C_Error));
  decoder->start = start;
  decoder->parent = NULL;
  decoder->depth = 0;
  if (decoder->maxDepth == 0) {
    decoder->maxDepth = A1C_MAX_DEPTH_DEFAULT;
  }
  decoder->arena.allocatedBytes = 0;
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
    if (data == NULL && totalSize > 0) {
      A1C_Decoder_error(A1C_ErrorType_badAlloc);
    }
    uint8_t *dataEnd = data + totalSize;
    while (previous != NULL) {
      const uint8_t *chunkPtr = majorType == A1C_MajorType_bytes
                                    ? previous->bytes.data
                                    : (uint8_t *)previous->string.data;
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
    if (data == NULL && singleSize > 0) {
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