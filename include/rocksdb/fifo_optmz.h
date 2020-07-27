#pragma once

#include <atomic>
#include <inttypes.h>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/types.h"

namespace rocksdb {

class FifoOptmzTableFactory : public TableFactory {
 public:
  FifoOptmzTableFactory(const ColumnFamilyOptions& cf_options, std::shared_ptr<InvertedCache> cache_)
      : base_factory_(cf_options.table_factory)
      , cache(cache_) {
        assert(cache);
  }

  const char* Name() const override { return "FifoOptmzTable"; }

  Status NewTableReader(
      const TableReaderOptions& options,
      std::unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
      std::unique_ptr<TableReader>* result,
      bool prefetch_index_and_filter_in_cache = true) const override;

  TableBuilder* NewTableBuilder(const TableBuilderOptions& options,
                                uint32_t column_family_id,
                                WritableFileWriter* file) const override;

  std::string GetPrintableTableOptions() const override;

  Status SanitizeOptions(const DBOptions& db_options,
                         const ColumnFamilyOptions& cf_options) const override {
    // Override this when we need to validate our options.
    return base_factory_->SanitizeOptions(db_options, cf_options);
  }

  Status GetOptionString(std::string* opt_string,
                         const std::string& delimiter) const override {
    // Override this when we need to persist our options.
    return base_factory_->GetOptionString(opt_string, delimiter);
  }

  void* GetOptions() override { return base_factory_->GetOptions(); }

  bool IsDeleteRangeSupported() const override {
    return base_factory_->IsDeleteRangeSupported();
  }

public:
  static uint64_t ivtDecodeFixed64(const std::string& data);

 private:
  std::shared_ptr<TableFactory> base_factory_;
  // TODO: lock
  std::shared_ptr<InvertedCache> cache;
};

}  // namespace rocksdb
