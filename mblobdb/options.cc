#include "mblobdb/options.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include "logging/logging.h"
#include "options/options_helper.h"
#include "rocksdb/convenience.h"

namespace rocksdb {
namespace mblobdb {

void NubaseDBOptions::Dump(Logger* logger) const {
  ROCKS_LOG_HEADER(logger, "NubaseDBOptions.dirname                    : %s",
                   dirname.c_str());
  ROCKS_LOG_HEADER(logger, "NubaseDBOptions.disable_background_gc      : %d",
                   static_cast<int>(disable_background_gc));
  ROCKS_LOG_HEADER(logger,
                   "NubaseDBOptions.max_background_gc          : %" PRIi32,
                   max_background_gc);
  ROCKS_LOG_HEADER(logger,
                   "NubaseDBOptions.purge_obsolete_files_period_sec: %" PRIu32,
                   purge_obsolete_files_period_sec);
  ROCKS_LOG_HEADER(logger,
                   "NubaseDBOptions.titan_stats_dump_period_sec: %" PRIu32,
                   titan_stats_dump_period_sec);
}

NubaseCFOptions::NubaseCFOptions(const ColumnFamilyOptions& cf_opts,
                               const ImmutableTitanCFOptions& immutable_opts,
                               const MutableTitanCFOptions& mutable_opts)
    : ColumnFamilyOptions(cf_opts),
      min_blob_size(immutable_opts.min_blob_size),
      blob_file_compression(immutable_opts.blob_file_compression),
      blob_file_target_size(immutable_opts.blob_file_target_size),
      blob_cache(immutable_opts.blob_cache),
      max_gc_batch_size(immutable_opts.max_gc_batch_size),
      min_gc_batch_size(immutable_opts.min_gc_batch_size),
      blob_file_discardable_ratio(immutable_opts.blob_file_discardable_ratio),
      sample_file_size_ratio(immutable_opts.sample_file_size_ratio),
      merge_small_file_threshold(immutable_opts.merge_small_file_threshold),
      blob_run_mode(mutable_opts.blob_run_mode),
      gc_merge_rewrite(mutable_opts.gc_merge_rewrite) {}

void NubaseCFOptions::Dump(Logger* logger) const {
  ROCKS_LOG_HEADER(logger,
                   "NubaseCFOptions.min_blob_size                : %" PRIu64,
                   min_blob_size);
  std::string compression_str = "unknown";
  for (auto& compression_type : compression_type_string_map) {
    if (compression_type.second == blob_file_compression) {
      compression_str = compression_type.first;
      break;
    }
  }
  ROCKS_LOG_HEADER(logger, "NubaseCFOptions.blob_file_compression        : %s",
                   compression_str.c_str());
  ROCKS_LOG_HEADER(logger,
                   "NubaseCFOptions.blob_file_target_size        : %" PRIu64,
                   blob_file_target_size);
  ROCKS_LOG_HEADER(logger, "NubaseCFOptions.blob_cache                   : %p",
                   blob_cache.get());
  if (blob_cache != nullptr) {
    ROCKS_LOG_HEADER(logger, "%s", blob_cache->GetPrintableOptions().c_str());
  }
  ROCKS_LOG_HEADER(logger,
                   "NubaseCFOptions.max_gc_batch_size            : %" PRIu64,
                   max_gc_batch_size);
  ROCKS_LOG_HEADER(logger,
                   "NubaseCFOptions.min_gc_batch_size            : %" PRIu64,
                   min_gc_batch_size);
  ROCKS_LOG_HEADER(logger, "NubaseCFOptions.blob_file_discardable_ratio  : %lf",
                   blob_file_discardable_ratio);
  ROCKS_LOG_HEADER(logger, "NubaseCFOptions.sample_file_size_ratio       : %lf",
                   sample_file_size_ratio);
  ROCKS_LOG_HEADER(logger,
                   "NubaseCFOptions.merge_small_file_threshold   : %" PRIu64,
                   merge_small_file_threshold);
  std::string blob_run_mode_str = "unknown";
  if (blob_run_mode_to_string.count(blob_run_mode) > 0) {
    blob_run_mode_str = blob_run_mode_to_string.at(blob_run_mode);
  }
  ROCKS_LOG_HEADER(logger, "NubaseCFOptions.blob_run_mode                : %s",
                   blob_run_mode_str.c_str());
}

std::map<TitanBlobRunMode, std::string>
    TitanOptionsHelper::blob_run_mode_to_string = {
        {TitanBlobRunMode::kNormal, "kNormal"},
        {TitanBlobRunMode::kReadOnly, "kReadOnly"},
        {TitanBlobRunMode::kFallback, "kFallback"}};

std::unordered_map<std::string, TitanBlobRunMode>
    TitanOptionsHelper::blob_run_mode_string_map = {
        {"kNormal", TitanBlobRunMode::kNormal},
        {"kReadOnly", TitanBlobRunMode::kReadOnly},
        {"kFallback", TitanBlobRunMode::kFallback}};

}  // namespace mblobdb
}  // namespace rocksdb
