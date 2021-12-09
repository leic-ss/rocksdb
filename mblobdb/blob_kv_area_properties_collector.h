#pragma once

#include "blob_file_set.h"
#include "db_impl.h"
#include "rocksdb/listener.h"
#include "rocksdb/table_properties.h"
#include "util/coding.h"

namespace rocksdb {
namespace mblobdb {

class BlobKvAreaPropertiesCollectorFactory final : public TablePropertiesCollectorFactory {
public:
    TablePropertiesCollector* CreateTablePropertiesCollector(
      TablePropertiesCollectorFactory::Context context) override;

    const char* Name() const override { return "BlobKvAreaPropertiesCollectorFactory"; }
};

class BlobKvAreaPropertiesCollector final : public TablePropertiesCollector {
public:
    const static std::string kvAreaPropertiesName;

    static bool Encode(const std::map<uint32_t, uint64_t>& kv_area_size,
                       const std::map<uint32_t, uint64_t>& kv_area_item_count,
                       std::string* result);
    static bool Decode(Slice* slice,
                       std::map<uint32_t, uint64_t>& kv_area_size,
                       std::map<uint32_t, uint64_t>& kv_area_item_count);

    Status AddUserKey(const Slice& key, const Slice& value, EntryType type,
                      SequenceNumber seq, uint64_t file_size) override;

    Status Finish(UserCollectedProperties* properties) override;

    UserCollectedProperties GetReadableProperties() const override {
        return UserCollectedProperties();
    }
    const char* Name() const override { return "BlobKvAreaPropertiesCollector"; }

private:
    std::map<uint32_t, uint64_t> kv_area_size;
    std::map<uint32_t, uint64_t> kv_area_item_count;
};

}  // namespace mblobdb
}  // namespace rocksdb
