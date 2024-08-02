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

    ColumnFamilySet* column_family_sets = versions.GetColumnFamilySet();
    for (auto column_family : column_families) {
        printf("column_family: %s id: %d\n", column_family.c_str(), column_family_sets->GetColumnFamily(column_family)->GetID());
    }

    std::string op = argv[2];
    std::string column_family_name = argv[3];

    if (op == "delete") {
        InstrumentedMutex mutex_;
        InstrumentedMutexLock l(&mutex_);

        if (argc == 3) {
            auto cfd = column_family_sets->GetColumnFamily(column_family_name);
            if (!cfd) {
                printf("column_family_name %s not found!", column_family_name.c_str());
                return -1;
            }
        }
        auto cfd = column_family_sets->GetColumnFamily(column_family_name);
        if (!cfd) {
            printf("column_family_name %s not found!", column_family_name.c_str());
            return -1;
        }

        VersionEdit edit;
        edit.DropColumnFamily();
        edit.SetColumnFamily(cfd->GetID());

        auto status = versions.LogAndApply(cfd, *cfd->GetLatestMutableCFOptions(), &edit, &mutex_);
        if (status.ok()) {
            printf("log apply delete resulst: %s", status.ToString().c_str());
        }
    } else if (op == "create") {
        InstrumentedMutex mutex_;
        InstrumentedMutexLock l(&mutex_);

        auto cfd = column_family_sets->GetColumnFamily(column_family_name);
        if (cfd) {
            printf("column_family_name %s already exist!", column_family_name.c_str());
            return -1;
        }

        VersionEdit edit;
        edit.AddColumnFamily(column_family_name);
        // edit.SetColumnFamily(cfd->GetID());

        auto status = versions.LogAndApply(cfd, *cfd->GetLatestMutableCFOptions(), &edit, &mutex_);
        if (status.ok()) {
            printf("log apply create resulst: %s", status.ToString().c_str());
        }
    }

    return 0;
}
