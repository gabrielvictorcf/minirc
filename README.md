# minirc
A (minimal) implementation of an IRC client and server.

This is the final project for a Computer Networks course as ICMC@USP, where we were
tasked to implement a version of the [IRC RFC](https://datatracker.ietf.org/doc/html/rfc1459).

Made by:
- Gabriel Victor Cardoso Fernandes - 11878296
- Pedro Henrique Borges Monici - 10816732
- Guilherme Machado Rios - 11222839

## Architecture
<img src="https://github.com/gabrielvictorcf/minirc/blob/main/arquitetura.png" width=1000px>

## Demonstration Video
[VIDEO](https://www.youtube.com/watch?v=DMOOJB9809k)

## How to
1. Open two terminals (or have two hosts in the same network)
2. In one terminal, run `make run_server`
2. In another terminal, run `make run_client` (You can open more terminals to run more clients).
4. Now you can just start chatting! 

OBS: There is some defines in `src/irc.h` to specify the maximum quantity of clients
in the server and channels. You can change if you want!

## Specifications
- linux 5.10.16.3
- gcc (Debian 10.2.1-6) 10.2.1 20210110
