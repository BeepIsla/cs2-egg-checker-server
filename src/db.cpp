#include "db.hpp"
#include "helpers.hpp"
#include <chrono>
#include <gcsdk_gcmessages.pb.h>
#include <print>
#include <sqlite3.h>
#include <utility>

static const char *SQL_TABLE = "CREATE TABLE IF NOT EXISTS egg_checker_analytics("
                               "  steamid TEXT NOT NULL,"
                               "  timestamp INTEGER NOT NULL,"
                               "  result INTEGER NOT NULL,"
                               "  extra TEXT,"
                               "  socache BLOB"
                               ") STRICT;";
static const char *SQL_STMT  = "INSERT INTO egg_checker_analytics"
                               "(steamid, timestamp, result, extra, socache)"
                               "VALUES"
                               "(?, ?, ?, ?, ?);";

void CDB::Init(const char *dbpath)
{
	if (m_db || !dbpath || dbpath[0] == '\0')
		return;

	if (sqlite3_open(dbpath, &m_db) != SQLITE_OK)
	{
		std::println("Failed to open database '{}': {}", dbpath, sqlite3_errmsg(m_db));
		Close();
		return;
	}

	// Yes this is quite high but we only serve ~1 user per minute anyways, a single query shortly locking up the thread isn't a big deal in such a tiny scenario
	sqlite3_busy_timeout(m_db, 1000);

	char *errmsg = nullptr;
	if (sqlite3_exec(m_db, SQL_TABLE, nullptr, nullptr, &errmsg) != SQLITE_OK)
	{
		std::println("Failed to execute SQLite table creation query: {}", errmsg);
		sqlite3_free(errmsg);
		Close();
		return;
	}

	if (sqlite3_prepare_v2(m_db, SQL_STMT, -1, &m_stmt, nullptr) != SQLITE_OK)
	{
		std::println("Failed to create prepared statement for analytics: {}", sqlite3_errmsg(m_db));
		Close();
		return;
	}
}

void CDB::Close()
{
	if (m_stmt)
	{
		sqlite3_finalize(m_stmt);
		m_stmt = nullptr;
	}

	if (m_db)
	{
		sqlite3_close(m_db);
		m_db = nullptr;
	}
}

void CDB::SyncTrackUser(CSteamID steamID, EUserResult result, const char *extra, const CMsgSOCacheSubscribed *socache)
{
	if (!m_stmt)
		return;

	auto now = std::chrono::system_clock::now().time_since_epoch();
	auto sec = std::chrono::duration_cast<std::chrono::seconds>(now);

	std::string arg1_steamid = std::format("{}", steamID.ConvertToUint64());

	sqlite3_bind_text(m_stmt, 1, arg1_steamid.c_str(), static_cast<int>(arg1_steamid.size()), SQLITE_TRANSIENT);
	sqlite3_bind_int64(m_stmt, 2, static_cast<sqlite3_int64>(sec.count()));
	sqlite3_bind_int(m_stmt, 3, static_cast<int>(std::to_underlying(result)));
	if (extra)
		sqlite3_bind_text(m_stmt, 4, extra, -1, SQLITE_TRANSIENT);
	else
		sqlite3_bind_null(m_stmt, 4);

	if (socache)
	{
		std::string socache_blob;
		if (socache->SerializeToString(&socache_blob))
			sqlite3_bind_blob(m_stmt, 5, socache_blob.data(), static_cast<int>(socache_blob.size()), SQLITE_TRANSIENT);
		else
			sqlite3_bind_null(m_stmt, 5);
	}
	else
	{
		sqlite3_bind_null(m_stmt, 5);
	}

	if (sqlite3_step(m_stmt) != SQLITE_DONE)
		std::println("Failed to track user {}: {}", steamID, sqlite3_errmsg(m_db));
	sqlite3_reset(m_stmt);
	sqlite3_clear_bindings(m_stmt);
}

CDB &DB()
{
	static CDB s_DB;
	return s_DB;
}
