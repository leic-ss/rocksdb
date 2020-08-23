#pragma once

#include <memory>

#include "db/column_family.h"
#include "db/write_callback.h"
#include "file/filename.h"
#include "rocksdb/status.h"

#include "blob_file_manager.h"
#include "blob_format.h"
#include "blob_gc.h"
#include "blob_storage.h"

namespace rocksdb {
namespace mblobdb {

class BlobGCPicker {
 public:
  BlobGCPicker(){};
  virtual ~BlobGCPicker(){};

  // Pick candidate blob files for a new gc.
  // Returns nullptr if there is no gc to be done.
  // Otherwise returns a pointer to a heap-allocated object that
  // describes the gc.  Caller should delete the result.
  virtual std::unique_ptr<BlobGC> PickBlobGC(BlobStorage* blob_storage) = 0;
};

class BasicBlobGCPicker final : public BlobGCPicker {
 public:
  BasicBlobGCPicker(TitanDBOptions, TitanCFOptions, TitanStats*, uint64_t scan_speed=0);
  ~BasicBlobGCPicker();

  std::unique_ptr<BlobGC> PickBlobGC(BlobStorage* blob_storage) override;

 private:
  TitanDBOptions db_options_;
  TitanCFOptions cf_options_;
  TitanStats* stats_;
  uint64_t max_scan_speed;

  // Check if blob_file needs to gc, return true means we need pick this
  // file for gc
  bool CheckBlobFile(BlobFileMeta* blob_file) const;
};

}  // namespace mblobdb
}  // namespace rocksdb
