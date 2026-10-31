#pragma once

#include <optional>
#include <steam/steamnetworkingtypes.h>

class CConfig
{
public:
	// "-dbpath"
	const char *dbpath = nullptr;

	// "-listen"
	std::optional<SteamNetworkingIPAddr> listenaddress = std::nullopt;

	// "-cachebypasspassword"
	const char *cachebypasspassword = nullptr;

public:
	bool Init(int argc, char **argv);
};

CConfig &Config();
