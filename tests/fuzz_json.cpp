
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "../a1cbor.h"

namespace {
using Ptrs = std::vector<std::unique_ptr<uint8_t[]>>;

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
  ptrs->push_back(std::move(ptr));
  return ptrs->back().get();
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

bool hasNonAscii(const nlohmann::json &a) {
  if (a.is_string()) {
    auto aStr = a.get<std::string>();
    for (auto c : aStr) {
      if (!isascii(c)) {
        return true;
      }
    }
    return false;
  }
  if (a.is_array()) {
    for (size_t i = 0; i < a.size(); ++i) {
      if (hasNonAscii(a[i])) {
        return true;
      }
    }
    return false;
  }
  if (a.is_object()) {
    for (const auto &it : a.items()) {
      if (hasNonAscii(it.key()) || hasNonAscii(it.value())) {
        return true;
      }
    }
    return false;
  }
  return false;
}
bool approxEq(const nlohmann::json &a, const nlohmann::json &b) {
  if (a == b) {
    return true;
  }
  if (a.is_number_float() && b.is_number()) {
    return true;
  }
  if (a.is_number() && b.is_number_float()) {
    return true;
  }
  if (a.is_array() && b.is_array()) {
    if (a.size() != b.size()) {
      return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
      if (!approxEq(a[i], b[i])) {
        return false;
      }
    }
    return true;
  }
  if (a.is_object() && b.is_object()) {
    if (a.size() != b.size()) {
      return false;
    }
    for (const auto &it : a.items()) {
      auto jt = b.find(it.key());
      if (jt == b.end() || !approxEq(it.value(), jt.value())) {
        // Key not found or values do not match
        return false;
      }
    }
    return true;
  }
  return false;
}

// Sanitize floating point values, because they will differ
void sanitize(A1C_Item *item) {
  switch (item->type) {
  case A1C_ItemType_int64:
  case A1C_ItemType_float16:
  case A1C_ItemType_boolean:
  case A1C_ItemType_null:
  case A1C_ItemType_undefined:
  case A1C_ItemType_simple:
  case A1C_ItemType_bytes:
  case A1C_ItemType_string:
  case A1C_ItemType_tag:
  case A1C_ItemType_invalid:
    return;
  case A1C_ItemType_float32:
    item->float32 = int64_t(item->float32);
    return;
  case A1C_ItemType_float64:
    item->float64 = int64_t(item->float64);
    return;
  case A1C_ItemType_array:
    for (size_t i = 0; i < item->array.size; ++i) {
      sanitize(&item->array.items[i]);
    }
    return;
  case A1C_ItemType_map:
    for (size_t i = 0; i < item->map.size; ++i) {
      sanitize(&item->map.keys[i]);
      sanitize(&item->map.values[i]);
    }
    return;
  }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  nlohmann::json json;
  json = nlohmann::json::parse((const char *)data, (const char *)data + size,
                               nullptr, false);
  if (json.is_discarded()) {
    return 0;
  }

  auto cbor = nlohmann::json::to_cbor(json);

  Ptrs ptrs;
  A1C_Arena arena;
  arena.calloc = testCalloc;
  arena.opaque = &ptrs;
  A1C_Decoder decoder;
  A1C_Decoder_init(&decoder, arena, 0, 0);

  auto item = A1C_Decoder_decode(&decoder, cbor.data(), cbor.size());

  if (item == NULL) {
    if (decoder.error.type != A1C_ErrorType_maxDepthExceeded &&
        decoder.error.type != A1C_ErrorType_largeIntegersUnsupported) {
      fail("Decoding failed", nullptr, decoder.error);
    }
    return 0;
  }

  A1C_Encoder encoder;
  std::string str;
  A1C_Encoder_init(&encoder, appendToString, &str);

  if (!A1C_Encoder_encode(&encoder, item)) {
    fail("Encoding failed!", item, encoder.error);
  }

  auto json2 = nlohmann::json::from_cbor(str.begin(), str.end(), true, false);
  if (json2.is_discarded() || json != json2) {
    fprintf(stderr, "Original JSON: %s\n", json.dump(2).c_str());
    fprintf(stderr, "RoundTrip JSON: %s\n", json2.dump(2).c_str());
    fail("Json failed to round trip in CBOR", item, decoder.error);
  }

  str.clear();
  if (!A1C_Encoder_json(&encoder, item)) {
    if (encoder.error.type == A1C_ErrorType_jsonUTF8Unsupported) {
      if (!hasNonAscii(json)) {
        fail("JSON encoding failed due to UTF8 unsupported but no non-ascii "
             "found",
             item, encoder.error);
      }
    } else {
      fail("JSON encoding failed!", item, encoder.error);
    }
  }

  auto json3 = nlohmann::json::parse(str.begin(), str.end(), nullptr, false);

  if (json3.is_discarded() || !approxEq(json, json3)) {
    if (hasNonAscii(json)) {
      // JSON printing is for debugging, we don't get unicode right
      return 0;
    }
    fprintf(stderr, "Original JSON: %s\n", json.dump(2).c_str());
    fprintf(stderr, "String JSON:   %s\n", str.c_str());
    fprintf(stderr, "RoundTrip JSON: %s\n", json3.dump(2).c_str());
    fail("Json failed to round trip", item, decoder.error);
  }

  return 0;
}