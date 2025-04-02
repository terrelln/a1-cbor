
#include "../a1cbor.h"

#include <memory>
#include <string.h>

#include <gtest/gtest.h>

bool operator==(const A1C_Item &a, const A1C_Item &b) {
  return A1C_Item_strictEq(&a, &b);
}
bool operator!=(const A1C_Item &a, const A1C_Item &b) { return !(a == b); }

namespace {
using namespace ::testing;

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
} // namespace

class A1CBorTest : public Test {
protected:
  void SetUp() override {
    arena.calloc = testCalloc;
    arena.opaque = &ptrs;
  }

  std::string printError(std::string msg, A1C_Error error) {
    msg += ": ";
    msg += "type=";
    msg += A1C_ErrorType_getString(error.type);
    msg += ", srcPos=" + std::to_string(error.srcPos);
    msg += ", depth=" + std::to_string(error.depth);
    msg += ", file=";
    msg += error.file;
    msg += ", line=" + std::to_string(error.line);
    return msg;
  }

  std::string encode(const A1C_Item *item) {
    std::string str;
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, appendToString, &str);
    if (!A1C_Encoder_encode(&encoder, item)) {
      throw std::runtime_error{printError("Encoding failed", encoder.error)};
    }
    EXPECT_EQ(str.size(), A1C_Item_encodedSize(item));
    std::string string2;
    string2.resize(str.size());
    EXPECT_EQ(A1C_Item_encode(item, reinterpret_cast<uint8_t *>(&string2[0]),
                              string2.size(), nullptr),
              string2.size());
    EXPECT_EQ(str, string2);
    return str;
  }

  std::string json(const A1C_Item *item) {
    std::string str;
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, appendToString, &str);
    if (!A1C_Encoder_json(&encoder, item)) {
      throw std::runtime_error{printError("JSON Encoding failed", encoder.error)};
    }
    return str;
  }

  A1C_Item *decode(const std::string &data, size_t limit = 0,
                   bool referenceSource = false) {
    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, arena, limit, referenceSource);
    A1C_Item *item = A1C_Decoder_decode(
        &decoder, reinterpret_cast<const uint8_t *>(data.data()), data.size());
    if (item == nullptr) {
      throw std::runtime_error{printError("Decoding failed", decoder.error)};
    }
    return item;
  }

  void TearDown() override { ptrs.clear(); }

  A1C_Arena arena;
  Ptrs ptrs;
};

TEST_F(A1CBorTest, UInt64) {
  auto testValue = [this](uint64_t value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_uint64(item, value);
    ASSERT_EQ(item->type, A1C_ItemType_uint64);
    ASSERT_EQ(item->uint64, value);
    ASSERT_EQ(item->parent, nullptr);
    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(decoded->parent, nullptr);
    ASSERT_EQ(*item, *decoded);
  };

  testValue(0);
  testValue(42);
  testValue(UINT8_MAX);
  testValue(UINT16_MAX);
  testValue(UINT32_MAX);
  testValue(UINT64_MAX);

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_uint64(item1, 0);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    A1C_Item_uint64(item2, 1);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Int64) {
  auto testValue = [this](int64_t value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_int64(item, value);
    ASSERT_EQ(item->type, A1C_ItemType_int64);
    ASSERT_EQ(item->int64, value);
    ASSERT_EQ(item->parent, nullptr);
    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(decoded->parent, nullptr);
    ASSERT_EQ(decoded->type,
              value >= 0 ? A1C_ItemType_uint64 : A1C_ItemType_int64);
    ASSERT_EQ(item->int64, value);
    if (value >= 0) {
      ASSERT_NE(*item, *decoded);
    } else {
      ASSERT_EQ(*item, *decoded);
    }
    ASSERT_TRUE(A1C_Item_eq(item, decoded));
  };

  testValue(0);
  testValue(42);
  testValue(UINT8_MAX);
  testValue(UINT16_MAX);
  testValue(UINT32_MAX);
  testValue(UINT64_MAX);

  testValue(-1);
  testValue(-UINT8_MAX);
  testValue(-UINT8_MAX - 1);
  testValue(-UINT16_MAX);
  testValue(-UINT16_MAX - 1);
  testValue(-UINT32_MAX);
  testValue(-UINT32_MAX - 1);
  testValue(INT64_MIN);

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_int64(item1, -1);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    A1C_Item_int64(item2, -2);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Float32) {
  auto testValue = [this](float value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_float32(item, value);
    ASSERT_EQ(item->type, A1C_ItemType_float32);
    if (!isnan(value)) {
      ASSERT_EQ(item->float32, value);
    } else {
      ASSERT_TRUE(isnan(item->float32));
    }
    ASSERT_EQ(item->parent, nullptr);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
  };

  testValue(0.0);
  testValue(1e10);
  testValue(-1e10);
  testValue(std::numeric_limits<float>::quiet_NaN());
  testValue(std::numeric_limits<float>::signaling_NaN());
  testValue(std::numeric_limits<float>::infinity());

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_float32(item1, 1.0);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    A1C_Item_float32(item2, 2.0);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Float64) {
  auto testValue = [this](float value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_float64(item, value);
    ASSERT_EQ(item->type, A1C_ItemType_float64);
    if (!isnan(value)) {
      ASSERT_EQ(item->float64, value);
    } else {
      ASSERT_TRUE(isnan(item->float64));
    }
    ASSERT_EQ(item->parent, nullptr);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
  };

  testValue(0.0);
  testValue(1e10);
  testValue(-1e10);
  testValue(std::numeric_limits<float>::quiet_NaN());
  testValue(std::numeric_limits<float>::signaling_NaN());
  testValue(std::numeric_limits<float>::infinity());

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_float64(item1, 1.0);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    A1C_Item_float64(item2, 2.0);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Boolean) {
  auto testValue = [this](bool value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_boolean(item, value);
    ASSERT_EQ(item->type, A1C_ItemType_boolean);
    ASSERT_EQ(item->boolean, value);
    ASSERT_EQ(item->parent, nullptr);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
  };

  testValue(false);
  testValue(true);
  testValue(100);

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_boolean(item1, true);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    A1C_Item_boolean(item2, false);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Undefined) {
  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);
  A1C_Item_undefined(item);
  ASSERT_EQ(item->type, A1C_ItemType_undefined);
  ASSERT_EQ(item->parent, nullptr);

  auto encoded = encode(item);
  auto decoded = decode(encoded);
  ASSERT_EQ(*item, *decoded);
  ASSERT_EQ(decoded->parent, nullptr);
}

TEST_F(A1CBorTest, Null) {
  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);
  A1C_Item_null(item);
  ASSERT_EQ(item->type, A1C_ItemType_null);
  ASSERT_EQ(item->parent, nullptr);

  auto encoded = encode(item);
  auto decoded = decode(encoded);
  ASSERT_EQ(*item, *decoded);
  ASSERT_EQ(decoded->parent, nullptr);
}

TEST_F(A1CBorTest, Tag) {
  auto testValue = [this](A1C_UInt64 value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    auto tag = A1C_Item_tag(item, value, &arena);
    ASSERT_NE(tag, nullptr);
    ASSERT_EQ(item->type, A1C_ItemType_tag);
    ASSERT_EQ(tag->tag, value);
    ASSERT_NE(tag->item, nullptr);
    ASSERT_EQ(tag->item->parent, item);
    ASSERT_EQ(item->parent, nullptr);

    A1C_Item_null(tag->item);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(decoded->type, A1C_ItemType_tag);
    ASSERT_EQ(decoded->tag.item->type, A1C_ItemType_null);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
    ASSERT_EQ(decoded->tag.item->parent, decoded);

    {
      auto item1 = A1C_Item_root(&arena);
      ASSERT_NE(item1, nullptr);
      auto tag1 = A1C_Item_tag(item1, 1, &arena);
      ASSERT_NE(tag1, nullptr);
      A1C_Item_null(tag1->item);
      auto item2 = A1C_Item_root(&arena);
      ASSERT_NE(item2, nullptr);
      auto tag2 = A1C_Item_tag(item2, 2, &arena);
      ASSERT_NE(tag2, nullptr);
      A1C_Item_null(tag2->item);
      ASSERT_NE(*item1, *item2);
    }
    {
      auto item1 = A1C_Item_root(&arena);
      ASSERT_NE(item1, nullptr);
      auto tag1 = A1C_Item_tag(item1, 1, &arena);
      ASSERT_NE(tag1, nullptr);
      A1C_Item_null(tag1->item);
      auto item2 = A1C_Item_root(&arena);
      ASSERT_NE(item2, nullptr);
      auto tag2 = A1C_Item_tag(item2, 1, &arena);
      ASSERT_NE(tag2, nullptr);
      A1C_Item_undefined(tag2->item);
      ASSERT_NE(*item1, *item2);
    }
  };

  testValue(0);
  testValue(100);
  testValue(55799);
  testValue(UINT8_MAX);
  testValue(UINT16_MAX);
  testValue(UINT32_MAX);
  testValue(UINT64_MAX);
}

TEST_F(A1CBorTest, Bytes) {
  auto testValue = [this](const std::string &value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    const uint8_t *data = reinterpret_cast<const uint8_t *>(value.data());
    size_t size = value.size();
    A1C_Item_bytes_ref(item, data, size);
    ASSERT_EQ(item->type, A1C_ItemType_bytes);
    ASSERT_EQ(memcmp(item->bytes.data, data, size), 0);
    ASSERT_EQ(item->bytes.size, size);
    ASSERT_EQ(item->parent, nullptr);
    ASSERT_EQ(item->bytes.data, data);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    ASSERT_TRUE(A1C_Item_bytes_copy(item, data, size, &arena));
    ASSERT_EQ(item->type, A1C_ItemType_bytes);
    ASSERT_EQ(memcmp(item->bytes.data, data, size), 0);
    ASSERT_EQ(item->bytes.size, size);
    ASSERT_EQ(item->parent, nullptr);
    ASSERT_NE(item->bytes.data, data);
  };

  testValue("");
  testValue("hello");
  testValue("world");
  testValue("this is a longer string that doesn't fit in one character");
  testValue(std::string(1000, 'a'));
  testValue(std::string(100000, 'a'));

  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);

  A1C_Item_bytes_ref(item, nullptr, 0);
  auto encoded = encode(item);
  auto decoded = decode(encoded);
  ASSERT_EQ(*item, *decoded);

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_bytes_ref(item1, nullptr, 0);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    uint8_t data = 5;
    A1C_Item_bytes_ref(item2, &data, 1);
    ASSERT_NE(*item1, *item2);
  }
  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    uint8_t data1 = 4;
    A1C_Item_bytes_ref(item1, &data1, 1);
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    uint8_t data2 = 5;
    A1C_Item_bytes_ref(item2, &data2, 1);
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, String) {
  auto testValue = [this](const std::string &value) {
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    const char *data = value.data();
    size_t size = value.size();
    A1C_Item_string_ref(item, data, size);
    ASSERT_EQ(item->type, A1C_ItemType_string);
    ASSERT_EQ(memcmp(item->string.data, data, size), 0);
    ASSERT_EQ(item->string.size, size);
    ASSERT_EQ(item->parent, nullptr);
    ASSERT_EQ(item->string.data, data);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    ASSERT_TRUE(A1C_Item_string_copy(item, data, size, &arena));
    ASSERT_EQ(item->type, A1C_ItemType_string);
    ASSERT_EQ(memcmp(item->string.data, data, size), 0);
    ASSERT_EQ(item->string.size, size);
    ASSERT_EQ(item->parent, nullptr);
    ASSERT_NE(item->string.data, data);
  };

  testValue("");
  testValue("hello");
  testValue("world");
  testValue("this is a longer string that doesn't fit in one character");
  testValue(std::string(1000, 'a'));
  testValue(std::string(100000, 'a'));

  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);

  A1C_Item_string_ref(item, nullptr, 0);
  auto encoded = encode(item);
  auto decoded = decode(encoded);
  ASSERT_EQ(*item, *decoded);

  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_string_refCStr(item1, "x");
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    uint8_t data = 5;
    A1C_Item_string_refCStr(item2, "y");
    ASSERT_NE(*item1, *item2);
  }
  {
    auto item1 = A1C_Item_root(&arena);
    ASSERT_NE(item1, nullptr);
    A1C_Item_string_refCStr(item1, "short");
    auto item2 = A1C_Item_root(&arena);
    ASSERT_NE(item2, nullptr);
    uint8_t data = 5;
    A1C_Item_string_refCStr(item2, "longer string");
    ASSERT_NE(*item1, *item2);
  }
}

TEST_F(A1CBorTest, Map) {
  auto testMap = [this](const A1C_Item *map) {
    ASSERT_EQ(map->parent, nullptr);
    ASSERT_EQ(map->type, A1C_ItemType_map);
    const A1C_Map &m = map->map;

    for (size_t i = 0; i < m.size; ++i) {
      auto item = A1C_Map_get(&m, &m.keys[i]);
      ASSERT_NE(item, nullptr);
      ASSERT_EQ(item, &m.values[i]);

      ASSERT_EQ(m.keys[i].parent, map);
      ASSERT_EQ(m.values[i].parent, map);
    }

    auto encoded = encode(map);
    auto decoded = decode(encoded);
    ASSERT_EQ(*map, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    for (size_t i = 0; i < m.size; ++i) {
      ASSERT_EQ(decoded->map.keys[i].parent, decoded);
      ASSERT_EQ(decoded->map.values[i].parent, decoded);
    }
  };

  {
    auto map = A1C_Item_root(&arena);
    auto m = A1C_Item_map(map, 0, &arena);
    ASSERT_NE(map, nullptr);
    testMap(map);
  }

  A1C_Item *map1;
  {
    auto map = A1C_Item_root(&arena);
    auto m = A1C_Item_map(map, 1, &arena);
    ASSERT_NE(map, nullptr);
    A1C_Item_uint64(m->keys + 0, 42);
    A1C_Item_uint64(m->values + 0, 42);
    testMap(map);
    EXPECT_NE(A1C_Map_get_int(m, 42), nullptr);
    EXPECT_EQ(A1C_Map_get_cstr(m, "key1"), nullptr);
    EXPECT_EQ(A1C_Map_get_int(m, 0), nullptr);
    EXPECT_EQ(A1C_Map_get_int(m, -5), nullptr);
    map1 = map;
  }
  A1C_Item *map2;
  {
    auto map = A1C_Item_root(&arena);
    auto m = A1C_Item_map(map, 2, &arena);
    ASSERT_NE(map, nullptr);
    A1C_Item_string_refCStr(m->keys + 0, "key1");
    A1C_Item_string_refCStr(m->values + 0, "value1");
    A1C_Item_string_refCStr(m->keys + 1, "key2");
    A1C_Item_string_refCStr(m->values + 1, "value2");
    testMap(map);
    auto item = A1C_Map_get_cstr(m, "key1");
    ASSERT_NE(item, nullptr);
    A1C_Map_get_cstr(m, "key1");
    EXPECT_EQ(A1C_Map_get_int(m, 42), nullptr);
    map2 = map;
  }
  ASSERT_NE(*map1, *map2);
  {
    auto map = A1C_Item_root(&arena);
    auto m = A1C_Item_map(map, 4, &arena);
    ASSERT_NE(map, nullptr);
    A1C_Item_null(m->keys + 0);
    A1C_Item_null(m->values + 0);
    A1C_Item_boolean(m->keys + 1, true);
    A1C_Item_boolean(m->values + 1, true);
    A1C_Item_boolean(m->keys + 2, false);
    A1C_Item_boolean(m->values + 2, false);
    A1C_Item_undefined(m->keys + 3);
    A1C_Item_undefined(m->values + 3);
    testMap(map);
  }
}

TEST_F(A1CBorTest, Array) {
  auto testArray = [this](const A1C_Item *array) {
    ASSERT_EQ(array->parent, nullptr);
    ASSERT_EQ(array->type, A1C_ItemType_array);
    const A1C_Array &a = array->array;

    for (size_t i = 0; i < a.size; ++i) {
      auto item = A1C_Array_get(&a, i);
      ASSERT_NE(item, nullptr);
      ASSERT_EQ(item, &a.items[i]);

      ASSERT_EQ(a.items[i].parent, array);
    }
    ASSERT_EQ(A1C_Array_get(&a, a.size), nullptr);

    auto encoded = encode(array);
    auto decoded = decode(encoded);
    ASSERT_EQ(*array, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    for (size_t i = 0; i < a.size; ++i) {
      ASSERT_EQ(decoded->array.items[i].parent, decoded);
    }
  };

  {
    auto array = A1C_Item_root(&arena);
    ASSERT_NE(array, nullptr);
    auto a = A1C_Item_array(array, 0, &arena);
    ASSERT_NE(a, nullptr);
    testArray(array);
  }
  A1C_Item *array1;
  {
    auto array = A1C_Item_root(&arena);
    ASSERT_NE(array, nullptr);
    auto a = A1C_Item_array(array, 1, &arena);
    ASSERT_NE(a, nullptr);

    // Fill the array with a single uint64 value
    A1C_Item_uint64(a->items + 0, 42);
    testArray(array);

    EXPECT_EQ(A1C_Array_get(a, 1), nullptr); // out of bounds
    array1 = array;
  }
  A1C_Item *array2;
  {
    auto array = A1C_Item_root(&arena);
    ASSERT_NE(array, nullptr);
    auto a = A1C_Item_array(array, 5, &arena);
    ASSERT_NE(a, nullptr);

    A1C_Item_null(a->items + 0);
    A1C_Item_boolean(a->items + 1, true);
    A1C_Item_undefined(a->items + 2);
    A1C_Item_int64(a->items + 3, 100);
    auto m = A1C_Item_map(a->items + 4, 1, &arena);
    ASSERT_NE(m, nullptr);
    A1C_Item_null(m->keys + 0);
    A1C_Item_null(m->values + 0);

    testArray(array);
    array2 = array;
  }
  ASSERT_NE(*array1, *array2);
}

TEST_F(A1CBorTest, LargeArray) {
  size_t size = 1000;
  auto array = A1C_Item_root(&arena);
  ASSERT_NE(array, nullptr);
  auto a = A1C_Item_array(array, size, &arena);
  ASSERT_NE(a, nullptr);

  for (size_t i = 0; i < size; ++i) {
    A1C_Item_uint64(a->items + i, i);
  }

  auto encoded = encode(array);
  auto decoded = decode(encoded);
  ASSERT_EQ(*array, *decoded);
  ASSERT_EQ(decoded->parent, nullptr);

  for (size_t i = 0; i < size; ++i) {
    auto item = A1C_Array_get(&decoded->array, i);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(item->type, A1C_ItemType_uint64);
    ASSERT_EQ(item->uint64, i);
  }
}

TEST_F(A1CBorTest, DeeplyNested) {
  size_t depth = A1C_MAX_DEPTH_DEFAULT;
  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);

  A1C_Item *current = item;
  for (size_t i = 0; i < depth - 1; ++i) {
    auto tag = A1C_Item_tag(current, i, &arena);
    ASSERT_NE(tag, nullptr);
    current = tag->item;
  }
  A1C_Item_null(current);

  auto encoded = encode(item);
  auto decoded = decode(encoded);
  ASSERT_EQ(*item, *decoded);
  ASSERT_EQ(decoded->parent, nullptr);

  auto tag = A1C_Item_tag(current, 100, &arena);
  ASSERT_NE(tag, nullptr);
  A1C_Item_null(tag->item);

  encoded = encode(item);

  A1C_Decoder decoder;
  A1C_Decoder_init(&decoder, arena, 0, false);
  ASSERT_EQ(A1C_Decoder_decode(
                &decoder, reinterpret_cast<const uint8_t *>(encoded.data()),
                encoded.size()),
            nullptr);
  ASSERT_EQ(decoder.error.type, A1C_ErrorType_maxDepthExceeded);
}

static constexpr char kExpectedJSON[] = R"({
  "key": "value",
  42: [
    -1,
    -3.140000,
    3.140000,
    true,
    false,
    null,
    undefined,
    "aGVsbG8gd29ybGQxAA==",
    "this is a longer string",
    [
    ],
    {
    },
    {
      "type": "tag",
      "tag": 100,
      "value": true
    },
    {
      "type": "simple",
      "value": 42
    },
    [
      "",
      "aA==",
      "aGU=",
      "aGVs",
      "aGVsbA=="
    ]
  ]
})";

TEST_F(A1CBorTest, JSON) {
  auto item = A1C_Item_root(&arena);
  ASSERT_NE(item, nullptr);
  auto map = A1C_Item_map(item, 2, &arena);
  ASSERT_NE(map, nullptr);
  A1C_Item_string_refCStr(map->keys + 0, "key");
  A1C_Item_string_refCStr(map->values + 0, "value");
  A1C_Item_uint64(map->keys + 1, 42);
  auto array = A1C_Item_array(map->values + 1, 14, &arena);
  ASSERT_NE(array, nullptr);
  A1C_Item_int64(array->items + 0, -1);
  A1C_Item_float32(array->items + 1, -3.14);
  A1C_Item_float64(array->items + 2, 3.14);
  A1C_Item_boolean(array->items + 3, true);
  A1C_Item_boolean(array->items + 4, false);
  A1C_Item_null(array->items + 5);
  A1C_Item_undefined(array->items + 6);
  uint8_t shortData[] = "hello world1";
  A1C_Item_bytes_ref(array->items + 7, shortData, sizeof(shortData));
  A1C_Item_string_refCStr(array->items + 8, "this is a longer string");
  ASSERT_NE(A1C_Item_array(array->items + 9, 0, &arena), nullptr);
  ASSERT_NE(A1C_Item_map(array->items + 10, 0, &arena), nullptr);
  auto tag = A1C_Item_tag(array->items + 11, 100, &arena);
  ASSERT_NE(tag, nullptr);
  A1C_Item_boolean(tag->item, true);
  array->items[12].type = A1C_ItemType_simple;
  array->items[12].simple = 42;
  array = A1C_Item_array(array->items + 13, 5, &arena);
  ASSERT_NE(array, nullptr);
  A1C_Item_bytes_ref(array->items + 0, shortData, 0);
  A1C_Item_bytes_ref(array->items + 1, shortData, 1);
  A1C_Item_bytes_ref(array->items + 2, shortData, 2);
  A1C_Item_bytes_ref(array->items + 3, shortData, 3);
  A1C_Item_bytes_ref(array->items + 4, shortData, 4);

  auto encoded = json(item);
  EXPECT_EQ(encoded, kExpectedJSON) << encoded;
}