#pragma once

#include "rocksdb/listener.h"

namespace rocksdb {
namespace mblobdb {

class NublobDBImpl;

class BaseDbListener final : public EventListener {
 public:
  BaseDbListener(NublobDBImpl* db);
  ~BaseDbListener();

  void OnFlushCompleted(DB* db, const FlushJobInfo& flush_job_info) override;

  void OnCompactionCompleted(
      DB* db, const CompactionJobInfo& compaction_job_info) override;

 private:
  rocksdb::mblobdb::NublobDBImpl* db_impl_;
};

}  // namespace mblobdb
}  // namespace rocksdb
