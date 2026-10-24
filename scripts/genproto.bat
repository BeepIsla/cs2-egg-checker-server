rm -r src/protobufs/*.pb.*
.\submodules\s2sdk\devtools\bin\protoc.exe ^
	--proto_path=./submodules/protobufs/csgo ^
	--cpp_out=./src/protobufs ^
	valveextensions.proto ^
	source2_steam_stats.proto ^
	connectionless_netmessages.proto ^
	steammessages.proto ^
	netmessages.proto ^
	usermessages.proto ^
	network_connection.proto ^
	networkbasetypes.proto ^
	base_gcmessages.proto ^
	engine_gcmessages.proto ^
	gcsystemmsgs.proto ^
	gcsdk_gcmessages.proto ^
	cstrike15_gcmessages.proto
