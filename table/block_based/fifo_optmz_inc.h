#pragma once

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/types.h"
#include "table/table_builder.h"

namespace rocksdb {

class FifoOptmzTableBuilder : public TableBuilder {
 public:
  FifoOptmzTableBuilder(std::unique_ptr<TableBuilder> base_builder, std::shared_ptr<Cache> cache_)
      : cache(cache_)
      , base_builder_(std::move(base_builder)) { }

  void Add(const Slice& key, const Slice& value) override;

  Status status() const override;

  Status Finish() override;

  void Abandon() override;

  uint64_t NumEntries() const override;

  uint64_t FileSize() const override;

  bool NeedCompact() const override;

  void setFileNumber(uint64_t file_number) { fileNumber = file_number; }

  TableProperties GetTableProperties() const override;

private:
    bool ok() const { return status().ok(); }
    Status status_;
    uint64_t fileNumber;
    std::shared_ptr<Cache> cache;
    std::unique_ptr<TableBuilder> base_builder_;
};

}