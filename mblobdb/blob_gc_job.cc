#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include <memory>

#include "blob_gc_job.h"

#include "blob_file_size_collector.h"

namespace rocksdb {
namespace mblobdb {

// Write callback for garbage collection to check if key has been updated
// since last read. Similar to how OptimisticTransaction works.
class BlobGCJob::GarbageCollectionWriteCallback : public WriteCallback {
 public:
  GarbageCollectionWriteCallback(ColumnFamilyHandle* cfh, std::string&& _key,
                                 BlobIndex&& blob_index)
      : cfh_(cfh), key_(std::move(_key)), blob_index_(blob_index) {
    assert(!key_.empty());
  }

  std::string value;

  virtual Status Callback(DB* db) override {
    auto* db_impl = reinterpret_cast<DBImpl*>(db);

    DBImpl::GetImplOptions get_impl_options;
    PinnableSlice index_entry;
    bool is_blob_index = false;
    get_impl_options.column_family = cfh_;
    get_impl_options.value = &index_entry;
    get_impl_options.is_blob_index = &is_blob_index;
    auto s = db_impl->GetImpl(ReadOptions(), key_, get_impl_options);
    if (!s.ok() && !s.IsNotFound()) {
      return s;
    }
    read_bytes_ = key_.size() + index_entry.size();
    if (s.IsNotFound()) {
      // Either the key is deleted or updated with a newer version which is
      // inlined in LSM.
      s = Status::Busy("key deleted");
    } else if (!is_blob_index) {
      s = Status::Busy("key overwritten with other value");
    }

    if (s.ok()) {
      BlobIndex other_blob_index;
      index_entry.remove_prefix(ValueMeta::size());
      s = other_blob_index.DecodeFrom(&index_entry);
      if (!s.ok()) {
        return s;
      }

      if (!(blob_index_ == other_blob_index)) {
        s = Status::Busy("key overwritten with other blob");
      }
    }

    return s;
  }

  virtual bool AllowWriteBatching() override { return false; }

  std::string key() { return key_; }

  uint64_t read_bytes() { return read_bytes_; }

  uint64_t blob_record_size() { return blob_index_.blob_handle.size; }

 private:
  ColumnFamilyHandle* cfh_;
  // Key to check
  std::string key_;
  BlobIndex blob_index_;
  uint64_t read_bytes_;
};

void CSpeedLimit::limitrate()
{
    uint64_t curmsec = (env->NowMicros() / 1000);
    if( curmsec/m_smoothTimer != m_curTimerIndex ) {        
        m_curTimerIndex = curmsec/m_smoothTimer;
        m_count = 1;
        return ;
    }

    ++m_count;
    uint64_t walkmsec = curmsec%m_smoothTimer;
    uint64_t shouldmsec = (uint64_t)(m_count/m_ratePmsec);
    if( shouldmsec > walkmsec ) {
        env->SleepForMicroseconds( 1000*(shouldmsec - walkmsec) );
    }
}

BlobGCJob::BlobGCJob(BlobGC* blob_gc, DB* db, port::Mutex* mutex,
                     const NubaseDBOptions& titan_db_options,
                     bool gc_merge_rewrite, Env* env,
                     const EnvOptions& env_options,
                     BlobFileManager* blob_file_manager,
                     BlobFileSet* blob_file_set, LogBuffer* log_buffer,
                     std::atomic_bool* shuting_down, TitanStats* stats)
    : blob_gc_(blob_gc),
      base_db_(db),
      base_db_impl_(reinterpret_cast<DBImpl*>(base_db_)),
      mutex_(mutex),
      db_options_(titan_db_options),
      gc_merge_rewrite_(gc_merge_rewrite),
      env_(env),
      env_options_(env_options),
      blob_file_manager_(blob_file_manager),
      blob_file_set_(blob_file_set),
      log_buffer_(log_buffer),
      shuting_down_(shuting_down),
      stats_(stats) {}

BlobGCJob::~BlobGCJob() {
  if (log_buffer_) {
    log_buffer_->FlushBufferToLog();
    LogFlush(db_options_.info_log.get());
  }
  // flush metrics
  RecordTick(statistics(stats_), TITAN_GC_BYTES_READ, metrics_.gc_bytes_read);
  RecordTick(statistics(stats_), TITAN_GC_BYTES_WRITTEN,
             metrics_.gc_bytes_written);
  RecordTick(statistics(stats_), TITAN_GC_NUM_KEYS_OVERWRITTEN,
             metrics_.gc_num_keys_overwritten);
  RecordTick(statistics(stats_), TITAN_GC_BYTES_OVERWRITTEN,
             metrics_.gc_bytes_overwritten);
  RecordTick(statistics(stats_), TITAN_GC_NUM_KEYS_RELOCATED,
             metrics_.gc_num_keys_relocated);
  RecordTick(statistics(stats_), TITAN_GC_BYTES_RELOCATED,
             metrics_.gc_bytes_relocated);
  RecordTick(statistics(stats_), TITAN_GC_NUM_NEW_FILES,
             metrics_.gc_num_new_files);
  RecordTick(statistics(stats_), TITAN_GC_NUM_FILES, metrics_.gc_num_files);
  RecordTick(statistics(stats_), TITAN_GC_DISCARDABLE, metrics_.gc_discardable);
  RecordTick(statistics(stats_), TITAN_GC_SMALL_FILE, metrics_.gc_small_file);
  RecordTick(statistics(stats_), TITAN_GC_SAMPLE, metrics_.gc_sample);
}

Status BlobGCJob::Prepare() {
  SavePrevIOBytes(&prev_bytes_read_, &prev_bytes_written_);
  return Status::OK();
}

Status BlobGCJob::Run() {
  std::string tmp;
  for (const auto& f : blob_gc_->inputs()) {
    if (!tmp.empty()) {
      tmp.append(" ");
    }
    tmp.append(std::to_string(f->file_number()));
  }
  ROCKS_LOG_BUFFER(log_buffer_, "[%s] Titan GC candidates[%s] limit_speed[%lu]",
                   blob_gc_->column_family_handle()->GetName().c_str(),
                   tmp.c_str(),
                   blob_gc_->GetMaxScanSpeed());
  return DoRunGC();
}

Status BlobGCJob::DoRunGC() {
  Status s;

  std::unique_ptr<BlobFileMergeIterator> gc_iter;
  s = BuildIterator(&gc_iter);
  if (!s.ok()) return s;
  if (!gc_iter) return Status::Aborted("Build iterator for gc failed");

  // Similar to OptimisticTransaction, we obtain latest_seq from
  // base DB, which is guaranteed to be no smaller than the sequence of
  // current key. We use a WriteCallback on write to check the key sequence
  // on write. If the key sequence is larger than latest_seq, we know
  // a new versions is inserted and the old blob can be discard.
  //
  // We cannot use OptimisticTransaction because we need to pass
  // is_blob_index flag to GetImpl.
  std::unique_ptr<BlobFileHandle> blob_file_handle;
  std::unique_ptr<BlobFileBuilder> blob_file_builder;

  auto* cfh = blob_gc_->column_family_handle();
  uint64_t limit_speed = blob_gc_->GetMaxScanSpeed();
  CSpeedLimit* limiter = nullptr;
  if ( limit_speed > 0 ) limiter = new CSpeedLimit(limit_speed);
  Env* env = Env::Default();
  int64_t cur_sec = 0;
  s = env->GetCurrentTime(&cur_sec);
  if (!s.ok()) {
      ROCKS_LOG_BUFFER(log_buffer_, "GetCurrentTime failed! status[%s]", s.ToString().c_str());
  }

  //  uint64_t drop_entry_num = 0;
  //  uint64_t drop_entry_size = 0;
  //  uint64_t total_entry_num = 0;
  //  uint64_t total_entry_size = 0;

  uint64_t file_size = 0;

  std::string last_key;
  bool last_key_valid = false;
  gc_iter->SeekToFirst();
  assert(gc_iter->Valid());
  for (; gc_iter->Valid(); gc_iter->Next()) {
    if (IsShutingDown()) {
      s = Status::ShutdownInProgress();
      break;
    }

    if (limiter) limiter->limitrate();

    BlobIndex blob_index = gc_iter->GetBlobIndex();
    // count read bytes for blob record of gc candidate files
    metrics_.gc_bytes_read += blob_index.blob_handle.size;

    if (!last_key.empty() && !gc_iter->key().compare(last_key)) {
      if (last_key_valid) {
        continue;
      }
    } else {
      last_key = gc_iter->key().ToString();
      last_key_valid = false;
    }

    bool discardable = false;
    s = DiscardEntry(gc_iter->key(), blob_index, &discardable);
    if (!s.ok()) {
      break;
    }
    if (discardable) {
      metrics_.gc_num_keys_overwritten++;
      metrics_.gc_bytes_overwritten += blob_index.blob_handle.size;
      continue;
    }

    last_key_valid = true;

    const Slice& value = gc_iter->value();
    if (value.size() >= ValueMeta::size()) {
        ValueMeta meta;
        meta.decodeFromBuf((uint8_t*)value.data());
        if (cur_sec > 0 && meta.edate < cur_sec) continue;
    }

    // Rewrite entry to new blob file
    if ((!blob_file_handle && !blob_file_builder) ||
        file_size >= blob_gc_->titan_cf_options().blob_file_target_size) {
      if (file_size >= blob_gc_->titan_cf_options().blob_file_target_size) {
        assert(blob_file_builder);
        assert(blob_file_handle);
        assert(blob_file_builder->status().ok());
        blob_file_builders_.emplace_back(std::make_pair(
            std::move(blob_file_handle), std::move(blob_file_builder)));
      }
      s = blob_file_manager_->NewFile(&blob_file_handle);
      if (!s.ok()) {
        break;
      }
      ROCKS_LOG_INFO(db_options_.info_log,
                     "Titan new GC output file %" PRIu64 ".",
                     blob_file_handle->GetNumber());
      blob_file_builder = std::unique_ptr<BlobFileBuilder>(
          new BlobFileBuilder(db_options_, blob_gc_->titan_cf_options(),
                              blob_file_handle->GetFile()));
      file_size = 0;
    }
    assert(blob_file_handle);
    assert(blob_file_builder);

    BlobRecord blob_record;
    blob_record.key = gc_iter->key();
    blob_record.value = gc_iter->value();
    // count written bytes for new blob record,
    // blob index's size is counted in `RewriteValidKeyToLSM`
    uint64_t rec_size = blob_record.size();
    metrics_.gc_bytes_written += rec_size;
    if(limiter) file_size += rec_size;

    MergeBlobIndex new_blob_index;
    new_blob_index.file_number = blob_file_handle->GetNumber();
    new_blob_index.source_file_number = blob_index.file_number;
    new_blob_index.source_file_offset = blob_index.blob_handle.offset;

    blob_file_builder->Add(blob_record, &new_blob_index.blob_handle);
    std::string index_entry;

    if (blob_record.value.size() >= ValueMeta::size()) {
      index_entry.append(blob_record.value.data(), ValueMeta::size());
    }

    if (!gc_merge_rewrite_) {
      new_blob_index.EncodeToBase(&index_entry);
      // Store WriteBatch for rewriting new Key-Index pairs to LSM
      GarbageCollectionWriteCallback callback(cfh, blob_record.key.ToString(),
                                              std::move(blob_index));
      callback.value = index_entry;
      rewrite_batches_.emplace_back(
          std::make_pair(WriteBatch(), std::move(callback)));
      auto& wb = rewrite_batches_.back().first;
      s = WriteBatchInternal::PutBlobIndex(&wb, cfh->GetID(), blob_record.key,
                                           index_entry);
    } else {
      new_blob_index.EncodeTo(&index_entry);
      rewrite_batches_without_callback_.emplace_back(
          std::make_pair(WriteBatch(), blob_index.blob_handle.size));
      auto& wb = rewrite_batches_without_callback_.back().first;
      s = WriteBatchInternal::Merge(&wb, cfh->GetID(), blob_record.key,
                                    index_entry);
    }
    if (!s.ok()) {
      break;
    }
  }

  if (limiter) delete limiter;

  if (gc_iter->status().ok() && s.ok()) {
    if (blob_file_builder && blob_file_handle) {
      assert(blob_file_builder->status().ok());
      blob_file_builders_.emplace_back(std::make_pair(
          std::move(blob_file_handle), std::move(blob_file_builder)));
    } else {
      assert(!blob_file_builder);
      assert(!blob_file_handle);
    }
  } else if (!gc_iter->status().ok()) {
    return gc_iter->status();
  }

  return s;
}

Status BlobGCJob::BuildIterator(
    std::unique_ptr<BlobFileMergeIterator>* result) {
  Status s;
  const auto& inputs = blob_gc_->inputs();
  assert(!inputs.empty());
  std::vector<std::unique_ptr<BlobFileIterator>> list;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    std::unique_ptr<RandomAccessFileReader> file;
    // TODO(@DorianZheng) set read ahead size
    s = NewBlobFileReader(inputs[i]->file_number(), 0, db_options_,
                          env_options_, env_, &file);
    if (!s.ok()) {
      break;
    }
    list.emplace_back(std::unique_ptr<BlobFileIterator>(new BlobFileIterator(
        std::move(file), inputs[i]->file_number(), inputs[i]->file_size(),
        blob_gc_->titan_cf_options())));
  }

  if (s.ok())
    result->reset(new BlobFileMergeIterator(
        std::move(list), blob_gc_->titan_cf_options().comparator));

  return s;
}

Status BlobGCJob::DiscardEntry(const Slice& key, const BlobIndex& blob_index,
                               bool* discardable) {
  TitanStopWatch sw(env_, metrics_.gc_read_lsm_micros);
  assert(discardable != nullptr);

  DBImpl::GetImplOptions get_impl_options;
  PinnableSlice index_entry;
  bool is_blob_index = false;
  get_impl_options.column_family = blob_gc_->column_family_handle();
  get_impl_options.value = &index_entry;
  get_impl_options.is_blob_index = &is_blob_index;

  Status s = base_db_impl_->GetImpl(ReadOptions(), key, get_impl_options);
  if (!s.ok() && !s.IsNotFound()) {
    return s;
  }
  // count read bytes for checking LSM entry
  metrics_.gc_bytes_read += key.size() + index_entry.size();
  if (s.IsNotFound() || !is_blob_index) {
    // Either the key is deleted or updated with a newer version which is
    // inlined in LSM.
    *discardable = true;
    return Status::OK();
  }

  BlobIndex other_blob_index;
  index_entry.remove_prefix(ValueMeta::size());
  s = other_blob_index.DecodeFrom(&index_entry);
  if (!s.ok()) {
    return s;
  }

  *discardable = !(blob_index == other_blob_index);
  return Status::OK();
}

// We have to make sure crash consistency, but LSM db MANIFEST and BLOB db
// MANIFEST are separate, so we need to make sure all new blob file have
// added to db before we rewrite any key to LSM
Status BlobGCJob::Finish() {
  Status s;
  {
    mutex_->Unlock();
    s = InstallOutputBlobFiles();
    if (s.ok()) {
      s = RewriteValidKeyToLSM();
      if (!s.ok()) {
        ROCKS_LOG_ERROR(db_options_.info_log,
                        "[%s] GC job failed to rewrite keys to LSM: %s",
                        blob_gc_->column_family_handle()->GetName().c_str(),
                        s.ToString().c_str());
      }
    } else {
      ROCKS_LOG_ERROR(db_options_.info_log,
                      "[%s] GC job failed to install output blob files: %s",
                      blob_gc_->column_family_handle()->GetName().c_str(),
                      s.ToString().c_str());
    }
    mutex_->Lock();
  }

  // TODO(@DorianZheng) cal discardable size for new blob file

  if (s.ok() && !blob_gc_->GetColumnFamilyData()->IsDropped()) {
    s = DeleteInputBlobFiles();
  }

  if (s.ok()) {
    UpdateInternalOpStats();
  }

  return s;
}

Status BlobGCJob::InstallOutputBlobFiles() {
  Status s;
  for (auto& builder : blob_file_builders_) {
    s = builder.second->Finish();
    if (!s.ok()) {
      break;
    }
    metrics_.gc_num_new_files++;
  }
  if (s.ok()) {
    std::vector<std::pair<std::shared_ptr<BlobFileMeta>,
                          std::unique_ptr<BlobFileHandle>>>
        files;
    std::string tmp;
    for (auto& builder : blob_file_builders_) {
      auto file = std::make_shared<BlobFileMeta>(
          builder.first->GetNumber(), builder.first->GetFile()->GetFileSize(),
          0, 0, builder.second->GetSmallestKey(),
          builder.second->GetLargestKey());
      file->set_live_data_size(builder.second->live_data_size());
      file->FileStateTransit(BlobFileMeta::FileEvent::kGCOutput);
      RecordInHistogram(statistics(stats_), TITAN_GC_OUTPUT_FILE_SIZE,
                        file->file_size());
      if (!tmp.empty()) {
        tmp.append(" ");
      }
      tmp.append(std::to_string(file->file_number()));
      files.emplace_back(std::make_pair(file, std::move(builder.first)));
    }
    ROCKS_LOG_BUFFER(log_buffer_, "[%s] output[%s]",
                     blob_gc_->column_family_handle()->GetName().c_str(),
                     tmp.c_str());
    s = blob_file_manager_->BatchFinishFiles(
        blob_gc_->column_family_handle()->GetID(), files);
    if (s.ok()) {
      for (auto& file : files) {
        blob_gc_->AddOutputFile(file.first.get());
      }
    }
  } else {
    std::vector<std::unique_ptr<BlobFileHandle>> handles;
    std::string to_delete_files;
    for (auto& builder : blob_file_builders_) {
      if (!to_delete_files.empty()) {
        to_delete_files.append(" ");
      }
      to_delete_files.append(std::to_string(builder.first->GetNumber()));
      handles.emplace_back(std::move(builder.first));
    }
    ROCKS_LOG_BUFFER(log_buffer_,
                     "[%s] InstallOutputBlobFiles failed. Delete GC output "
                     "files: %s",
                     blob_gc_->column_family_handle()->GetName().c_str(),
                     to_delete_files.c_str());
    // Do not set status `s` here, cause it may override the non-okay-status of
    // `s` so that in the outer funcation it will rewrite blob indexes to LSM by
    // mistake.
    Status status = blob_file_manager_->BatchDeleteFiles(handles);
    if (!status.ok()) {
      ROCKS_LOG_WARN(db_options_.info_log,
                     "Delete GC output files[%s] failed: %s",
                     to_delete_files.c_str(), status.ToString().c_str());
    }
  }
  return s;
}

Status BlobGCJob::RewriteValidKeyToLSM() {
  TitanStopWatch sw(env_, metrics_.gc_update_lsm_micros);
  Status s;
  auto* db_impl = reinterpret_cast<DBImpl*>(base_db_);

  WriteOptions wo;
  wo.low_pri = true;
  wo.ignore_missing_column_families = true;
  if (!gc_merge_rewrite_) {
    for (auto& write_batch : rewrite_batches_) {
      if (blob_gc_->GetColumnFamilyData()->IsDropped()) {
        s = Status::Aborted("Column family drop");
        break;
      }
      if (IsShutingDown()) {
        s = Status::ShutdownInProgress();
        break;
      }
      s = db_impl->WriteWithCallback(wo, &write_batch.first,
                                     &write_batch.second);
      if (s.ok()) {
        // count written bytes for new blob index.
        metrics_.gc_bytes_written += write_batch.first.GetDataSize();
        metrics_.gc_num_keys_relocated++;
        metrics_.gc_bytes_relocated += write_batch.second.blob_record_size();
        // Key is successfully written to LSM.
      } else if (s.IsBusy()) {
        metrics_.gc_num_keys_overwritten++;
        metrics_.gc_bytes_overwritten += write_batch.second.blob_record_size();
        // The key is overwritten in the meanwhile. Drop the blob record.
      } else {
        // We hit an error.
        break;
      }
      // count read bytes in write callback
      metrics_.gc_bytes_read += write_batch.second.read_bytes();
    }
  } else {
    for (auto& write_batch : rewrite_batches_without_callback_) {
      if (blob_gc_->GetColumnFamilyData()->IsDropped()) {
        s = Status::Aborted("Column family drop");
        break;
      }
      if (IsShutingDown()) {
        s = Status::ShutdownInProgress();
        break;
      }
      s = db_impl->Write(wo, &write_batch.first);
      if (s.ok()) {
        // count written bytes for new blob index.
        metrics_.gc_bytes_written += write_batch.first.GetDataSize();
        metrics_.gc_num_keys_relocated++;
        metrics_.gc_bytes_relocated += write_batch.second;
        // Key is successfully written to LSM.
      } else {
        // We hit an error.
        break;
      }
      // read bytes is 0
    }
  }
  if (s.IsBusy()) {
    s = Status::OK();
  }

  if (s.ok()) {
    // Flush and sync WAL.
    s = db_impl->FlushWAL(true /*sync*/);
  }

  return s;
}

Status BlobGCJob::DeleteInputBlobFiles() {
  SequenceNumber obsolete_sequence = base_db_impl_->GetLatestSequenceNumber();

  Status s;
  VersionEdit edit;
  edit.SetColumnFamilyID(blob_gc_->column_family_handle()->GetID());
  for (const auto& file : blob_gc_->inputs()) {
    ROCKS_LOG_INFO(db_options_.info_log,
                   "Titan add obsolete file [%" PRIu64 "] range [%s, %s]",
                   file->file_number(),
                   Slice(file->smallest_key()).ToString(true).c_str(),
                   Slice(file->largest_key()).ToString(true).c_str());
    metrics_.gc_num_files++;
    RecordInHistogram(statistics(stats_), TITAN_GC_INPUT_FILE_SIZE,
                      file->file_size());
    edit.DeleteBlobFile(file->file_number(), obsolete_sequence);
  }
  s = blob_file_set_->LogAndApply(edit);
  // TODO(@DorianZheng) Purge pending outputs
  // base_db_->pending_outputs_.erase(handle->GetNumber());
  return s;
}

bool BlobGCJob::IsShutingDown() {
  return (shuting_down_ && shuting_down_->load(std::memory_order_acquire));
}

void BlobGCJob::UpdateInternalOpStats() {
  if (stats_ == nullptr) {
    return;
  }
  UpdateIOBytes(prev_bytes_read_, prev_bytes_written_, &io_bytes_read_,
                &io_bytes_written_);
  uint32_t cf_id = blob_gc_->column_family_handle()->GetID();
  TitanInternalStats* internal_stats = stats_->internal_stats(cf_id);
  if (internal_stats == nullptr) {
    return;
  }
  InternalOpStats* internal_op_stats =
      internal_stats->GetInternalOpStatsForType(InternalOpType::GC);
  assert(internal_op_stats != nullptr);
  AddStats(internal_op_stats, InternalOpStatsType::COUNT);
  AddStats(internal_op_stats, InternalOpStatsType::BYTES_READ,
           metrics_.gc_bytes_read);
  AddStats(internal_op_stats, InternalOpStatsType::BYTES_WRITTEN,
           metrics_.gc_bytes_written);
  AddStats(internal_op_stats, InternalOpStatsType::IO_BYTES_READ,
           io_bytes_read_);
  AddStats(internal_op_stats, InternalOpStatsType::IO_BYTES_WRITTEN,
           io_bytes_written_);
  AddStats(internal_op_stats, InternalOpStatsType::INPUT_FILE_NUM,
           metrics_.gc_num_files);
  AddStats(internal_op_stats, InternalOpStatsType::OUTPUT_FILE_NUM,
           metrics_.gc_num_new_files);
  AddStats(internal_op_stats, InternalOpStatsType::GC_SAMPLING_MICROS,
           metrics_.gc_sampling_micros);
  AddStats(internal_op_stats, InternalOpStatsType::GC_READ_LSM_MICROS,
           metrics_.gc_read_lsm_micros);
  AddStats(internal_op_stats, InternalOpStatsType::GC_UPDATE_LSM_MICROS,
           metrics_.gc_update_lsm_micros);
}

}  // namespace mblobdb
}  // namespace rocksdb
