#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>

#define MAX 256

// Color codes for terminal output
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

const int READ_END = 0;
const int WRITE_END = 1;

typedef enum {
    NEW_MINER = 1,
    NEW_BLOCK = 2
} TLV_TYPE;

typedef struct TLV {
    int type;
    int length;
    char value[1024];
} TLV;

// Block structure
typedef struct {
    int height;
    int timestamp;
    unsigned int hash;
    unsigned int prev_hash;
    int difficulty;
    int nonce;
    int relayed_by;
} Block_t;

int first_block = 0;
int fd_Miner;

// Function prototypes
bool verify_difficulty(unsigned int hash, int diff);
unsigned int calc_hash(Block_t* block);
int get_next_miner_id();
void signal_handler(int signum);

void print_block(Block_t* block);

TLV* readTlvFromPipe(int pipeReadEnd);
void writeTlvToPipe(int pipeWriteEnd, TLV* tlv);

// Log file pointer
FILE *log_file;

// Log function
void log_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    fprintf(log_file, "\n");
    fflush(log_file);
    va_end(args);
}

int main(int argc, char* argv[]) {
    srand(time(NULL));

     // Open log file
    log_file = fopen("/var/log/mtacoin.log", "a");
    if (!log_file) {
        log_message("Error opening log file");
        exit(EXIT_FAILURE);
    }

    int miner_id = get_next_miner_id();
    char path[256];

    snprintf(path, sizeof(path), "/mnt/mta/miner_%d", miner_id);

    // Create the directory if it doesn't exist (in case it's not created yet)
    if (mkdir("/mnt/mta", 0666) == -1 && errno != EEXIST) {
        log_message("Error creating directory /mnt/mta");
    }

    // Create the named pipe (FIFO) with read/write permissions
    if (mkfifo(path, 0666) == -1) {
        if (errno != EEXIST) {
            log_message("Error creating named pipe");
            exit(EXIT_FAILURE);
        }
    }

    char miner_id_str[32];
    snprintf(miner_id_str, sizeof(miner_id_str), "%d", miner_id);
    TLV tlv = { NEW_MINER, strlen(miner_id_str) + 1, {0} }; 
    strncpy(tlv.value, miner_id_str, sizeof(tlv.value) - 1);
    
    int pipe_fd_Server = open("/mnt/mta/server_pipe", O_WRONLY);
    if (pipe_fd_Server == -1) {
        log_message("Miner: Error opening server pipe");
        exit(EXIT_FAILURE);
    }
    writeTlvToPipe(pipe_fd_Server, &tlv);
    close(pipe_fd_Server);

    log_message("Miner %d sent connection request on %s\n", miner_id, path);

    fd_Miner = open(path, O_RDONLY | O_NONBLOCK);
    if (fd_Miner == -1) {
        log_message("Error opening named pipe");
        return 1;
    }

    int flags;
    flags = fcntl(fd_Miner, F_GETFL); /* Fetch open files status flags */
    flags |= O_NONBLOCK; /* Enable O_NONBLOCK bit */
    fcntl(fd_Miner, F_SETFL, flags); /* Update open files status flags */

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    Block_t* next_block = (Block_t*)malloc(sizeof(Block_t));

    while (true) 
    {
        TLV* new_tlv = readTlvFromPipe(fd_Miner);
        if (new_tlv != NULL && new_tlv->type == NEW_BLOCK) 
        {
            first_block = 1;
            memcpy(next_block, new_tlv->value, sizeof(Block_t));

            log_message(ANSI_COLOR_YELLOW "Miner #%d: " ANSI_COLOR_RESET "received a new block: relayed by (%d), height (%d), timestamp (%d), hash (0x%x), prev_hash(0x%x), difficulty (%d), nonce(%d)\n"
            ,miner_id, next_block->relayed_by, next_block->height, next_block->timestamp, next_block->hash, next_block->prev_hash, next_block->difficulty, next_block->nonce);
            
            next_block->relayed_by = miner_id;
            next_block->height += 1;
            next_block->nonce = 0;
            next_block->prev_hash = next_block->hash;
            next_block->timestamp = (int)time(NULL);
            
            free(new_tlv);
        } 

        next_block->timestamp = (int)time(NULL);
        unsigned int hash = calc_hash(next_block);

        if (verify_difficulty(hash, next_block->difficulty)) 
        {
            next_block->hash = hash;

            TLV new_block_tlv;
            new_block_tlv.type = NEW_BLOCK;
            new_block_tlv.length = sizeof(Block_t);
            memcpy(new_block_tlv.value, next_block, sizeof(Block_t));

            pipe_fd_Server = open("/mnt/mta/server_pipe", O_WRONLY);
            if (pipe_fd_Server == -1) 
            {
                log_message("Miner: Error opening server pipe");
                continue;
            }

            log_message(ANSI_COLOR_YELLOW "Miner #%d: " ANSI_COLOR_RESET "Mined a new block #%d, with the hash 0x%x\n", next_block->relayed_by, next_block->height, next_block->hash);

            writeTlvToPipe(pipe_fd_Server, &new_block_tlv);
            close(pipe_fd_Server);
        }

        next_block->nonce++;
    }

    free(next_block);
    return 0;
}

// Function to verify the difficulty of a hash
bool verify_difficulty(unsigned int hash, int diff) {
    int mask = -1 << (32 - diff);
    return !(mask & hash);
}

// Function to calculate the hash of a block
unsigned int calc_hash(Block_t* block) {
    char input[MAX];
    snprintf(input, sizeof(input), "%d%d%u%d%d", block->height, block->timestamp, block->prev_hash, block->nonce, block->relayed_by);
    return (unsigned int)crc32(0, (const Bytef*)input, strlen(input));
}

// Deserialize TLV from pipe
TLV* readTlvFromPipe(int pipeReadEnd) {
    TLV* tlv = (TLV*)malloc(sizeof(TLV));
    if (!tlv) {
        log_message("malloc failed");
        return NULL;
    }

    ssize_t len = read(pipeReadEnd, tlv, sizeof(tlv->type) + sizeof(tlv->length));
    if (len != sizeof(tlv->type) + sizeof(tlv->length)) {
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("Error reading TLV header");
        }
        free(tlv);
        return NULL;
    }

    len = read(pipeReadEnd, tlv->value, tlv->length);
    if (len != tlv->length) {
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("Error reading TLV value");
        }
        free(tlv);
        return NULL;
    }

    return tlv;
}

// Serialize TLV to pipe
void writeTlvToPipe(int pipeWriteEnd, TLV* tlv) {
    int len = 0;

    len = write(pipeWriteEnd, (void*)tlv, sizeof(tlv->type) + sizeof(tlv->length) + tlv->length);
    if (len < 0) { log_message("type + length + value"); return; }
}

void print_block(Block_t* block) {
    log_message(ANSI_COLOR_CYAN "Server: " ANSI_COLOR_RESET "New block added by %d, attributes: ", block->relayed_by);
    log_message("Height:(%d), ", block->height);
    log_message("Timestamp:(%d), ", block->timestamp);
    log_message("Hash:(0x%x), ", block->hash);
    log_message("Prev Hash:(0x%x), ", block->prev_hash);
    log_message("Difficulty:(%d), ", block->difficulty);
    log_message("Nonce:(%d)\n", block->nonce);
}

int get_next_miner_id() {
    int next_id = 1;
    DIR* dir = opendir("/mnt/mta");
    if (dir == NULL) {
        log_message("Error opening /mnt/mta directory");
        exit(EXIT_FAILURE);
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "miner_", 6) == 0) {
            int id = atoi(entry->d_name + 6);
            if (id >= next_id) {
                next_id = id + 1;
            }
        }
    }
    closedir(dir);
    return next_id;
}

// Signal handler
void signal_handler(int signum) {
    log_message("Received signal %d, cleaning up and exiting...\n", signum);
    if (fd_Miner != -1) {
        close(fd_Miner);
    }
    exit(signum);
}
