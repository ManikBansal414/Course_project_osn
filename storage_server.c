#include "common.h"
#include <stdarg.h>
#include <dirent.h>

#define STORAGE_DIR "./storage"
#define UNDO_DIR "./undo"

typedef struct {
    char filename[MAX_FILENAME];
    int sentence_index;
    pthread_mutex_t lock;
    char locked_by[MAX_USERNAME];
} SentenceLock;

// Global variables
char nm_ip[INET_ADDRSTRLEN] = "127.0.0.1";
int nm_port = 8080;
int client_port = 9000;
int nm_listen_port = 9001;
SentenceLock sentence_locks[MAX_FILES];
int num_locks = 0;
pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE* log_file = NULL;

// Bonus: Fault Tolerance - Persistent NM connection for heartbeat
int nm_heartbeat_sock = -1;
pthread_mutex_t nm_sock_mutex = PTHREAD_MUTEX_INITIALIZER;
int should_exit = 0; // Flag to stop threads on shutdown

// Function prototypes
void register_with_nm();
void* handle_nm_request(void* arg);
void* handle_client_request(void* arg);
void* heartbeat_thread(void* arg); // Bonus: Send periodic heartbeats
void* nm_listener(void* arg); // Existing NM listener thread
int read_file_content(const char* filename, char* buffer, size_t buffer_size);
int write_file_content(const char* filename, const char* content);
int parse_sentences(const char* content, char sentences[][MAX_SENTENCE_LENGTH], int* count);
int reconstruct_file(char sentences[][MAX_SENTENCE_LENGTH], int count, char* output);
void create_file(const char* filename, const char* owner);
void delete_file(const char* filename);
void save_for_undo(const char* filename);
void log_to_file(const char* format, ...);

// Logging
void log_to_file(const char* format, ...) {
    if (!log_file) return;
    
    time_t now;
    time(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(log_file, "[%s] ", time_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
}

// Parse content into sentences
int parse_sentences(const char* content, char sentences[][MAX_SENTENCE_LENGTH], int* count) {
    *count = 0;
    int len = strlen(content);
    int start = 0;
    
    for (int i = 0; i < len && *count < 1000; i++) {
        if (content[i] == '.' || content[i] == '!' || content[i] == '?') {
            int sentence_len = i - start + 1;
            if (sentence_len > 0 && sentence_len < MAX_SENTENCE_LENGTH) {
                strncpy(sentences[*count], content + start, sentence_len);
                sentences[*count][sentence_len] = '\0';
                (*count)++;
            }
            start = i + 1;
            
            // Skip whitespace after delimiter
            while (start < len && content[start] == ' ') {
                start++;
            }
        }
    }
    
    // Handle last sentence without delimiter
    if (start < len) {
        int sentence_len = len - start;
        if (sentence_len > 0 && sentence_len < MAX_SENTENCE_LENGTH) {
            strncpy(sentences[*count], content + start, sentence_len);
            sentences[*count][sentence_len] = '\0';
            (*count)++;
        }
    }
    
    return *count;
}

// Reconstruct file from sentences
int reconstruct_file(char sentences[][MAX_SENTENCE_LENGTH], int count, char* output) {
    int offset = 0;
    for (int i = 0; i < count; i++) {
        int len = strlen(sentences[i]);
        strcpy(output + offset, sentences[i]);
        offset += len;
        
        // Add space between sentences if needed
        if (i < count - 1 && offset > 0 && output[offset - 1] != ' ') {
            output[offset++] = ' ';
        }
    }
    output[offset] = '\0';
    return offset;
}

// Construct full file path including folder
void construct_file_path(char* filepath, size_t size, const char* folder_path, const char* filename) {
    if (folder_path != NULL && strlen(folder_path) > 0 && strcmp(folder_path, "/") != 0) {
        snprintf(filepath, size, "%s/%s/%s", STORAGE_DIR, folder_path, filename);
    } else {
        snprintf(filepath, size, "%s/%s", STORAGE_DIR, filename);
    }
}

// Read file content
int read_file_content(const char* filename, char* buffer, size_t buffer_size) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        return -1;
    }
    
    size_t n = fread(buffer, 1, buffer_size - 1, fp);
    buffer[n] = '\0';
    fclose(fp);
    
    return n;
}

// Read file content with folder support
int read_file_content_with_folder(const char* folder_path, const char* filename, char* buffer, size_t buffer_size) {
    char filepath[512];
    construct_file_path(filepath, sizeof(filepath), folder_path, filename);
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        return -1;
    }
    
    size_t n = fread(buffer, 1, buffer_size - 1, fp);
    buffer[n] = '\0';
    fclose(fp);
    
    return n;
}

// Write file content
int write_file_content(const char* filename, const char* content) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
    
    FILE* fp = fopen(filepath, "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "%s", content);
    fclose(fp);
    
    return 0;
}

// Write file content with folder support
int write_file_content_with_folder(const char* folder_path, const char* filename, const char* content) {
    char filepath[512];
    construct_file_path(filepath, sizeof(filepath), folder_path, filename);
    
    FILE* fp = fopen(filepath, "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "%s", content);
    fclose(fp);
    
    return 0;
}

// Helper function to create nested folders recursively
int create_folder_recursive(const char* path) {
    char tmp[512];
    char* p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

// Create file
void create_file(const char* filename, const char* owner) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
    
    // Ensure parent folder exists (if file is in a folder)
    char* last_slash = strrchr(filepath, '/');
    if (last_slash != NULL && last_slash != filepath) {
        char parent_dir[512];
        strncpy(parent_dir, filepath, last_slash - filepath);
        parent_dir[last_slash - filepath] = '\0';
        create_folder_recursive(parent_dir);
    }
    
    FILE* fp = fopen(filepath, "w");
    if (fp) {
        fclose(fp);
        log_message("SS", "Created file: %s (owner: %s)", filename, owner);
        log_to_file("CREATE: %s by %s", filename, owner);
    }
}

// Delete file
void delete_file(const char* filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
    
    if (unlink(filepath) == 0) {
        log_message("SS", "Deleted file: %s", filename);
        log_to_file("DELETE: %s", filename);
        
        // Also delete undo file
        snprintf(filepath, sizeof(filepath), "%s/%s", UNDO_DIR, filename);
        unlink(filepath);
    }
}

// Save for undo
// Save for undo - with folder support
void save_for_undo_with_folder(const char* folder_path, const char* filename) {
    char src_path[512], dst_path[512];
    construct_file_path(src_path, sizeof(src_path), folder_path, filename);
    construct_file_path(dst_path, sizeof(dst_path), folder_path, filename);
    
    // Replace STORAGE_DIR with UNDO_DIR in dst_path
    snprintf(dst_path, sizeof(dst_path), "%s", src_path);
    char* storage_pos = strstr(dst_path, STORAGE_DIR);
    if (storage_pos == dst_path) {
        memmove(dst_path, dst_path + strlen(STORAGE_DIR), strlen(dst_path) - strlen(STORAGE_DIR) + 1);
        char temp[512];
        snprintf(temp, sizeof(temp), "%s%s", UNDO_DIR, dst_path);
        strcpy(dst_path, temp);
    }
    
    FILE* src = fopen(src_path, "r");
    if (!src) return;
    
    // Ensure parent directory exists for undo file
    char* last_slash = strrchr(dst_path, '/');
    if (last_slash) {
        char parent_dir[512];
        strncpy(parent_dir, dst_path, last_slash - dst_path);
        parent_dir[last_slash - dst_path] = '\0';
        create_folder_recursive(parent_dir);
    }
    
    FILE* dst = fopen(dst_path, "w");
    if (!dst) {
        fclose(src);
        return;
    }
    
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, n, dst);
    }
    
    fclose(src);
    fclose(dst);
}

void save_for_undo(const char* filename) {
    save_for_undo_with_folder("", filename);
}

// Get sentence lock
SentenceLock* get_sentence_lock(const char* filename, int sentence_index) {
    pthread_mutex_lock(&locks_mutex);
    
    for (int i = 0; i < num_locks; i++) {
        if (strcmp(sentence_locks[i].filename, filename) == 0 &&
            sentence_locks[i].sentence_index == sentence_index) {
            pthread_mutex_unlock(&locks_mutex);
            return &sentence_locks[i];
        }
    }
    
    // Create new lock
    if (num_locks < MAX_FILES) {
        strcpy(sentence_locks[num_locks].filename, filename);
        sentence_locks[num_locks].sentence_index = sentence_index;
        pthread_mutex_init(&sentence_locks[num_locks].lock, NULL);
        sentence_locks[num_locks].locked_by[0] = '\0';
        num_locks++;
        
        pthread_mutex_unlock(&locks_mutex);
        return &sentence_locks[num_locks - 1];
    }
    
    pthread_mutex_unlock(&locks_mutex);
    return NULL;
}

// Handle READ request
void handle_read(int sock, Message* msg) {
    char buffer[MAX_BUFFER_SIZE];
    // filename now includes path (e.g., "documents/test.txt")
    int n = read_file_content(msg->filename, buffer, sizeof(buffer));
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (n < 0) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: Cannot read file");
    } else {
        response.error_code = ERR_SUCCESS;
        strcpy(response.data, buffer);
        response.data_len = n;
    }
    
    send_message(sock, &response);
    log_to_file("READ: %s", msg->filename);
}

// Handle WRITE request
void handle_write(int sock, Message* msg) {
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    // Save for undo - filename now includes path
    save_for_undo(msg->filename);
    
    // Read current content
    char buffer[MAX_BUFFER_SIZE];
    int n = read_file_content(msg->filename, buffer, sizeof(buffer));
    
    if (n < 0) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: Cannot read file");
        send_message(sock, &response);
        return;
    }
    
    // Parse sentences
    char sentences[1000][MAX_SENTENCE_LENGTH];
    int sentence_count;
    parse_sentences(buffer, sentences, &sentence_count);
    
    int sentence_index = msg->flags;
    
    // Validate sentence index
    if (sentence_index < 0 || sentence_index > sentence_count) {
        response.error_code = ERR_INVALID_INDEX;
        strcpy(response.data, "ERROR: Sentence index out of range");
        send_message(sock, &response);
        return;
    }
    
    // Get lock
    SentenceLock* lock = get_sentence_lock(msg->filename, sentence_index);
    if (!lock) {
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Cannot acquire lock");
        send_message(sock, &response);
        return;
    }
    
    // Try to lock
    if (pthread_mutex_trylock(&lock->lock) != 0) {
        response.error_code = ERR_SENTENCE_LOCKED;
        sprintf(response.data, "ERROR: Sentence locked by %s", lock->locked_by);
        send_message(sock, &response);
        return;
    }
    
    strcpy(lock->locked_by, msg->username);
    
    // Send ACK that lock is acquired
    response.error_code = ERR_SUCCESS;
    strcpy(response.data, "Lock acquired. Send word updates.");
    send_message(sock, &response);
    
    // Receive word updates until ETIRW
    while (1) {
        Message update_msg;
        if (receive_message(sock, &update_msg) < 0) {
            pthread_mutex_unlock(&lock->lock);
            lock->locked_by[0] = '\0';
            return;
        }
        
        // Check for ETIRW (end of write)
        if (strcmp(update_msg.data, "ETIRW") == 0) {
            break;
        }
        
        // Parse word index and content
        int word_index = update_msg.word_index;
        
        // Get or create sentence
        char* target_sentence;
        if (sentence_index >= sentence_count) {
            // Append new sentence
            target_sentence = sentences[sentence_count];
            target_sentence[0] = '\0';
            sentence_count++;
        } else {
            target_sentence = sentences[sentence_index];
        }
        
        // Parse words in sentence using thread-safe strtok_r
        char words[1000][MAX_WORD_LENGTH];
        int word_count = 0;
        
        char* saveptr1;
        char* token = strtok_r(target_sentence, " ", &saveptr1);
        while (token && word_count < 1000) {
            strcpy(words[word_count++], token);
            token = strtok_r(NULL, " ", &saveptr1);
        }
        
        // Validate word index
        if (word_index < 0 || word_index > word_count + 1) {
            Message error_response;
            memset(&error_response, 0, sizeof(error_response));
            error_response.type = MSG_ERROR;
            error_response.error_code = ERR_INVALID_INDEX;
            strcpy(error_response.data, "ERROR: Word index out of range");
            send_message(sock, &error_response);
            continue;
        }
        
        // Insert content at word index
        char new_content[MAX_SENTENCE_LENGTH * 10];
        char* content_tokens[1000];
        int content_count = 0;
        
        // Parse content into tokens using thread-safe strtok_r
        char content_copy[MAX_BUFFER_SIZE];
        strcpy(content_copy, update_msg.data);
        char* saveptr2;
        char* ct = strtok_r(content_copy, " ", &saveptr2);
        while (ct && content_count < 1000) {
            content_tokens[content_count++] = strdup(ct);
            ct = strtok_r(NULL, " ", &saveptr2);
        }
        
        // Rebuild sentence with inserted content
        int new_word_count = 0;
        char new_words[2000][MAX_WORD_LENGTH];
        
        for (int i = 0; i < word_index && i < word_count; i++) {
            strcpy(new_words[new_word_count++], words[i]);
        }
        
        for (int i = 0; i < content_count; i++) {
            strcpy(new_words[new_word_count++], content_tokens[i]);
            free(content_tokens[i]);
        }
        
        for (int i = word_index; i < word_count; i++) {
            strcpy(new_words[new_word_count++], words[i]);
        }
        
        // Reconstruct sentence
        target_sentence[0] = '\0';
        for (int i = 0; i < new_word_count; i++) {
            strcat(target_sentence, new_words[i]);
            if (i < new_word_count - 1) {
                strcat(target_sentence, " ");
            }
        }
        
        // Check for sentence delimiters and split if necessary
        char temp_sentences[100][MAX_SENTENCE_LENGTH];
        int temp_count = 0;
        parse_sentences(target_sentence, temp_sentences, &temp_count);
        
        if (temp_count > 1) {
            // Replace current sentence with first split
            strcpy(sentences[sentence_index], temp_sentences[0]);
            
            // Insert remaining splits
            for (int i = sentence_count - 1; i > sentence_index; i--) {
                strcpy(sentences[i + temp_count - 1], sentences[i]);
            }
            
            for (int i = 1; i < temp_count; i++) {
                strcpy(sentences[sentence_index + i], temp_sentences[i]);
            }
            
            sentence_count += temp_count - 1;
        } else {
            strcpy(sentences[sentence_index], target_sentence);
        }
        
        // Send ACK
        Message ack;
        memset(&ack, 0, sizeof(ack));
        ack.type = MSG_ACK;
        ack.error_code = ERR_SUCCESS;
        send_message(sock, &ack);
    }
    
    // Reconstruct and save file
    char final_content[MAX_BUFFER_SIZE];
    reconstruct_file(sentences, sentence_count, final_content);
    write_file_content(msg->filename, final_content); // filename includes path
    
    // Release lock
    pthread_mutex_unlock(&lock->lock);
    lock->locked_by[0] = '\0';
    
    // Send final response
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    response.error_code = ERR_SUCCESS;
    strcpy(response.data, "Write Successful!");
    send_message(sock, &response);
    
    log_to_file("WRITE: %s by %s, sentence %d", msg->filename, msg->username, sentence_index);
}

// Handle STREAM request
void handle_stream(int sock, Message* msg) {
    // Allocate thread-local buffer to avoid race conditions
    char* buffer = (char*)malloc(MAX_BUFFER_SIZE);
    if (!buffer) {
        Message response;
        memset(&response, 0, sizeof(response));
        response.type = MSG_RESPONSE;
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Memory allocation failed");
        send_message(sock, &response);
        return;
    }
    
    // filename now includes path
    int n = read_file_content(msg->filename, buffer, MAX_BUFFER_SIZE);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (n < 0) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: Cannot read file");
        send_message(sock, &response);
        free(buffer);
        return;
    }
    
    // Send words one by one using thread-safe strtok_r
    char* saveptr;
    char* token = strtok_r(buffer, " \n\t", &saveptr);
    while (token) {
        memset(&response, 0, sizeof(response));
        response.type = MSG_RESPONSE;
        response.error_code = ERR_SUCCESS;
        
        // Copy token safely
        strncpy(response.data, token, MAX_BUFFER_SIZE - 1);
        response.data[MAX_BUFFER_SIZE - 1] = '\0';
        
        send_message(sock, &response);
        usleep(100000); // 0.1 second delay
        
        token = strtok_r(NULL, " \n\t", &saveptr);
    }
    
    // Send STOP
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    response.error_code = ERR_SUCCESS;
    strcpy(response.data, "STOP");
    send_message(sock, &response);
    
    free(buffer);
    log_to_file("STREAM: %s by %s", msg->filename, msg->username);
}

// Handle UNDO request
void handle_undo(int sock, Message* msg) {
    char src_path[512], dst_path[512];
    // filename now includes path (e.g., "documents/test.txt")
    snprintf(src_path, sizeof(src_path), "%s/%s", UNDO_DIR, msg->filename);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", STORAGE_DIR, msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    FILE* src = fopen(src_path, "r");
    if (!src) {
        response.error_code = ERR_NO_UNDO_AVAILABLE;
        strcpy(response.data, "ERROR: No undo available");
        send_message(sock, &response);
        return;
    }
    
    FILE* dst = fopen(dst_path, "w");
    if (!dst) {
        fclose(src);
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Cannot write file");
        send_message(sock, &response);
        return;
    }
    
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, n, dst);
    }
    
    fclose(src);
    fclose(dst);
    
    response.error_code = ERR_SUCCESS;
    strcpy(response.data, "Undo Successful!");
    send_message(sock, &response);
    
    log_to_file("UNDO: %s", msg->filename);
}

// Handle client request
void* handle_client_request(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    if (receive_message(client_sock, &msg) == 0) {
        log_message("SS", "Client request: type=%d, file=%s", msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_READ_FILE:
                handle_read(client_sock, &msg);
                break;
                
            case MSG_WRITE_FILE:
                handle_write(client_sock, &msg);
                break;
                
            case MSG_STREAM_FILE:
                handle_stream(client_sock, &msg);
                break;
                
            case MSG_UNDO_FILE:
                handle_undo(client_sock, &msg);
                break;
                
            default:
                log_message("SS", "Unknown client request: %d", msg.type);
        }
    }
    
    close(client_sock);
    return NULL;
}

// Handle NM request
void* handle_nm_request(void* arg) {
    int nm_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    while (receive_message(nm_sock, &msg) == 0) {
        log_message("SS", "NM request: type=%d, file=%s", msg.type, msg.filename);
        
        Message response;
        memset(&response, 0, sizeof(response));
        response.type = MSG_ACK;
        
        switch (msg.type) {
            case MSG_SS_CREATE:
                create_file(msg.filename, msg.username);
                response.error_code = ERR_SUCCESS;
                strcpy(response.data, "File created");
                break;
                
            case MSG_SS_DELETE:
                delete_file(msg.filename);
                response.error_code = ERR_SUCCESS;
                strcpy(response.data, "File deleted");
                break;
                
            case MSG_SS_READ: {
                char buffer[MAX_BUFFER_SIZE];
                int n = read_file_content(msg.filename, buffer, sizeof(buffer));
                if (n < 0) {
                    response.error_code = ERR_FILE_NOT_FOUND;
                    strcpy(response.data, "ERROR: Cannot read file");
                } else {
                    response.error_code = ERR_SUCCESS;
                    strcpy(response.data, buffer);
                }
                break;
            }
            
            case MSG_SS_STAT: {
                char buffer[MAX_BUFFER_SIZE];
                int n = read_file_content(msg.filename, buffer, sizeof(buffer));
                if (n < 0) {
                    response.error_code = ERR_FILE_NOT_FOUND;
                    strcpy(response.data, "0 0");
                } else {
                    // Count words and characters
                    int word_count = 0;
                    int char_count = n;
                    int in_word = 0;
                    
                    for (int i = 0; i < n; i++) {
                        if (buffer[i] == ' ' || buffer[i] == '\n' || buffer[i] == '\t') {
                            in_word = 0;
                        } else {
                            if (!in_word) {
                                word_count++;
                                in_word = 1;
                            }
                        }
                    }
                    
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "%d %d", word_count, char_count);
                }
                break;
            }
            
            case MSG_SS_CREATE_FOLDER: {
                // Create physical folder in storage directory
                char folder_path[512];
                snprintf(folder_path, sizeof(folder_path), "%s/%s", STORAGE_DIR, msg.folder_path);
                
                if (create_folder_recursive(folder_path) == 0) {
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "✓ Folder created: %s", msg.folder_path);
                    log_message("SS", "Created folder: %s", folder_path);
                } else {
                    response.error_code = ERR_INVALID_COMMAND;
                    sprintf(response.data, "ERROR: Cannot create folder: %s", strerror(errno));
                    log_message("SS", "Failed to create folder %s: %s", folder_path, strerror(errno));
                }
                break;
            }
            
            case MSG_SS_MOVE_FILE: {
                // Move file: msg.filename = old full path, msg.folder_path = new full path
                char old_path[512];
                char new_path[512];
                
                // Construct old path from old filename (which may include folder)
                snprintf(old_path, sizeof(old_path), "%s/%s", STORAGE_DIR, msg.filename);
                
                // Construct new path from new filename (which includes folder)
                snprintf(new_path, sizeof(new_path), "%s/%s", STORAGE_DIR, msg.folder_path);
                
                // Ensure parent directory exists for new path
                char* last_slash = strrchr(new_path, '/');
                if (last_slash) {
                    char parent_dir[512];
                    strncpy(parent_dir, new_path, last_slash - new_path);
                    parent_dir[last_slash - new_path] = '\0';
                    create_folder_recursive(parent_dir);
                }
                
                // Use rename() to move the file atomically
                if (rename(old_path, new_path) == 0) {
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "✓ File moved successfully");
                    log_message("SS", "Moved file: %s -> %s", old_path, new_path);
                    
                    // Also move undo file if it exists
                    char old_undo[512], new_undo[512];
                    snprintf(old_undo, sizeof(old_undo), "%s/%s", UNDO_DIR, msg.filename);
                    snprintf(new_undo, sizeof(new_undo), "%s/%s", UNDO_DIR, msg.folder_path);
                    
                    // Ensure parent directory exists for undo file
                    last_slash = strrchr(new_undo, '/');
                    if (last_slash) {
                        char undo_parent[512];
                        strncpy(undo_parent, new_undo, last_slash - new_undo);
                        undo_parent[last_slash - new_undo] = '\0';
                        create_folder_recursive(undo_parent);
                    }
                    
                    rename(old_undo, new_undo); // Ignore errors for undo file
                } else {
                    response.error_code = ERR_INVALID_COMMAND;
                    sprintf(response.data, "ERROR: Cannot move file: %s", strerror(errno));
                    log_message("SS", "Failed to move %s to %s: %s", old_path, new_path, strerror(errno));
                }
                break;
            }
            
            case MSG_SS_CHECKPOINT: {
                char checkpoint_dir[512];
                char checkpoint_path[512];
                char file_path[512];
                
                // Create checkpoints directory: checkpoints/<filename>/
                snprintf(checkpoint_dir, sizeof(checkpoint_dir), "checkpoints/%s", msg.filename);
                create_folder_recursive(checkpoint_dir);
                
                // Checkpoint file path: checkpoints/<filename>/<tag>
                snprintf(checkpoint_path, sizeof(checkpoint_path), "%s/%s", checkpoint_dir, msg.checkpoint_tag);
                snprintf(file_path, sizeof(file_path), "%s/%s", STORAGE_DIR, msg.filename);
                
                if (msg.flags == 0) {
                    // CREATE CHECKPOINT: Copy current file to checkpoint
                    FILE* src = fopen(file_path, "r");
                    if (!src) {
                        response.error_code = ERR_FILE_NOT_FOUND;
                        strcpy(response.data, "ERROR: File not found");
                        break;
                    }
                    
                    FILE* dst = fopen(checkpoint_path, "w");
                    if (!dst) {
                        fclose(src);
                        response.error_code = ERR_SERVER_ERROR;
                        strcpy(response.data, "ERROR: Cannot create checkpoint");
                        break;
                    }
                    
                    char buffer[4096];
                    size_t n;
                    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                        fwrite(buffer, 1, n, dst);
                    }
                    
                    fclose(src);
                    fclose(dst);
                    
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "✓ Checkpoint '%s' created for file '%s'", 
                            msg.checkpoint_tag, msg.filename);
                    log_message("SS", "Checkpoint created: %s for %s", msg.checkpoint_tag, msg.filename);
                    
                } else if (msg.flags == 1) {
                    // VIEW CHECKPOINT: Read and return checkpoint content
                    FILE* f = fopen(checkpoint_path, "r");
                    if (!f) {
                        response.error_code = ERR_FILE_NOT_FOUND;
                        sprintf(response.data, "ERROR: Checkpoint '%s' not found", msg.checkpoint_tag);
                        break;
                    }
                    
                    size_t n = fread(response.data, 1, sizeof(response.data) - 1, f);
                    response.data[n] = '\0';
                    fclose(f);
                    
                    response.error_code = ERR_SUCCESS;
                    response.data_len = n;
                    log_message("SS", "Checkpoint viewed: %s for %s", msg.checkpoint_tag, msg.filename);
                    
                } else if (msg.flags == 2) {
                    // REVERT CHECKPOINT: Copy checkpoint back to original file
                    FILE* src = fopen(checkpoint_path, "r");
                    if (!src) {
                        response.error_code = ERR_FILE_NOT_FOUND;
                        sprintf(response.data, "ERROR: Checkpoint '%s' not found", msg.checkpoint_tag);
                        break;
                    }
                    
                    // Save current version to undo first
                    save_for_undo(msg.filename);
                    
                    FILE* dst = fopen(file_path, "w");
                    if (!dst) {
                        fclose(src);
                        response.error_code = ERR_SERVER_ERROR;
                        strcpy(response.data, "ERROR: Cannot revert file");
                        break;
                    }
                    
                    char buffer[4096];
                    size_t n;
                    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                        fwrite(buffer, 1, n, dst);
                    }
                    
                    fclose(src);
                    fclose(dst);
                    
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "✓ File '%s' reverted to checkpoint '%s'", 
                            msg.filename, msg.checkpoint_tag);
                    log_message("SS", "File reverted: %s to checkpoint %s", msg.filename, msg.checkpoint_tag);
                    
                } else if (msg.flags == 3) {
                    // LIST CHECKPOINTS: List all checkpoint tags for file
                    DIR* dir = opendir(checkpoint_dir);
                    if (!dir) {
                        response.error_code = ERR_SUCCESS;
                        sprintf(response.data, "─── Checkpoints for '%s' ───\n(no checkpoints)\n", msg.filename);
                        break;
                    }
                    
                    char buffer[MAX_BUFFER_SIZE];
                    int offset = 0;
                    offset += sprintf(buffer + offset, "─── Checkpoints for '%s' ───\n", msg.filename);
                    
                    struct dirent* entry;
                    int count = 0;
                    while ((entry = readdir(dir)) != NULL && offset < MAX_BUFFER_SIZE - 100) {
                        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                            continue;
                        }
                        
                        offset += sprintf(buffer + offset, "  • %s\n", entry->d_name);
                        count++;
                    }
                    
                    if (count == 0) {
                        offset += sprintf(buffer + offset, "(no checkpoints)\n");
                    }
                    
                    closedir(dir);
                    strcpy(response.data, buffer);
                    response.error_code = ERR_SUCCESS;
                    log_message("SS", "Checkpoints listed for %s: %d found", msg.filename, count);
                }
                break;
            }
                
            default:
                log_message("SS", "Unknown NM request: %d", msg.type);
                response.error_code = ERR_INVALID_COMMAND;
        }
        
        send_message(nm_sock, &response);
    }
    
    close(nm_sock);
    return NULL;
}

// Register with Name Server
void register_with_nm() {
    log_message("SS", "Registering with Name Server at %s:%d", nm_ip, nm_port);
    
    int sock = connect_to_server(nm_ip, nm_port);
    if (sock < 0) {
        log_message("SS", "Failed to connect to Name Server");
        exit(1);
    }
    
    // Get list of files
    DIR* dir = opendir(STORAGE_DIR);
    if (!dir) {
        mkdir(STORAGE_DIR, 0755);
        dir = opendir(STORAGE_DIR);
    }
    
    char file_list[MAX_BUFFER_SIZE] = {0};
    int offset = 0;
    
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                offset += snprintf(file_list + offset, sizeof(file_list) - offset, 
                    "%s\n", entry->d_name);
            }
        }
        closedir(dir);
    }
    
    // Send registration message
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_REGISTER_SS;
    strcpy(msg.ss_ip, "127.0.0.1");
    msg.ss_port = nm_listen_port;
    msg.flags = client_port; // Store client port in flags
    strcpy(msg.data, file_list);
    msg.data_len = strlen(file_list);
    
    send_message(sock, &msg);
    
    Message response;
    if (receive_message(sock, &response) == 0 && response.error_code == ERR_SUCCESS) {
        log_message("SS", "Successfully registered with Name Server");
        log_message("SS", "%s", response.data);
    } else {
        log_message("SS", "Failed to register with Name Server");
    }
    
    close(sock);
}

// Bonus: Heartbeat thread - sends periodic heartbeats to Name Server
void* heartbeat_thread(void* arg) {
    (void)arg; // Unused
    
    log_message("SS", "Heartbeat thread started");
    
    while (!should_exit) {
        sleep(10); // Send heartbeat every 10 seconds
        
        if (should_exit) break;
        
        // Connect to NM for heartbeat
        pthread_mutex_lock(&nm_sock_mutex);
        int sock = connect_to_server(nm_ip, nm_port);
        pthread_mutex_unlock(&nm_sock_mutex);
        
        if (sock < 0) {
            log_message("SS", "Failed to send heartbeat - cannot connect to NM");
            continue;
        }
        
        Message msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = MSG_HEARTBEAT;
        strcpy(msg.ss_ip, "127.0.0.1");
        msg.ss_port = nm_listen_port;
        
        send_message(sock, &msg);
        log_message("SS", "Heartbeat sent to Name Server");
        
        close(sock);
    }
    
    log_message("SS", "Heartbeat thread stopped");
    return NULL;
}

// Listener for NM requests (runs in separate thread)
void* nm_listener(void* arg) {
    int nm_listen_port = *(int*)arg;
    
    int nm_listen_sock = create_socket(nm_listen_port);
    if (nm_listen_sock < 0) {
        log_message("SS", "Failed to create NM listener socket");
        return NULL;
    }
    
    log_message("SS", "Listening for NM requests on port %d", nm_listen_port);
    
    // Accept NM connections
    while (1) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        int* nm_sock = (int*)malloc(sizeof(int));
        *nm_sock = accept(nm_listen_sock, (struct sockaddr*)&addr, &addr_len);
        
        if (*nm_sock >= 0) {
            pthread_t thread;
            pthread_create(&thread, NULL, handle_nm_request, nm_sock);
            pthread_detach(thread);
        } else {
            free(nm_sock);
        }
    }
    
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        client_port = atoi(argv[1]);
    }
    if (argc >= 3) {
        nm_listen_port = atoi(argv[2]);
    }
    
    log_message("SS", "Starting Storage Server");
    log_message("SS", "Client port: %d, NM listen port: %d", client_port, nm_listen_port);
    
    // Create directories
    mkdir(STORAGE_DIR, 0755);
    mkdir(UNDO_DIR, 0755);
    
    // Open log file
    log_file = fopen("ss_log.txt", "a");
    if (!log_file) {
        log_message("SS", "Warning: Cannot open log file");
    }
    
    // Start NM listener thread first
    pthread_t nm_listener_thread;
    int* port_arg = malloc(sizeof(int));
    *port_arg = nm_listen_port;
    pthread_create(&nm_listener_thread, NULL, nm_listener, port_arg);
    pthread_detach(nm_listener_thread);
    
    // Wait for NM listener to start
    sleep(1);
    
    // Now register with Name Server (synchronously)
    log_message("SS", "Registering with Name Server...");
    register_with_nm();
    
    log_message("SS", "Registration complete!");
    
    // Bonus: Start heartbeat thread for fault tolerance
    pthread_t heartbeat_tid;
    if (pthread_create(&heartbeat_tid, NULL, heartbeat_thread, NULL) == 0) {
        pthread_detach(heartbeat_tid);
        log_message("SS", "Heartbeat thread started for fault tolerance");
    } else {
        log_message("SS", "Warning: Failed to start heartbeat thread");
    }
    
    // Create client listener socket
    int client_sock = create_socket(client_port);
    if (client_sock < 0) {
        log_message("SS", "Failed to create client socket");
        return 1;
    }
    
    log_message("SS", "Storage Server started, listening for clients on port %d", client_port);
    
    // Accept client connections
    while (1) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        int* conn_sock = (int*)malloc(sizeof(int));
        *conn_sock = accept(client_sock, (struct sockaddr*)&addr, &addr_len);
        
        if (*conn_sock < 0) {
            free(conn_sock);
            continue;
        }
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client_request, conn_sock);
        pthread_detach(thread);
    }
    
    if (log_file) {
        fclose(log_file);
    }
    
    close(client_sock);
    return 0;
}
