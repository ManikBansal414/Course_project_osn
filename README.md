# Distributed File System (DFS)

A comprehensive distributed file system implementation featuring concurrent access, sentence-level locking, access control, and efficient file search capabilities.

## 🚀 Features

### Core Functionalities (150 marks)
- ✅ **VIEW**: List files with various flags (-a, -l, -al)
- ✅ **READ**: Read complete file contents
- ✅ **CREATE**: Create new files
- ✅ **WRITE**: Write to files at word-level with sentence locking
- ✅ **DELETE**: Delete files (owner only)
- ✅ **INFO**: Get detailed file metadata
- ✅ **STREAM**: Stream file content word-by-word (0.1s delay)
- ✅ **LIST**: List all registered users
- ✅ **ACCESS CONTROL**: Add/remove read/write permissions
- ✅ **EXEC**: Execute file contents as shell commands
- ✅ **UNDO**: Revert last file change

### System Requirements (40 marks)
- ✅ **Data Persistence**: Files and metadata stored persistently
- ✅ **Access Control**: Permission-based file access
- ✅ **Logging**: Comprehensive logging for NM and SS
- ✅ **Error Handling**: Clear error codes and messages
- ✅ **Efficient Search**: O(1) hash-based file lookup with LRU caching

## 📁 System Architecture

```
┌─────────────┐         ┌──────────────┐         ┌─────────────────┐
│   Client    │◄───────►│ Name Server  │◄───────►│ Storage Server  │
│  (Client)   │         │     (NM)     │         │      (SS)       │
└─────────────┘         └──────────────┘         └─────────────────┘
                               │                          │
                               ▼                          ▼
                        ┌─────────────┐          ┌─────────────┐
                        │  Metadata   │          │   Files     │
                        │   Storage   │          │  Storage    │
                        └─────────────┘          └─────────────┘
```

### Components

1. **Name Server (NM)**: Central coordinator
   - Maintains file-to-storage-server mapping
   - Handles client registration
   - Manages metadata and access control
   - Efficient file lookup with hash tables
   - LRU caching for recent searches
   - Port: 8080

2. **Storage Server (SS)**: Data storage and retrieval
   - Stores actual file data
   - Handles sentence-level locking
   - Supports undo functionality
   - Direct client connections for data operations
   - Configurable ports (default: 9000 for clients, 9001 for NM)

3. **Client**: User interface
   - Interactive command-line interface
   - Username-based authentication
   - Direct SS connection for data operations

## 🛠️ Building the Project

### Prerequisites
- GCC compiler
- POSIX-compatible system (Linux/Unix)
- pthread library

### Compilation

```bash
# Build all components
make

# Build individual components
make name_server
make storage_server
make client

# Clean build artifacts
make clean
```

## 🚀 Running the System

### Step 1: Start the Name Server
```bash
./name_server
```
The Name Server starts on port 8080 and waits for Storage Servers and Clients to connect.

### Step 2: Start Storage Server(s)
```bash
# Default ports (9000 for clients, 9001 for NM)
./storage_server

# Custom ports
./storage_server <client_port> <nm_port>

# Example:
./storage_server 9000 9001
```

You can start multiple storage servers with different ports.

### Step 3: Start Client(s)
```bash
./client
```
Enter a username when prompted. Multiple clients can connect simultaneously.

## 📝 Usage Examples

### Example 1: Creating and Reading a File

```
Client> CREATE myfile.txt
File Created Successfully!

Client> WRITE myfile.txt 0
Lock acquired. Enter word updates (word_index content), type ETIRW to finish:
Client: 1 Hello world.
Client: ETIRW
Write Successful!

Client> READ myfile.txt
Hello world.
```

### Example 2: Viewing Files

```
Client> VIEW
--> myfile.txt

Client> VIEW -l
---------------------------------------------------------
|  Filename  | Words | Chars | Last Access Time | Owner |
|------------|-------|-------|------------------|-------|
| myfile.txt |   2   |  12   | 2025-11-04 10:30 | user1 |
---------------------------------------------------------

Client> VIEW -a
--> myfile.txt
--> otherfile.txt
```

### Example 3: Access Control

```
Client> ADDACCESS -R myfile.txt user2
Access granted successfully!

Client> ADDACCESS -W myfile.txt user2
Access granted successfully!

Client> INFO myfile.txt
--> File: myfile.txt
--> Owner: user1
--> Created: 2025-11-04 10:25
--> Last Modified: 2025-11-04 10:30
--> Size: 12 bytes
--> Access: user1 (RW), user2 (RW)
--> Last Accessed: 2025-11-04 10:30 by user1

Client> REMACCESS myfile.txt user2
Access removed successfully!
```

### Example 4: Complex Write Operations

```
Client> WRITE mouse.txt 0
Lock acquired. Enter word updates (word_index content), type ETIRW to finish:
Client: 1 Im just a mouse.
Client: ETIRW
Write Successful!

Client> WRITE mouse.txt 1
Lock acquired. Enter word updates (word_index content), type ETIRW to finish:
Client: 1 I dont like PNS
Client: ETIRW
Write Successful!

Client> READ mouse.txt
Im just a mouse. I dont like PNS

Client> WRITE mouse.txt 0
Lock acquired. Enter word updates (word_index content), type ETIRW to finish:
Client: 4 deeply mistaken hollow lil
Client: 6 pocket-sized
Client: ETIRW
Write Successful!

Client> READ mouse.txt
Im just a deeply mistaken hollow pocket-sized lil mouse. I dont like PNS
```

### Example 5: Streaming Content

```
Client> STREAM myfile.txt
Hello world. This is a test file.
(Each word appears with 0.1 second delay)
```

### Example 6: Execute File

```
Client> READ commands.txt
echo "Hello from DFS"
ls -la
pwd

Client> EXEC commands.txt
Hello from DFS
total 64
drwxr-xr-x 3 user user 4096 Nov  4 10:30 .
drwxr-xr-x 5 user user 4096 Nov  4 10:25 ..
/home/user/dfs
```

### Example 7: Undo Changes

```
Client> READ test.txt
Original content.

Client> WRITE test.txt 0
Client: 1 Modified
Client: ETIRW
Write Successful!

Client> READ test.txt
Modified content.

Client> UNDO test.txt
Undo Successful!

Client> READ test.txt
Original content.
```

## 🎯 Command Reference

| Command | Syntax | Description |
|---------|--------|-------------|
| VIEW | `VIEW [-a] [-l]` | List files (a=all, l=detailed) |
| READ | `READ <filename>` | Display file contents |
| CREATE | `CREATE <filename>` | Create new empty file |
| WRITE | `WRITE <filename> <sent#>` | Write to file sentence |
| DELETE | `DELETE <filename>` | Delete file (owner only) |
| INFO | `INFO <filename>` | Show file metadata |
| STREAM | `STREAM <filename>` | Stream file word-by-word |
| LIST | `LIST` | List all users |
| ADDACCESS | `ADDACCESS -R/-W <file> <user>` | Grant access |
| REMACCESS | `REMACCESS <file> <user>` | Remove access |
| EXEC | `EXEC <filename>` | Execute as shell commands |
| UNDO | `UNDO <filename>` | Revert last change |
| HELP | `HELP` | Show command list |
| EXIT | `EXIT` | Exit client |

## 📂 File Structure

```
.
├── common.h              # Shared definitions and structures
├── common.c              # Common utility functions
├── name_server.c         # Name Server implementation
├── storage_server.c      # Storage Server implementation
├── client.c              # Client implementation
├── Makefile              # Build configuration
├── README.md             # This file
├── storage/              # Storage directory (created at runtime)
├── undo/                 # Undo backup directory (created at runtime)
├── nm_metadata.dat       # Name Server metadata (persistent)
├── nm_log.txt            # Name Server log file
└── ss_log.txt            # Storage Server log file
```

## 🔧 Technical Details

### File Sentence Model
- Sentences end with `.`, `!`, or `?`
- Words are separated by spaces
- Sentence delimiters in the middle of words create new sentences
- Example: `"e.g."` creates two sentences: `"e."` and `"g."`

### Concurrency
- Multiple clients can connect simultaneously
- Sentence-level locking prevents concurrent edits to the same sentence
- Multiple sentences in the same file can be edited concurrently

### Write Operation
1. Client requests write lock for a sentence
2. SS acquires lock and grants permission
3. Client sends word updates (word_index + content)
4. Updates are applied sequentially
5. Client sends `ETIRW` to finish
6. SS saves changes and releases lock

### Data Persistence
- Name Server: Metadata stored in `nm_metadata.dat`
- Storage Server: Files stored in `storage/` directory
- Undo: Previous versions in `undo/` directory

### Error Codes
- `ERR_SUCCESS (0)`: Operation successful
- `ERR_FILE_NOT_FOUND (1)`: File doesn't exist
- `ERR_UNAUTHORIZED (2)`: Access denied
- `ERR_FILE_EXISTS (3)`: File already exists
- `ERR_INVALID_INDEX (4)`: Invalid sentence/word index
- `ERR_SENTENCE_LOCKED (5)`: Sentence being edited
- `ERR_NO_STORAGE_SERVER (6)`: No SS available
- `ERR_CONNECTION_FAILED (7)`: Connection error
- `ERR_INVALID_COMMAND (8)`: Unknown command
- `ERR_SERVER_ERROR (9)`: Internal server error
- `ERR_NO_UNDO_AVAILABLE (10)`: No undo data

## 📊 Performance Features

### Efficient Search (O(1))
- Hash table for file lookup
- LRU cache for frequently accessed files
- Cache hit logging for monitoring

### Logging
- Timestamped entries for all operations
- Separate logs for NM and SS
- IP addresses and ports logged
- Operation details for debugging

### Access Control
- Owner-based permissions
- Read (R) and Write (W) access levels
- Owner always has RW access
- Per-user access control lists

## 🐛 Troubleshooting

### Port Already in Use
```bash
# Check what's using the port
lsof -i :8080

# Kill the process
kill -9 <PID>
```

### Connection Refused
- Ensure Name Server is running before starting SS or Client
- Check firewall settings
- Verify correct IP addresses and ports

### File Not Found
- Ensure Storage Server has necessary directories
- Check file permissions
- Verify file was created successfully

### Sentence Locked Error
- Another user is editing the sentence
- Wait for the lock to be released
- Check log files for lock status

## 📈 Future Enhancements (Bonus Features)

Potential additions:
- Hierarchical folder structure
- Checkpoints and versioning
- Access request system
- Fault tolerance with replication
- Multiple Storage Server support with load balancing

## 👥 Team Information

This project implements a distributed file system as specified in the OSN course requirements.

## 📄 License

This is an academic project for educational purposes.

## 🙏 Acknowledgments

- CMU Distributed Systems resources
- POSIX C Library documentation
- OSN Course TAs and instructors

---

**Note**: This implementation focuses on core functionality and learning objectives. For production use, additional security, error handling, and optimization would be required.
