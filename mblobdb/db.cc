#include "mblobdb/db.h"

#include "db_impl.h"

namespace rocksdb {
namespace mblobdb {

Status NubaseDB::Open(const NubaseOptions& options, const std::string& dbname,
                     NubaseDB** db) {
  NubaseDBOptions db_options(options);
  NubaseCFOptions cf_options(options);
  std::vector<NubaseCFDescriptor> descs;
  descs.emplace_back(kDefaultColumnFamilyName, cf_options);
  std::vector<ColumnFamilyHandle*> handles;
  Status s = NubaseDB::Open(db_options, dbname, descs, &handles, db);
  if (s.ok()) {
    assert(handles.size() == 1);
    // DBImpl is always holding the default handle.
    delete handles[0];
  }
  return s;
}

Status NubaseDB::Open(const NubaseDBOptions& db_options,
                     const std::string& dbname,
                     const std::vector<NubaseCFDescriptor>& descs,
                     std::vector<ColumnFamilyHandle*>* handles, NubaseDB** db) {
  auto impl = new NubaseDBImpl(db_options, dbname);
  auto s = impl->Open(descs, handles);
  if (s.ok()) {
    *db = impl;
  } else {
    *db = nullptr;
    delete impl;
  }
  return s;
}

}  // namespace mblobdb
}  // namespace rocksdb
