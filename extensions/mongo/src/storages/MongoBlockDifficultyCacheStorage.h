#pragma once
#include "mongo/src/ExternalCacheStorage.h"
#include <memory>

namespace catapult { namespace mongo { class MongoDatabase; } }

namespace catapult { namespace mongo { namespace storages {

	/// Creates a mongo block difficulty cache storage around \a database and \a difficultyHistorySize.
	std::unique_ptr<ExternalCacheStorage> CreateMongoBlockDifficultyCacheStorage(MongoDatabase&& database, uint64_t difficultyHistorySize);
}}}