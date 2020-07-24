#include "rocksdb/fifo_optmz.h"
#include "fifo_optmz_inc.h"
#include "db/dbformat.h"

#include <stdint.h>

namespace rocksdb {

static void* EncodeValue(uintptr_t v) { return reinterpret_cast<void*>(v); }

void FifoOptmzTableBuilder::Add(const Slice& key, const Slice& value) {
    if (!ok()) return;

    rocksdb::ParsedInternalKey ikey;
    if (!ParseInternalKey(key, &ikey)) {
      status_ = Status::Corruption(Slice());
      return;
    }

    //fprintf(stderr, "%.*s %lu\n", (int)ikey.user_key.size(), ikey.user_key.data(), fileNumber);
    base_builder_->Add(key, value);

    if (!cache) {
        return ;
    }
    cache->Insert(ikey.user_key, EncodeValue(fileNumber), ikey.user_key.size() + sizeof(uint64_t), nullptr);
}

Status FifoOptmzTableBuilder::status() const {
  Status s = status_;
  if (s.ok()) {
    s = base_builder_->status();
  }
  return s;
}

Status FifoOptmzTableBuilder::Finish() {
  return base_builder_->Finish();
}

void FifoOptmzTableBuilder::Abandon() {
  base_builder_->Abandon();
}

uint64_t FifoOptmzTableBuilder::NumEntries() const {
  return base_builder_->NumEntries();
}

uint64_t FifoOptmzTableBuilder::FileSize() const {
  return base_builder_->FileSize();
}

bool FifoOptmzTableBuilder::NeedCompact() const {
  return base_builder_->NeedCompact();
}

TableProperties FifoOptmzTableBuilder::GetTableProperties() const {
  return base_builder_->GetTableProperties();
}

Status FifoOptmzTableFactory::NewTableReader(
    const TableReaderOptions& options,
    std::unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
    std::unique_ptr<TableReader>* result,
    bool prefetch_index_and_filter_in_cache) const {
  return base_factory_->NewTableReader(options, std::move(file), file_size,
                                       result,
                                       prefetch_index_and_filter_in_cache);
}

TableBuilder* FifoOptmzTableFactory::NewTableBuilder(
    const TableBuilderOptions& options, uint32_t column_family_id,
    WritableFileWriter* file) const {
    std::unique_ptr<TableBuilder> base_builder(
          base_factory_->NewTableBuilder(options, column_family_id, file));

    return new FifoOptmzTableBuilder(std::move(base_builder), cache);
}

std::string FifoOptmzTableFactory::GetPrintableTableOptions() const {
  return base_factory_->GetPrintableTableOptions();
}

}  // namespace rocksdb
