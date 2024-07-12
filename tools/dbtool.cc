//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//

#include "rocksdb/cache.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/utilities/options_util.h"
#include "options/db_options.h"
#include "db/dbformat.h"
#include "db/write_controller.h"
#include "db/version_set.h"
#include "db/job_context.h"
#include "db/db_impl/db_impl.h"
#include "db/column_family.h"

using namespace rocksdb;

int main(int argc, char** argv)
{
    std::string db_path = argv[1];
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descs;
    rocksdb::DBOptions db_opts;
    rocksdb::Status s = rocksdb::LoadLatestOptions(db_path, rocksdb::Env::Default(), &db_opts, &cf_descs, false);
    if (s.ok()) {
        printf("load latest option success! path[%s]\n", db_path.c_str());
    } else {
        printf("load latest option failed! path[%s] err[%s]\n", db_path.c_str(), s.ToString().c_str());
        return false;
    }

    std::string fname = argv[2];
    uint64_t number;
    FileType type;
    WalFileType log_type = kArchivedLogFile;
    if (!ParseFileName(fname, &number, &type, &log_type) ||
        (type != kTableFile && type != kLogFile)) {
        printf("DeleteFile %s failed.\n", fname.c_str());
        return -1;
    }
    printf("fname[%s] num[%lu] type[%d] log_type[%d]\n", fname.c_str(), number, type, log_type);

    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_size = 16384;
    table_options.cache_index_and_filter_blocks = true;

    for (auto &cf_desc : cf_descs) {
        cf_desc.options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
    }

    db_opts.db_paths.emplace_back(db_path,
                                   std::numeric_limits<uint64_t>::max());

    rocksdb::ImmutableDBOptions db_options(db_opts);
    rocksdb::EnvOptions soptions;

    std::shared_ptr<Cache> tc(NewLRUCache(db_opts.max_open_files - 10,
                                        db_opts.table_cache_numshardbits));

    rocksdb::WriteController wc(db_opts.delayed_write_rate);
    rocksdb::WriteBufferManager wb(db_opts.db_write_buffer_size);
    rocksdb::VersionSet versions(db_path, &db_options, soptions, tc.get(), &wb, &wc,
                                /*block_cache_tracer=*/nullptr);
    versions.Recover(cf_descs);

    Env* env_ = Env::Default();
    std::vector<std::string> column_families;
    versions.ListColumnFamilies(&column_families, db_path, env_);
    for (auto column_family : column_families) {
        printf("column_family: %s\n", column_family.c_str());
    }

    {
        rocksdb::VersionSet versions2(db_path, &db_options, soptions, tc.get(), &wb, &wc,
                                /*block_cache_tracer=*/nullptr);
        std::string manifest_filename;
        uint64_t manifest_file_number;
        versions2.GetCurrentManifestPath(db_path, env_, &manifest_filename, &manifest_file_number);
        printf("manifest_filename: %s manifest_file_number: %lu\n", manifest_filename.c_str(), manifest_file_number);

        for (auto &cf_desc : cf_descs) {
            if (cf_desc.name == "default") continue;

            rocksdb::Options options(db_opts, cf_desc.options);
            versions2.DumpManifest(options, manifest_filename, true);
        }
    }

    int level;
    FileMetaData* metadata;
    rocksdb::ColumnFamilyData* cfd;
    VersionEdit edit;
    Status status;
    JobContext job_context(1, true);

    {
        InstrumentedMutex mutex_;
        InstrumentedMutexLock l(&mutex_);
        status = versions.GetMetadataForFile(number, &level, &metadata, &cfd);
        if (!status.ok()) {
            printf("DeleteFile %s failed. File not found\n", fname.c_str());
            return -1;
        }

        auto* vstoreage = cfd->current()->storage_info();
        for (int i = level + 1; i < cfd->NumberLevels(); i++) {
            if (vstoreage->NumLevelFiles(i) != 0) {
                printf("DeleteFile %s FAILED. File not in last level\n", fname.c_str());
                // return -1;
            }
        }

        if (level == 0 &&
            vstoreage->LevelFiles(0).back()->fd.GetNumber() != number) {
            printf("DeleteFile %s failed ---"
                   " target file in level 0 must be the oldest.",
                   fname.c_str());
            // return -1;
        }

        Directories directories_;
        s = directories_.SetDirectories(env_, db_path,
                                                db_opts.wal_dir,
                                                db_opts.db_paths);

        edit.SetColumnFamily(cfd->GetID());
        edit.DeleteFile(level, number);
        status = versions.LogAndApply(cfd, *cfd->GetLatestMutableCFOptions(),
                                        &edit, &mutex_, directories_.GetDbDir());
        if (status.ok()) {
            printf("log apply resulst: %s", status.ToString().c_str());
        }
    }

    {
        rocksdb::VersionSet versions2(db_path, &db_options, soptions, tc.get(), &wb, &wc,
                                /*block_cache_tracer=*/nullptr);
        std::string manifest_filename;
        uint64_t manifest_file_number;
        versions2.GetCurrentManifestPath(db_path, env_, &manifest_filename, &manifest_file_number);
        printf("manifest_filename: %s manifest_file_number: %lu\n", manifest_filename.c_str(), manifest_file_number);

        for (auto &cf_desc : cf_descs) {
            if (cf_desc.name == "default") continue;

            rocksdb::Options options(db_opts, cf_desc.options);
            versions2.DumpManifest(options, manifest_filename, true);
        }
    }

    return 0;
}
