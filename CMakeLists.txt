cmake_minimum_required(VERSION 3.20)

project(cs2-egg-checker-server)

# Set some things globally
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_definitions(
	_CRT_SECURE_NO_WARNINGS
	_ITERATOR_DEBUG_LEVEL=0
	_GLIBCXX_USE_CXX11_ABI=0
)

# sqlite3
add_library(sqlite3 STATIC
	submodules/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC
	submodules/sqlite
)

# Our own executable
add_executable(cs2-egg-checker-server
	src/main.cpp
	src/buffer.cpp
	src/client.cpp
	src/server.cpp
	src/gcclient.cpp
	src/db.cpp

	# .\submodules\s2sdk\devtools\bin\protoc.exe ^
	# 	--proto_path=./submodules/protobufs/csgo ^
	# 	--cpp_out=./src/protobufs ^
	# 	valveextensions.proto ^
	# 	source2_steam_stats.proto ^
	# 	connectionless_netmessages.proto ^
	# 	steammessages.proto ^
	# 	netmessages.proto ^
	# 	usermessages.proto ^
	# 	network_connection.proto ^
	# 	networkbasetypes.proto ^
	# 	base_gcmessages.proto ^
	# 	engine_gcmessages.proto ^
	# 	gcsystemmsgs.proto ^
	# 	gcsdk_gcmessages.proto ^
	# 	cstrike15_gcmessages.proto
	src/protobufs/valveextensions.pb.cc
	src/protobufs/source2_steam_stats.pb.cc
	src/protobufs/connectionless_netmessages.pb.cc
	src/protobufs/steammessages.pb.cc
	src/protobufs/netmessages.pb.cc
	src/protobufs/usermessages.pb.cc
	src/protobufs/network_connection.pb.cc
	src/protobufs/networkbasetypes.pb.cc
	src/protobufs/base_gcmessages.pb.cc
	src/protobufs/engine_gcmessages.pb.cc
	src/protobufs/gcsystemmsgs.pb.cc
	src/protobufs/gcsdk_gcmessages.pb.cc
	src/protobufs/cstrike15_gcmessages.pb.cc
)

target_include_directories(cs2-egg-checker-server PUBLIC
	src/protobufs
	${CMAKE_SOURCE_DIR}/submodules/steamworks/public
	${CMAKE_SOURCE_DIR}/submodules/s2sdk/thirdparty/protobuf-3.21.8/src
)

target_compile_definitions(cs2-egg-checker-server PUBLIC
	$<$<CONFIG:Debug>:DEBUG>
	$<$<CONFIG:Release>:RELEASE>
	$<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_link_libraries(cs2-egg-checker-server PUBLIC
	sqlite3
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	target_compile_options(cs2-egg-checker-server PUBLIC
		-Wno-invalid-offsetof
	)
endif()

# Not going to bother with 32 bit
if(WIN32)
	target_link_directories(cs2-egg-checker-server PUBLIC
		${CMAKE_SOURCE_DIR}/submodules/steamworks/redistributable_bin/win64
		${CMAKE_SOURCE_DIR}/submodules/s2sdk/lib/public/win64/2015
	)
	target_link_libraries(cs2-egg-checker-server PUBLIC
		steam_api64.lib
		libprotobuf.lib
	)
	configure_file(${CMAKE_SOURCE_DIR}/submodules/steamworks/redistributable_bin/win64/steam_api64.dll ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/steam_api64.dll COPYONLY)
else()
	target_link_directories(cs2-egg-checker-server PUBLIC
		${CMAKE_SOURCE_DIR}/submodules/steamworks/redistributable_bin/linux64
		${CMAKE_SOURCE_DIR}/submodules/s2sdk/lib/linux64/release
	)
	target_link_libraries(cs2-egg-checker-server PUBLIC
		libsteam_api.so
		libprotobuf.a
	)
	configure_file(${CMAKE_SOURCE_DIR}/submodules/steamworks/redistributable_bin/linux64/libsteam_api.so ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libsteam_api.so COPYONLY)
endif()

# Generate protobufs
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

target_include_directories(cs2-egg-checker-server PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")

#include(${CMAKE_SOURCE_DIR}/submodules/protobuf/cmake/protobuf-generate.cmake)
#protobuf_generate(
#	TARGET cs2-egg-checker-server
#	IMPORT_DIRS "${CMAKE_SOURCE_DIR}/submodules/protobufs/csgo"
#	PROTOC_OUT_DIR "${PROTO_BINARY_DIR}"
#)
