# WRITE Operation Implementation

## Overview
This document describes the fresh implementation of the WRITE operation with word-level editing and sentence-level locking.

## Command Format
```
WRITE <filename> <sentence_number>
<word_index> <content>
<word_index> <content>
...
ETIRW
```

## Key Features

### 1. **Sentence-Level Locking**
- Each sentence in a file has its own independent lock
- Multiple clients can edit different sentences in the same file simultaneously
- Lock identified by: `(filename, sentence_index)` pair
- Non-blocking `trylock()` returns error if sentence is already locked

### 2. **Word-Level Editing**
- Words are indexed starting from 0
- Content is inserted at the specified word_index
- Existing words at and after word_index are shifted right
- Multiple word updates can be sent before ETIRW

### 3. **Sentence Delimiter Handling**
- Delimiters: `.` (period), `!` (exclamation), `?` (question mark)
- **Every delimiter creates a new sentence**, even in: "e.g.", "Umm…", etc.
- After ETIRW, the system checks for delimiters and splits sentences automatically
- Original sentence count updates when delimiters are detected

### 4. **Concurrent Write Safety**
- Uses temporary swap file approach (`.tmp` file)
- Atomic `rename()` operation ensures consistency
- If process crashes mid-write, original file remains intact
- Lock prevents simultaneous edits to the same sentence

### 5. **Undo Support**
- File is saved to undo directory before any modifications
- Allows rollback of changes if needed

## Implementation Flow

```
1. Client sends: WRITE filename sentence_index
2. Server:
   - Reads file content
   - Parses into sentences using delimiters (. ! ?)
   - Validates sentence_index
   - Attempts to acquire sentence lock (trylock)
   - If locked: returns "ERROR: Sentence X is locked by <user>"
   - If success: returns "ACK: Sentence locked. Send word updates, end with ETIRW"

3. Client sends multiple word updates:
   - word_index content
   - Server parses current sentence into words
   - Inserts content at word_index
   - Shifts existing words right
   - Sends "ACK" after each update

4. Client sends: ETIRW
5. Server:
   - Checks working_sentence for delimiters (. ! ?)
   - Splits into multiple sentences if delimiters found
   - Updates sentence array (shifts later sentences if needed)
   - Writes to temporary file: filename.tmp
   - Atomically moves temp file to actual file (rename)
   - Releases sentence lock
   - Returns "Write Successful! Sentence X updated."
```

## Examples

### Example 1: Simple Word Insertion
**Initial file content:**
```
Hello world. This is a test.
```
**Parsed sentences:**
- Sentence 0: "Hello world."
- Sentence 1: "This is a test."

**Client operations:**
```
WRITE test.txt 0
1 beautiful     # Insert "beautiful" at word index 1
ETIRW
```

**Result:**
```
Hello beautiful world. This is a test.
```

### Example 2: Delimiter Creation
**Initial file content:**
```
Hello world
```
**Parsed sentences:**
- Sentence 0: "Hello world"

**Client operations:**
```
WRITE test.txt 0
2 Everyone. Welcome!    # Insert at word index 2
ETIRW
```

**Processing:**
1. After insertion: "Hello world Everyone. Welcome!"
2. Delimiter detection finds: '.' and '!'
3. Splits into 3 sentences:
   - "Hello world Everyone."
   - "Welcome!"
4. Original sentence 0 replaced with "Hello world Everyone."
5. New sentence 1 inserted: "Welcome!"
6. Sentence count increases from 1 to 2

**Final file:**
```
Hello world Everyone. Welcome!
```

### Example 3: Concurrent Writes
**Initial file:**
```
First sentence. Second sentence. Third sentence.
```

**Client A:**
```
WRITE test.txt 0     # Locks sentence 0
0 Modified
ETIRW
```

**Client B (simultaneous):**
```
WRITE test.txt 1     # Locks sentence 1 (different sentence, succeeds)
0 Updated
ETIRW
```

**Client C (tries same sentence as A):**
```
WRITE test.txt 0     # ERROR: Sentence 0 is locked by userA
```

**Result:** Client A and B both succeed, Client C must retry after A finishes.

### Example 4: Multiple Word Updates
```
WRITE test.txt 0
0 The           # Insert "The" at position 0
4 very          # Insert "very" at position 4
7 today.        # Insert "today." at position 7
ETIRW
```

Each update modifies the sentence cumulatively. The "today." will create a new sentence due to the period delimiter.

## Error Handling

### ERROR: File not found
```
ERROR: Cannot read file
```
- File doesn't exist or can't be read
- Client should CREATE file first

### ERROR: Sentence index out of range
```
ERROR: Sentence index out of range
```
- sentence_index < 0 or > sentence_count
- Valid range: 0 to sentence_count (inclusive for new sentence)

### ERROR: Sentence locked
```
ERROR: Sentence X is locked by <username>
```
- Another client is currently editing this sentence
- Client should retry after a delay

### ERROR: Word index out of range
```
ERROR: Word index X out of range (0-Y)
```
- word_index < 0 or > word_count
- Client should check current word count

### ERROR: Connection lost
- If client disconnects before ETIRW
- Server automatically releases lock
- Partial changes are discarded

## Concurrency Guarantees

### What is Protected:
✅ Sentence-level isolation: Different sentences can be edited simultaneously  
✅ Word-level atomicity: All word updates are applied together after ETIRW  
✅ File consistency: Atomic rename prevents corruption  
✅ Lock safety: Automatic cleanup on disconnect  

### What is NOT Protected:
❌ Sentence index changes: If Client A adds sentences, Client B's indices may shift  
❌ File-level operations: Other operations (DELETE, MOVE) are not locked  

### Recommendation:
For best results, clients should:
1. READ file to get current sentence structure
2. WRITE to specific sentences
3. Re-READ if needed to see updated structure

## Testing the Implementation

### Test 1: Basic Word Insertion
```bash
# Terminal 1: Start servers
./name_server
./storage_server 9000 9001

# Terminal 2: Client
./client
> CREATE test.txt
> WRITE test.txt 0
0 Hello world
ETIRW
> READ test.txt
```

### Test 2: Concurrent Writes
```bash
# Terminal 1: Name Server
./name_server

# Terminal 2: Storage Server
./storage_server 9000 9001

# Terminal 3: Client A
./client
> CREATE test.txt
> WRITE test.txt 0
0 First sentence.
ETIRW
> WRITE test.txt 0
0 Updated
# Don't send ETIRW yet - keep lock

# Terminal 4: Client B (while A holds lock on sentence 0)
./client
> WRITE test.txt 0
# Should see: ERROR: Sentence 0 is locked by userA

> WRITE test.txt 1
0 Second sentence here
ETIRW
# Should succeed - different sentence

# Back to Terminal 3 (Client A)
ETIRW
# Release lock
```

### Test 3: Delimiter Splitting
```bash
./client
> CREATE test.txt
> WRITE test.txt 0
0 Hello world. How are you? I am fine!
ETIRW
> READ test.txt
# Should show 3 sentences created from delimiters
```

## Performance Characteristics

- **Lock acquisition:** O(1) - hash table lookup
- **Sentence parsing:** O(n) - linear scan for delimiters
- **Word insertion:** O(m) - m = number of words in sentence
- **File write:** O(n) - write entire file content
- **Atomic rename:** O(1) - filesystem operation

## Code Location

Implementation: `storage_server.c`, function `handle_write()` (lines ~406-670)

Key helper functions:
- `get_sentence_lock()` - Acquire/create sentence lock
- `parse_sentences()` - Split content by delimiters
- `reconstruct_file()` - Rebuild file from sentences
- `save_for_undo()` - Backup before modification

## Future Improvements

1. **Optimize for large files:** Stream processing instead of loading entire file
2. **Better index tracking:** Return updated sentence indices after ETIRW
3. **Word-level delimiters:** Handle commas, semicolons as word boundaries
4. **Compression:** Store undo files compressed to save space
5. **Lock timeout:** Automatically release locks after inactivity
