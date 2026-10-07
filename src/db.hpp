#pragma once

#include <steam/steamclientpublic.h>

struct sqlite3;
struct sqlite3_stmt;
class CMsgSOCacheSubscribed;

class CDB
{
public:
	enum class EUserResult
	{
		Success = 0,  // We got the egg and everything went well
		Timeout,      // We got no response for too long and kicked the client (Ticket or SOCache)
		BadTicket,    // Steam auth ticket was invalid or rejected
		NoEgg,        // Received inventory data, but user has no egg / pet
		Disconnected, // Connection closed prematurely before check completed
	};

	CDB() = default;
	~CDB()
	{
		Close();
	}

private:
	sqlite3      *m_db   = nullptr;
	sqlite3_stmt *m_stmt = nullptr;

public:
	void Init(const char *dbpath);
	void Close();
	void SyncTrackUser(CSteamID steamID, EUserResult result, const char *extra = nullptr, const CMsgSOCacheSubscribed *socache = nullptr);
};

CDB &DB();
