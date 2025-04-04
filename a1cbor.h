#ifndef A1CBOR_H
#define A1CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////
// Portability
////////////////////////////////////////

#ifdef __has_attribute
#define A1C_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#define A1C_HAS_ATTRIBUTE(x) 0
#endif

#if A1C_HAS_ATTRIBUTE(warn_unused_result)
#define A1C_NODISCARD __attribute__((warn_unused_result))
#else
#define A1C_NODISCARD
#endif

////////////////////////////////////////
// Item
////////////////////////////////////////

typedef enum {
  A1C_ItemType_invalid = 0,
  A1C_ItemType_int64,
  A1C_ItemType_bytes,
  A1C_ItemType_string,
  A1C_ItemType_array,
  A1C_ItemType_map,
  A1C_ItemType_boolean,
  A1C_ItemType_null,
  A1C_ItemType_undefined,
  A1C_ItemType_float16,
  A1C_ItemType_float32,
  A1C_ItemType_float64,
  A1C_ItemType_simple,
  A1C_ItemType_tag,
} A1C_ItemType;

typedef int64_t A1C_Int64;
typedef bool A1C_Bool;
typedef uint16_t A1C_Float16;
typedef double A1C_Float64;
typedef float A1C_Float32;

typedef struct {
  const uint8_t *data;
  size_t size;
} A1C_Bytes;

typedef struct {
  const char *data;
  size_t size;
} A1C_String;

typedef struct {
  struct A1C_Item *keys;
  struct A1C_Item *values;
  size_t size;
} A1C_Map;

typedef struct {
  struct A1C_Item *items;
  size_t size;
} A1C_Array;

typedef struct {
  uint64_t tag;
  struct A1C_Item *item;
} A1C_Tag;

typedef uint8_t A1C_Simple;

typedef struct A1C_Item {
  A1C_ItemType type;
  union {
    A1C_Bool boolean;
    A1C_Int64 int64;
    A1C_Float16 float16;
    A1C_Float32 float32;
    A1C_Float64 float64;
    A1C_Bytes bytes;
    A1C_String string;
    A1C_Map map;
    A1C_Array array;
    A1C_Simple simple;
    A1C_Tag tag;
  };
  struct A1C_Item *parent;
} A1C_Item;

////////////////////////////////////////
// Errors
////////////////////////////////////////

typedef enum {
  A1C_ErrorType_ok = 0,
  A1C_ErrorType_badAlloc,
  A1C_ErrorType_truncated,
  A1C_ErrorType_invalidItemHeader,
  A1C_ErrorType_largeIntegersUnsupported,
  A1C_ErrorType_integerOverflow,
  A1C_ErrorType_invalidChunkedString,
  A1C_ErrorType_maxDepthExceeded,
  A1C_ErrorType_invalidSimpleEncoding,
  A1C_ErrorType_breakNotAllowed,
  A1C_ErrorType_writeFailed,
  A1C_ErrorType_invalidItemType,
  A1C_ErrorType_invalidSimpleValue,
  A1C_ErrorType_formatError,
  A1C_ErrorType_trailingData,
  A1C_ErrorType_jsonUTF8Unsupported,
} A1C_ErrorType;

typedef struct {
  A1C_ErrorType type;
  size_t srcPos;
  size_t depth;
  const A1C_Item *item;
  const char *file;
  int line;
} A1C_Error;

const char *A1C_ErrorType_getString(A1C_ErrorType type);

////////////////////////////////////////
// Arena
////////////////////////////////////////

typedef struct {
  /// Allocates and zeros memory of the given size. The memory must outlive any
  /// objects created by the library using this arena.
  /// @returns NULL on failure.
  void *(*calloc)(void *opaque, size_t bytes);
  /// Opaque pointer passed to alloc and calloc.
  void *opaque;
} A1C_Arena;

/// Arena wrapper that limits the number of bytes allocated.
typedef struct {
  A1C_Arena backingArena;
  size_t allocatedBytes;
  size_t limitBytes;
} A1C_LimitedArena;

/**
 * Creates a limited arena that won't allocate more than @p limitBytes.
 */
A1C_LimitedArena A1C_LimitedArena_init(A1C_Arena arena, size_t limitBytes);

/// Get an arena interface for the @p limitedArena.
A1C_Arena A1C_LimitedArena_arena(A1C_LimitedArena *limitedArena);

/// Reset the number of allocated bytes by the @p limitedArena.
/// @warning This does not free any memory.
void A1C_LimitedArena_reset(A1C_LimitedArena *limitedArena);

////////////////////////////////////////
// Decoder
////////////////////////////////////////

#define A1C_MAX_DEPTH_DEFAULT 32

typedef struct {
  A1C_LimitedArena limitedArena;
  A1C_Arena arena;

  A1C_Error error;
  const uint8_t *start;
  const uint8_t *ptr;
  const uint8_t *end;
  A1C_Item *parent;
  size_t depth;
  size_t maxDepth;
  bool referenceSource;
  bool rejectUnknownSimple;
} A1C_Decoder;

void A1C_Decoder_init(A1C_Decoder *decoder, A1C_Arena arena, size_t limitBytes,
                      bool referenceSource);

A1C_Item *A1C_NODISCARD A1C_Decoder_decode(A1C_Decoder *decoder,
                                           const uint8_t *data, size_t size);

////////////////////////////////////////
// Item Helpers
////////////////////////////////////////

const A1C_Item *A1C_Map_get(const A1C_Map *map, const A1C_Item *key);
const A1C_Item *A1C_Map_get_cstr(const A1C_Map *map, const char *key);
const A1C_Item *A1C_Map_get_int(const A1C_Map *map, A1C_Int64 key);

const A1C_Item *A1C_Array_get(const A1C_Array *array, size_t index);

bool A1C_Item_eq(const A1C_Item *a, const A1C_Item *b);

////////////////////////////////////////
// Creation
////////////////////////////////////////

A1C_Item *A1C_NODISCARD A1C_Item_root(A1C_Arena *arena);

void A1C_Item_int64(A1C_Item *item, A1C_Int64 value);
void A1C_Item_float16(A1C_Item *item, A1C_Float16 value);
void A1C_Item_float32(A1C_Item *item, A1C_Float32 value);
void A1C_Item_float64(A1C_Item *item, A1C_Float64 value);
void A1C_Item_boolean(A1C_Item *item, bool value);
void A1C_Item_null(A1C_Item *item);
void A1C_Item_undefined(A1C_Item *item);
A1C_Tag *A1C_NODISCARD A1C_Item_tag(A1C_Item *item, uint64_t tag,
                                    A1C_Arena *arena);
uint8_t *A1C_NODISCARD A1C_Item_bytes(A1C_Item *item, size_t size,
                                      A1C_Arena *arena);
bool A1C_NODISCARD A1C_Item_bytes_copy(A1C_Item *item, const uint8_t *data,
                                       size_t size, A1C_Arena *arena);
void A1C_Item_bytes_ref(A1C_Item *item, const uint8_t *data, size_t size);
char *A1C_NODISCARD A1C_Item_string(A1C_Item *item, size_t size,
                                    A1C_Arena *arena);
bool A1C_NODISCARD A1C_Item_string_copy(A1C_Item *item, const char *data,
                                        size_t size, A1C_Arena *arena);
bool A1C_NODISCARD A1C_Item_string_cstr(A1C_Item *item, const char *data,
                                        A1C_Arena *arena);
void A1C_Item_string_ref(A1C_Item *item, const char *data, size_t size);
void A1C_Item_string_refCStr(A1C_Item *item, const char *data);
A1C_Map *A1C_NODISCARD A1C_Item_map(A1C_Item *item, size_t size,
                                    A1C_Arena *arena);
A1C_Array *A1C_NODISCARD A1C_Item_array(A1C_Item *item, size_t size,
                                        A1C_Arena *arena);

////////////////////////////////////////
// Encoder
////////////////////////////////////////

typedef size_t (*A1C_Encoder_WriteCallback)(void *opaque, const uint8_t *data,
                                            size_t size);

typedef struct {
  A1C_Error error;
  uint64_t bytesWritten;
  const A1C_Item *currentItem;
  A1C_Encoder_WriteCallback write;
  void *opaque;
  size_t depth;
} A1C_Encoder;

void A1C_Encoder_init(A1C_Encoder *encoder, A1C_Encoder_WriteCallback write,
                      void *opaque);

bool A1C_NODISCARD A1C_Encoder_encode(A1C_Encoder *encoder,
                                      const A1C_Item *item);

/// @warning Fails on non-ascii strings
bool A1C_NODISCARD A1C_Encoder_json(A1C_Encoder *encoder, const A1C_Item *item);

////////////////////////////////////////
// Simple Encoder
////////////////////////////////////////

size_t A1C_NODISCARD A1C_Item_encodedSize(const A1C_Item *item);
size_t A1C_NODISCARD A1C_Item_encode(const A1C_Item *item, uint8_t *dst,
                                     size_t dstCapacity, A1C_Error *error);

#ifdef __cplusplus
}
#endif

#endif
