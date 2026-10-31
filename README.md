# CS2 Egg Checker Server

Join and check your console for your pet information

I am hosting this, use `connect eggserver.beepisla.com` in the CS2 console to view your pet information, after joining scroll up in your console to view your pet data.

## How does this work?

Pets are technically items in your inventory, they don't show up anywhere in your inventory but they are defined as actual items.

When you connect to a server it will receive a list of items you currently have equipped, plus your pet.

The pet includes some information such as time and date when hatched, when your food expires, when it grows to the next stage, the pet age (Egg, Chick, Pullet, Adult), etc.

At the time of writing **the only way** to see this data is through a server or client modifications, Valve has not provided any official way to view this data.

---

Because I don't want to run a full CS2 server (Over 70 GB) and bother with constant updates this simply reimplements the networking.

## Usage

`cs2-egg-checker-server.exe <options>`

Options:

- `-dbpath <file>`: Where to write our analytics, sqlite3 file, no analytics if not provided (Environment variable: `DB_PATH`)
- `-listen <address>`: What IP to listen on, P2P if not provided (Environment variable: `LISTEN_ADDRESS`)
- `-cachebypasspassword`: If the client provides this password then the cache will be ignored (Environment variable: `CACHE_BYPASS_PASSWORD`)

Command-line arguments take priority over environment variables. Setting an argument to an empty string cancels out the environment variable (e.g. running with `-dbpath ""` overrides `DB_PATH` to disable analytics).

## Building

Requirements: CMake and a C++ compiler with C++23 support

1. `cmake --preset release`
2. `cmake --build --preset release`

### Docker

By default Docker will listen on port `27015`, use `LISTEN_ADDRESS` environment variable to override. You can set `LISTEN_ADDRESS` to an empty string if you want P2P but note that P2P does not work with persistent SteamIDs, so you have to get the server SteamID from the logs every time the container restarts.

1. `docker buildx build -t cs2-egg-checker-server:latest .`
2. `docker run --name cs2-egg-checker-server -p 27015:27015/udp cs2-egg-checker-server:latest`

If you want to log analytics add `-v $(pwd)/data:/data` and `-e DB_PATH=/data/analytics.sqlite`:
