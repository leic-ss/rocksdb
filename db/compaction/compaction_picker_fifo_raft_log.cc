//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/compaction/compaction_picker_fifo_raft_log.h"
#ifndef ROCKSDB_LITE

#include <cinttypes>
#include <string>
#include <vector>
#include "db/column_family.h"
#include "logging/log_buffer.h"
#include "util/string_util.h"

namespace rocksdb {
namespace {
uint64_t GetTotalFilesSize(const std::vector<FileMetaData*>& files) {
  uint64_t total_size = 0;
  for (const auto& f : files) {
    total_size += f->fd.file_size;
  }
  return total_size;
}
}  // anonymous namespace

bool FIFOCompactionPickerRaftLog::NeedsCompaction(
    const VersionStorageInfo* vstorage) const {
  const int kLevel0 = 0;
  return vstorage->CompactionScore(kLevel0) >= 1;
}

Compaction* FIFOCompactionPickerRaftLog::PickRaftlogExpiredCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, LogBuffer* log_buffer) {
  // assert(mutable_cf_options.ttl > 0);

  const int kLevel0 = 0;
  const std::vector<FileMetaData*>& level_files = vstorage->LevelFiles(kLevel0);
  uint64_t total_size = GetTotalFilesSize(level_files);

  int64_t _current_time;
  auto status = ioptions_.env->GetCurrentTime(&_current_time);
  if (!status.ok()) {
    ROCKS_LOG_BUFFER(log_buffer,
                     "[%s] FIFO compaction: Couldn't get current time: %s. "
                     "Not doing compactions based on TTL. ",
                     cf_name.c_str(), status.ToString().c_str());
    return nullptr;
  }
  const uint64_t current_time = static_cast<uint64_t>(_current_time);

  if (!level0_compactions_in_progress_.empty()) {
    ROCKS_LOG_BUFFER(
        log_buffer,
        "[%s] FIFO compaction: Already executing compaction. No need "
        "to run parallel compactions since compactions are very fast",
        cf_name.c_str());
    return nullptr;
  }

  std::vector<CompactionInputFiles> inputs;
  inputs.emplace_back();
  inputs[0].level = 0;

  // avoid underflow
  for (auto ritr = level_files.rbegin(); ritr != level_files.rend(); ++ritr) {
    auto f = *ritr;

    Slice largest_key = f->largest.user_key();
    uint64_t r_val = 0;
    if (largest_key.size() == 8) {
      const uint8_t* buff = (const uint8_t*)largest_key.data();
      get64b(buff, r_val);
    }

    if (r_val != 0 && r_val < mutable_cf_options.raft_log_min_key) {
        total_size -= f->compensated_file_size;
        inputs[0].files.push_back(f);
    }
  }

  // Return a nullptr and proceed to size-based FIFO compaction if:
  // 1. there are no files older than ttl OR
  // 2. there are a few files older than ttl, but deleting them will not bring
  //    the total size to be less than max_table_files_size threshold.
  if (inputs[0].files.empty() ||
      total_size > mutable_cf_options.compaction_options_fifo.max_table_files_size) {
    return nullptr;
  }

  for (const auto& f : inputs[0].files) {
    ROCKS_LOG_BUFFER(log_buffer,
                     "[%s] FIFORaftlog compaction: picking file %" PRIu64
                     " with creation time %" PRIu64 " for deletion",
                     cf_name.c_str(), f->fd.GetNumber(),
                     f->fd.table_reader->GetTableProperties()->creation_time);
  }

  Compaction* c = new Compaction(
      vstorage, ioptions_, mutable_cf_options, std::move(inputs), 0, 0, 0, 0,
      kNoCompression, ioptions_.compression_opts, /* max_subcompactions */ 0,
      {}, /* is manual */ false, vstorage->CompactionScore(0),
      /* is deletion compaction */ true, CompactionReason::kFIFORaftLog);
  return c;
}

Compaction* FIFOCompactionPickerRaftLog::PickCompaction(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, LogBuffer* log_buffer,
    SequenceNumber /*earliest_memtable_seqno*/) {
  assert(vstorage->num_levels() == 1);

  Compaction* c = nullptr;
  c = PickRaftlogExpiredCompaction(cf_name, mutable_cf_options, vstorage, log_buffer);
  RegisterCompaction(c);
  return c;
}

Compaction* FIFOCompactionPickerRaftLog::CompactRange(
    const std::string& cf_name, const MutableCFOptions& mutable_cf_options,
    VersionStorageInfo* vstorage, int input_level, int output_level,
    const CompactRangeOptions& /*compact_range_options*/,
    const InternalKey* /*begin*/, const InternalKey* /*end*/,
    InternalKey** compaction_end, bool* /*manual_conflict*/,
    uint64_t /*max_file_num_to_ignore*/) {
#ifdef NDEBUG
  (void)input_level;
  (void)output_level;
#endif
  assert(input_level == 0);
  assert(output_level == 0);
  *compaction_end = nullptr;
  LogBuffer log_buffer(InfoLogLevel::INFO_LEVEL, ioptions_.info_log);
  Compaction* c =
      PickCompaction(cf_name, mutable_cf_options, vstorage, &log_buffer);
  log_buffer.FlushBufferToLog();
  return c;
}

}  // namespace rocksdb
#endif  // !ROCKSDB_LITE
