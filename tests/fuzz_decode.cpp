
#include <memory>
#include <vector>

#include <cbor.h>

#include "../a1cbor.h"

namespace {
using Ptrs = std::pair<size_t, std::vector<std::unique_ptr<uint8_t[]>>>;

void *testCalloc(void *opaque, size_t bytes) {
  auto ptrs = static_cast<Ptrs *>(opaque);
  if (bytes == 0) {
    return nullptr;
  }
  auto ptr = std::make_unique<uint8_t[]>(bytes);
  if (ptr == nullptr) {
    return nullptr;
  }
  memset(ptr.get(), 0, bytes);
  ptrs->second.push_back(std::move(ptr));
  ptrs->first += bytes;
  return ptrs->second.back().get();
}
size_t appendToString(void *opaque, const uint8_t *data, size_t size) {
  auto str = static_cast<std::string *>(opaque);
  if (size == 0) {
    return 0;
  }
  str->append(reinterpret_cast<const char *>(data), size);
  return size;
}

size_t writeToFile(void *opaque, const uint8_t *data, size_t size) {
  FILE *f = static_cast<FILE *>(opaque);
  return fwrite(data, 1, size, f);
}

size_t noop(void *opaque, const uint8_t *data, size_t size) {
  (void)opaque;
  (void)data;
  return size;
}

void fail(const char *message, const A1C_Item *item, A1C_Error error) {
  A1C_Encoder encoder;
  fprintf(stderr, "FAIL: %s\n", message);
  if (item != nullptr) {
    fprintf(stderr, "Item:\n");
    A1C_Encoder_init(&encoder, writeToFile, stderr);
    (void)A1C_Encoder_json(&encoder, item);
  }
  fprintf(stderr, "\nError: type=%s, srcPos=%zu, depth=%zu, file=%s, line=%d\n",
          A1C_ErrorType_getString(error.type), error.srcPos, error.depth,
          error.file, error.line);
  abort();
}

constexpr size_t kMemLimit = 1024 * 1024;

void *limitedAlloc(size_t size) {
  if (size < kMemLimit) {
    return malloc(size);
  } else {
    return NULL;
  }
}
void limitedFree(void *ptr) { free(ptr); }
void *limitedRealloc(void *ptr, size_t size) {
  if (size < kMemLimit) {
    return realloc(ptr, size);
  } else {
    free(ptr);
    return NULL;
  }
}

bool equal(const A1C_Item *a, const cbor_item_t *b) {
  switch (a->type) {
  case A1C_ItemType_uint64:
    if (!cbor_isa_uint(b)) {
      return false;
    }
    return a->uint64 == cbor_get_int(b);
  case A1C_ItemType_int64:
    if (!cbor_isa_negint(b)) {
      return false;
    }
    return a->int64 == (int64_t)~cbor_get_int(b);
  case A1C_ItemType_float16:
    if (!cbor_is_float(b) || cbor_float_get_width(b) != CBOR_FLOAT_16) {
      return false;
    }
    // No way to compare float16, assume equality
    return true;
  case A1C_ItemType_float32: {
    if (!cbor_is_float(b) || cbor_float_get_width(b) != CBOR_FLOAT_32) {
      return false;
    }
    float bFloat = cbor_float_get_float4(b);
    uint32_t aBits, bBits;
    memcpy(&aBits, &a->float64, sizeof(aBits));
    memcpy(&bBits, &bFloat, sizeof(bBits));
    return aBits == bBits;
  }
  case A1C_ItemType_float64: {
    if (!cbor_is_float(b) || cbor_float_get_width(b) != CBOR_FLOAT_64) {
      return false;
    }
    double bFloat = cbor_float_get_float8(b);
    uint64_t aBits, bBits;
    memcpy(&aBits, &a->float64, sizeof(aBits));
    memcpy(&bBits, &bFloat, sizeof(bBits));
    return aBits == bBits;
  }
  case A1C_ItemType_boolean:
    if (!cbor_is_bool(b)) {
      return false;
    }
    return a->boolean == cbor_get_bool(b);
  case A1C_ItemType_null:
    return cbor_is_null(b);
  case A1C_ItemType_undefined:
    return cbor_is_undef(b);
  case A1C_ItemType_simple:
    assert(false);
    return false;
  case A1C_ItemType_bytes: {
    if (!cbor_isa_bytestring(b)) {
      return false;
    }
    auto ptr = a->bytes.data;
    auto end = a->bytes.data + a->bytes.size;
    auto testChunk = [&](const cbor_item_t *chunk) {
      auto chunkPtr = cbor_bytestring_handle(chunk);
      auto size = cbor_bytestring_length(chunk);
      if (size > (size_t)(end - ptr)) {
        return false;
      }
      if (memcmp(ptr, chunkPtr, size) != 0) {
        return false;
      }
      ptr += size;
      return true;
    };
    if (cbor_bytestring_is_definite(b)) {
      testChunk(b);
    } else {
      auto chunks = cbor_bytestring_chunks_handle(b);
      size_t numChunks = cbor_bytestring_chunk_count(b);
      for (size_t i = 0; i < numChunks; ++i) {
        if (!testChunk(chunks[i])) {
          return false;
        }
      }
    }
    return ptr == end;
  }
  case A1C_ItemType_string: {
    if (!cbor_isa_string(b)) {
      return false;
    }
    auto ptr = a->string.data;
    auto end = a->string.data + a->string.size;
    auto testChunk = [&](const cbor_item_t *chunk) {
      auto chunkPtr = cbor_string_handle(chunk);
      auto size = cbor_string_length(chunk);
      if (size > (size_t)(end - ptr)) {
        return false;
      }
      if (memcmp(ptr, chunkPtr, size) != 0) {
        return false;
      }
      ptr += size;
      return true;
    };
    if (cbor_string_is_definite(b)) {
      testChunk(b);
    } else {
      auto chunks = cbor_string_chunks_handle(b);
      size_t numChunks = cbor_string_chunk_count(b);
      for (size_t i = 0; i < numChunks; ++i) {
        if (!testChunk(chunks[i])) {
          return false;
        }
      }
    }
    return ptr == end;
  }
  case A1C_ItemType_array:
    if (!cbor_isa_array(b)) {
      return false;
    }
    if (a->array.size != cbor_array_size(b)) {
      return false;
    }
    for (size_t i = 0; i < a->array.size; ++i) {
      const A1C_Item *childA = &a->array.items[i];
      cbor_item_t *childB = cbor_array_get(b, i);
      if (childB == NULL || !equal(childA, childB)) {
        return false;
      }
    }
    return true;
  case A1C_ItemType_map:
    if (!cbor_isa_map(b)) {
      return false;
    }
    if (a->map.size != cbor_map_size(b)) {
      return false;
    }
    for (size_t i = 0; i < a->map.size; ++i) {
      const A1C_Item *keyA = &a->map.keys[i];
      const A1C_Item *valueA = &a->map.values[i];
      auto childB = cbor_map_handle(b)[i];
      if (childB.key == NULL || !equal(keyA, childB.key)) {
        return false;
      }
      if (childB.value == NULL || !equal(valueA, childB.value)) {
        if (childB.value == NULL) {
        }
        return false;
      }
    }
    return true;
  case A1C_ItemType_tag:
    if (!cbor_isa_tag(b)) {
      return false;
    }
    if (a->tag.tag != cbor_tag_value(b)) {
      return false;
    }
    return equal(a->tag.item, cbor_tag_item(b));
  case A1C_ItemType_invalid:
    return false;
  }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  cbor_set_allocs(limitedAlloc, limitedRealloc, limitedFree);
  Ptrs ptrs{};
  A1C_Arena arena;
  arena.calloc = testCalloc;
  arena.opaque = &ptrs;

  if (size < 5) {
    return 0;
  }
  const bool referenceSource = data[--size];
  uint32_t limit = 0;
  memcpy(&limit, data + size - 4, sizeof(limit));
  size -= 4;
  A1C_Decoder decoder;
  A1C_Decoder_init(&decoder, arena, 0, referenceSource);

  auto item = A1C_Decoder_decode(&decoder, data, size);

  if (limit != 0) {
    Ptrs ptrs2{};
    A1C_Decoder decoder2;
    auto arena2 = arena;
    arena2.opaque = &ptrs2;
    A1C_Decoder_init(&decoder2, arena2, limit, referenceSource);
    auto item2 = A1C_Decoder_decode(&decoder2, data, size);
    if (ptrs2.first > limit) {
      fail("Allocation limit not respected", item2, decoder2.error);
    }
    if (item2 != NULL) {
      if (item == NULL) {
        fail("Adding limit made decoding pass", item2, decoder.error);
      }
      if (ptrs.first > limit || ptrs2.first > limit) {
        fail("Memory limit not respected", item, decoder.error);
      }
      if (!A1C_Item_strictEq(item, item2)) {
        fail("Strict equality failed with limit", item, decoder.error);
      }
    } else {
      if (decoder2.error.type == A1C_ErrorType_badAlloc) {
        if (ptrs.first <= limit) {
          fail("Got bad alloc without surpassing limit", item, decoder2.error);
        }
      } else if (item != NULL) {
        fail("Decoding failed with limit but original decoding passed", item,
             decoder2.error);
      } else if (decoder2.error.type != decoder.error.type ||
                 decoder2.error.line != decoder.error.line) {
        fail("Decoding failed with different error types when using limits",
             item, decoder2.error);
      }
    }
    if (item == NULL) {
      if (item2 != NULL) {
      }
    } else {
      if (item2 == NULL) {
      }
    }
  }

  cbor_load_result result;
  auto ref = cbor_load(data, size, &result);

  if (item == NULL) {
    if (decoder.error.type != A1C_ErrorType_largeNegativeIntegerUnsupported &&
        decoder.error.type != A1C_ErrorType_maxDepthExceeded) {
      if (ref != NULL && result.read == size) {
        fail("Decoding failed but libcbor succeeded", nullptr, decoder.error);
      }
    }
    return 0;
  }

  if (ref == NULL) {
    auto decoder2 = decoder;
    decoder2.rejectUnknownSimple = true;
    // libcbor rejects unknown simple values
    auto item2 = A1C_Decoder_decode(&decoder2, data, size);
    if (item2 != NULL) {
      fprintf(stderr, "libcbor error %d\n", result.error.code);
      fail("Decoding succeeded but libcbor failed", item, decoder2.error);
    }
  }

  A1C_Encoder encoder;
  std::string str;
  A1C_Encoder_init(&encoder, appendToString, &str);
  if (!A1C_Encoder_encode(&encoder, item)) {
    fail("Encoding failed!", item, encoder.error);
  }

  A1C_Encoder_init(&encoder, noop, nullptr);
  if (!A1C_Encoder_json(&encoder, item)) {
    if (encoder.error.type != A1C_ErrorType_jsonUTF8Unsupported) {
      fail("JSON failed!", item, encoder.error);
    }
  }

  auto item2 =
      A1C_Decoder_decode(&decoder, (const uint8_t *)str.data(), str.size());
  if (item2 == NULL) {
    fail("Decoding re-encoded data failed!", item, decoder.error);
  }

  if (!A1C_Item_strictEq(item, item2)) {
    fail("Strict equality failed after encoding/decoding", item, decoder.error);
  }
  if (!A1C_Item_eq(item, item2)) {
    fail("Equality failed after encoding/decoding", item, decoder.error);
  }

  if (ref != nullptr && !equal(item, ref)) {
    fail("Decoded item does not match libcbor's decoded value", item,
         decoder.error);
  }

  return 0;
}