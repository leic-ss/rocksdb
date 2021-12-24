#include "blob_kv_area_properties_collector.h"
#include "base_db_listener.h"

namespace rocksdb {
namespace mblobdb {

TablePropertiesCollector*
BlobKvAreaPropertiesCollectorFactory::CreateTablePropertiesCollector(
            rocksdb::TablePropertiesCollectorFactory::Context /*context*/) {
    return new BlobKvAreaPropertiesCollector();
}

const std::string BlobKvAreaPropertiesCollector::kvAreaPropertiesName = "NublobDB.blob_kv_area_properties";

bool BlobKvAreaPropertiesCollector::Encode(const std::map<uint32_t, uint64_t>& kv_area_size,
                                           const std::map<uint32_t, uint64_t>& kv_area_item_count,
                                           std::string* result)
{
    PutVarint32(result, static_cast<uint32_t>(kv_area_size.size()));
    for (const auto& bfs : kv_area_size) {
        PutVarint32(result, bfs.first);
        PutVarint64(result, bfs.second);
    }
    PutVarint32(result, static_cast<uint32_t>(kv_area_item_count.size()));
    for (const auto& bfs : kv_area_item_count) {
        PutVarint32(result, bfs.first);
        PutVarint64(result, bfs.second);
    }
    return true;
}

bool BlobKvAreaPropertiesCollector::Decode(Slice* slice,
                                           std::map<uint32_t, uint64_t>& kv_area_size,
                                           std::map<uint32_t, uint64_t>& kv_area_item_count)
{
    uint32_t num = 0;
    if (!GetVarint32(slice, &num)) {
        return false;
    }

    uint32_t area;
    uint64_t size;
    for (uint32_t i = 0; i < num; ++i) {
        if (!GetVarint32(slice, &area)) {
            return false;
        }
        if (!GetVarint64(slice, &size)) {
            return false;
        }
        kv_area_size[area] = size;
    }

    if (!GetVarint32(slice, &num)) {
        return false;
    }

    uint64_t count;
    for (uint32_t i = 0; i < num; ++i) {
        if (!GetVarint32(slice, &area)) {
            return false;
        }
        if (!GetVarint64(slice, &count)) {
            return false;
        }
        kv_area_item_count[area] = count;
    }

    return true;
}

Status BlobKvAreaPropertiesCollector::AddUserKey(const Slice& key,
                                                 const Slice& value, EntryType type,
                                                 SequenceNumber /* seq */,
                                                 uint64_t /* file_size */)
{
    // Slice meta_value = Slice(value.data(), ValueMeta::size());
    // ValueMeta meta;
    // meta.decodeFromBuf((uint8_t*)meta_value.data());
    if (key.size() < 5) return Status::InvalidArgument();

    static const int32_t meta_size = 3;
    const char* buf = key.data() + meta_size;
    int32_t area = (static_cast<int32_t>(static_cast<uint8_t>(buf[1])) << 8) | static_cast<uint8_t>(buf[0]);

    if (type == kEntryPut) {
        auto size_iter = kv_area_size.find(area);
        if (size_iter == kv_area_size.end()) {
            kv_area_size[area] = value.size();
        } else {
            size_iter->second += value.size();
        }

        auto count_iter = kv_area_item_count.find(area);
        if (count_iter == kv_area_item_count.end()) {
            kv_area_item_count[area] = 1;
        } else {
            count_iter->second += 1;
        }

        return Status::OK();
    }

    if (type == kEntryBlobIndex || type == kEntryMerge) {
        Status s;
        MergeBlobIndex index;

        Slice copy = Slice(value.data() + ValueMeta::size(), value.size() - ValueMeta::size());
        if (type == kEntryMerge) {
            s = index.DecodeFrom(const_cast<Slice*>(&copy));
        } else {
            s = index.DecodeFromBase(const_cast<Slice*>(&copy));
        }

        if (!s.ok()) {
            return s;
        }

        auto size_iter = kv_area_size.find(area);
        if (size_iter == kv_area_size.end()) {
            kv_area_size[area] = index.blob_handle.size + value.size();
        } else {
            size_iter->second += index.blob_handle.size + value.size();
        }

        auto count_iter = kv_area_item_count.find(area);
        if (count_iter == kv_area_item_count.end()) {
            kv_area_item_count[area] = 1;
        } else {
            count_iter->second += 1;
        }

        return Status::OK();
    }

    return Status::OK();
}

Status BlobKvAreaPropertiesCollector::Finish(UserCollectedProperties* properties)
{
    if (kv_area_size.empty() || kv_area_item_count.empty()) {
        return Status::OK();
    }

    std::string res;
    bool ok __attribute__((__unused__)) = Encode(kv_area_size, kv_area_item_count, &res);
    assert(ok);
    assert(!res.empty());
    properties->emplace(std::make_pair(kvAreaPropertiesName, res));
    return Status::OK();
}

}  // namespace mblobdb
}  // namespace rocksdb
