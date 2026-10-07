# CS2 Egg Checker Server

Join and check your console for your pet information

## How does this work?

Pets are technically items in your inventory, they don't show up anywhere in your inventory but they are defined as actual items.

When you connect to a server it will receive a list of items you currently have equipped, plus your pet.

The pet includes some information such as time and date when hatched, when your food expires, when it grows to the next stage, the pet age (Egg, Chick, Pullet, Adult), etc.

At the time of writing **the only way** to see this data is through a server or client modifications, Valve has not provided any official way to view this data.

---

Because I don't want to run a full CS2 server (Over 70 GB) and bother with constant updates this simply reimplements the networking.

## Usage

`server.exe <options>`

Options:

- `-dbpath <file>`: Where to write our analytics, sqlite3 file, no analytics if not provided
- `-listen <address>`: What IP to listen on, P2P if not provided

## Building

Requirements: CMake and a C++ compiler with C++23 support

1. `cmake --preset release`
2. `cmake --build --preset release`
