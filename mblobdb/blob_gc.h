#pragma once

#include <memory>

#include "blob_format.h"
#include "db/column_family.h"
#include "mblobdb/options.h"

namespace rocksdb {
namespace mblobdb {

// A BlobGC encapsulates information about a blob gc.
class BlobGC {
 public:
  BlobGC(std::vector<BlobFileMeta*>&& blob_files,
         NubaseCFOptions&& _titan_cf_options, bool need_trigger_next, uint64_t scan_speed=0);

  // No copying allowed
  BlobGC(const BlobGC&) = delete;
  void operator=(const BlobGC&) = delete;

  ~BlobGC();

  const std::vector<BlobFileMeta*>& inputs() { return inputs_; }

  const NubaseCFOptions& titan_cf_options() { return titan_cf_options_; }

  void SetColumnFamily(ColumnFamilyHandle* cfh);

  ColumnFamilyHandle* column_family_handle() { return cfh_; }

  ColumnFamilyData* GetColumnFamilyData();

  uint64_t GetMaxScanSpeed() { return max_scan_speed; }

  void MarkFilesBeingGC();

  void AddOutputFile(BlobFileMeta*);

  void ReleaseGcFiles();

  bool trigger_next() { return trigger_next_; }

 private:
  std::vector<BlobFileMeta*> inputs_;
  std::vector<BlobFileMeta*> outputs_;
  NubaseCFOptions titan_cf_options_;
  ColumnFamilyHandle* cfh_{nullptr};
  // Whether need to trigger gc after this gc or not
  const bool trigger_next_;
  uint64_t max_scan_speed;
};

struct GCScore {
  uint64_t file_number;
  double score;
};

}  // namespace mblobdb
}  // namespace rocksdb
