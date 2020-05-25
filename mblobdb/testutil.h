#pragma once

#include "rocksdb/cache.h"
#include "test_util/testharness.h"
#include "util/compression.h"

namespace rocksdb {
namespace mblobdb {

template <typename T>
void CheckCodec(const T& input) {
  std::string buffer;
  input.EncodeTo(&buffer);
  T output;
  ASSERT_OK(DecodeInto(buffer, &output));
  ASSERT_EQ(output, input);
}

}  // namespace mblobdb
}  // namespace rocksdb
