# Networked Directory Synchronization Application

This project implements a directory synchronization system between a central server and multiple networked clients using TCP sockets, multithreading, and the Linux inotify API.  
The application ensures that all connected clients maintain a synchronized copy of the server’s designated sync directory, excluding files defined in each client’s ignore list.  

--------------------------------------------------------------------------------

## Overview

The system consists of two components:

1. Server (syncserver)
   - Monitors a designated sync directory (recursively).
   - Tracks file creation, deletion, and movement events using inotify.
   - Communicates updates to all connected clients.
   - Transfers new files to clients, excluding ignored extensions.

2. Client (syncclient)
   - Maintains a local mirror of the server’s sync directory.
   - Provides an ignore list of file extensions (e.g., .mp4,.exe,.zip).
   - Receives directory updates from the server in real-time.
   - Ignores syncing of specified file types.

--------------------------------------------------------------------------------

## Features

Server
- Recursive directory monitoring using the Linux inotify API.
- Tracks:
  - File creation
  - File deletion
  - File movement
- Supported inotify flags: IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
- Multithreaded design for:
  - Handling multiple clients simultaneously.
  - Monitoring the sync directory concurrently.
- Per-client ignore list handling in memory.
- Clients identified by their IP addresses.

Client
- Reads an ignore list file containing comma-separated file extensions.
- Sends the ignore list to the server upon connection.
- Maintains a synchronized directory that mirrors the server’s sync directory (excluding ignored file types).
- Multithreaded design to:
  - Receive and apply updates from the server.
  - Perform local synchronization.

--------------------------------------------------------------------------------

## Installation & Compilation

Compile both the server and client using:

gcc -o syncserver syncserver.c -lpthread
gcc -o syncclient syncclient.c -lpthread

--------------------------------------------------------------------------------

## Usage

Start the Server
./syncserver path_to_local_directory port max_clients

Example:
./syncserver ./server_sync 5000 5

Start the Client
./syncclient path_to_local_directory path_to_ignore_list_file

Example:
./syncclient ./client_sync ignore.txt

--------------------------------------------------------------------------------

## Ignore List File

- The ignore list file contains a comma-separated list of file extensions.  
- Example (ignore.txt):

.mp4,.exe,.zip

- The client reads this file and sends its contents to the server immediately after connecting.
- The format for sending this list is custom-defined and documented within the code.

--------------------------------------------------------------------------------

## Tech Stack
- C
- TCP sockets for communication
- Pthreads for multithreading
- inotify API (<sys/inotify.h>) for directory monitoring
- Linux environment

--------------------------------------------------------------------------------

## Author
Developed by Aasritha
