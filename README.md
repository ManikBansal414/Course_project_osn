# Distributed File System

This repository contains a distributed file system project written in C.
It is split into three executables:

- `name_server` manages metadata, users, access control, and server discovery.
- `storage_server` stores file contents and handles file operations.
- `client` provides the command-line interface used to interact with the system.

## Features

- File view, read, create, write, delete, info, and stream operations
- Access control management with read and write permissions
- File execution and undo support
- Folder support
- Checkpoints and restore operations
- Access request workflow
- Search and metrics commands
- Replication and heartbeat support in the server code

## Requirements

- GCC
- POSIX threads support
- A Unix-like environment or compatible shell toolchain

## Build

Build every component from the repository root:

```bash
make
```

You can also build individual targets:

```bash
make name_server
make storage_server
make client
```

## Run

Start the name server first:

```bash
make run-nm
```

Start the storage server next. Optional ports can be supplied through `PORT1` and `PORT2`:

```bash
make run-ss PORT1=9000 PORT2=9001
```

Run the client and pass the name server IP address:

```bash
make run-client
./client <name_server_ip>
```

After launch, the client asks for a username and then displays the command menu.

## Useful Commands

The client supports commands such as:

- `VIEW`
- `READ <filename>`
- `CREATE <filename>`
- `WRITE <filename> <sentence_number>`
- `DELETE <filename>`
- `INFO <filename>`
- `STREAM <filename>`
- `LIST`
- `ADDACCESS -R|-W <filename> <username>`
- `REMACCESS <filename> <username>`
- `EXEC <filename>`
- `UNDO <filename>`
- `CREATEFOLDER <foldername>`
- `MOVE <filename> <foldername>`
- `VIEWFOLDER <foldername>`
- `CHECKPOINT <filename> <tag>`
- `VIEWCHECKPOINT <filename> <tag>`
- `REVERT <filename> <tag>`
- `LISTCHECKPOINTS <filename>`
- `REQUESTACCESS -R|-W <filename>`
- `VIEWREQUESTS`
- `APPROVEREQUEST <username> <filename>`
- `DENYREQUEST <username> <filename>`
- `SEARCH <pattern>`
- `METRICS`

## Build Artifacts

The build generates the three binaries in the repository root along with object files and runtime data files such as logs, metadata, storage, undo, and checkpoint directories.

## Notes

- The name server listens on port `8080` by default.
- The storage server uses internal client and name-server ports defined in the source and can be launched with custom port values through the Makefile.
- The client expects the name server IP address on the command line.