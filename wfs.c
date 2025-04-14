#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>

#include "wfs.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

off_t get_nodeSize(int inode_num); 
int writeFileInode(int inode_num, char* buffer, size_t size, off_t wanted_offset);
struct wfs_sb block_val(int fd);
int write_prev_inode_disk(int fd, struct wfs_inode inode); 
void return_code_bitmap(int fd, void* buffer);
int realFileInode(int inode_num, char* buffer, size_t size, off_t wanted_offset); 
int return_inodenumber_path(const char* path, int isNewFile);
off_t block_allocate(int fd);
int writeCurrInodeDisk(int fd, struct wfs_inode inode);
struct wfs_inode get_node(int fd, int inode_num);
int create_dir(const char* path, mode_t mode);


char disk[100]; 
char disks[20][100]; 
struct wfs_sb block_large;


struct wfs_sb block_val(int disk_fd) {
    struct wfs_sb block_large;
    if (lseek(disk_fd, 0, SEEK_SET) == -1) {
        exit(EXIT_FAILURE);
    }

    ssize_t bytesRead = read(disk_fd, &block_large, sizeof(block_large));
    if (bytesRead != sizeof(block_large)) {
        exit(EXIT_FAILURE);
    }
    return block_large;
}


void return_code_bitmap(int disk_fd, void* bitmap_buffer) {
    const size_t bitmap_size = (block_large.num_inodes + CHAR_BIT - 1) / CHAR_BIT;
    
    off_t seek_result = lseek(disk_fd, block_large.i_bitmap_ptr, SEEK_SET);
    if (seek_result == (off_t)-1) {
        exit(1);
    }

    size_t bytes_remaining = bitmap_size;
    unsigned char* current_pos = bitmap_buffer;
    
    while (bytes_remaining > 0) {
        ssize_t read_result = read(disk_fd, current_pos, bytes_remaining);
        if (read_result <= 0) {
            exit(1);
        }
        bytes_remaining -= read_result;
        current_pos += read_result;
    }
}

off_t block_allocate(int disk_handle) {
    const size_t total_disk_blocks = block_large.num_data_blocks;
    const size_t bitmap_size_bytes = (total_disk_blocks + CHAR_BIT - 1) / CHAR_BIT;
    
    unsigned char* allocation_map = calloc(1, bitmap_size_bytes);
    if (!allocation_map) {
        return -1;
    }

    if (lseek(disk_handle, block_large.d_bitmap_ptr, SEEK_SET) == -1) {
        free(allocation_map);
        return -1;
    }

    size_t map_size = (block_large.num_data_blocks + 7) / 8;
    ssize_t bytes_transferred = read(disk_handle, allocation_map, map_size);

    if (bytes_transferred != map_size) {
        free(allocation_map);
        return -1;
    }    
    size_t block_position;
    int found_vacant_block = 0;
    
    for (block_position = 0; block_position < total_disk_blocks; block_position++) {
        const size_t map_byte_offset = block_position / CHAR_BIT;
        const int map_bit_position = block_position % CHAR_BIT;
        
        if (!(allocation_map[map_byte_offset] & (1 << map_bit_position))) {
            allocation_map[map_byte_offset] |= (1 << map_bit_position);
            found_vacant_block = 1;
            break;
        }
    }

    if (!found_vacant_block) {
        free(allocation_map);
        return -1;
    }

    if (lseek(disk_handle, block_large.d_bitmap_ptr, SEEK_SET) == -1) {
        free(allocation_map);
        return -1;
    }

    if (write(disk_handle, allocation_map, bitmap_size_bytes) != bitmap_size_bytes) {
        free(allocation_map);
        return -1;
    }

    free(allocation_map);
    return block_large.d_blocks_ptr + (block_position * BLOCK_SIZE);
}

int return_inodenumber_path(const char* file_path, int create_new) {
    if (!file_path) {
        return -1;
    }

    char* path_copy = strdup(file_path);
    if (!path_copy) {
        return -1;
    }

    int current_inodenum = 0; 
    char* token_state = NULL;
    char* path_segment = strtok_r(path_copy, "/", &token_state);
    char* next_segment = strtok_r(NULL, "/", &token_state);

    while ((create_new && next_segment) || (!create_new && path_segment)) {
        off_t dir_size = get_nodeSize(current_inodenum);
        char* dir_content = malloc(dir_size);
        if (!dir_content) {
            free(path_copy);
            return -1;
        }

        realFileInode(current_inodenum, dir_content, dir_size, 0);

        struct wfs_dentry* entry;
        int found = 0;
        
        for (off_t pos = 0; pos < dir_size; pos += sizeof(struct wfs_dentry)) {
            entry = (struct wfs_dentry*)(dir_content + pos);
            if (entry->num != 0 && strcmp(entry->name, path_segment) == 0) {
                current_inodenum = entry->num;
                found = 1;
                break;
            }
        }

        free(dir_content);
        
        if (!found) {
            free(path_copy);
            return -1;
        }

        path_segment = next_segment;
        next_segment = strtok_r(NULL, "/", &token_state);
    }

    free(path_copy);
    return current_inodenum;
}


struct wfs_inode get_node(int disk_fd, int target_inode) {
    struct wfs_inode inode;

    off_t offset = block_large.i_blocks_ptr + target_inode * BLOCK_SIZE;
    size_t lseek_result = lseek(disk_fd, offset, SEEK_SET);
    if (lseek_result == -1) {
        exit(EXIT_FAILURE);
    }

    ssize_t bytesRead = read(disk_fd, &inode, sizeof(inode));

    if (bytesRead != sizeof(inode)) {
        exit(EXIT_FAILURE);
    }
    return inode;
}

static off_t read_indirect_blocks(int file_handle, off_t block_start_offset) {
    off_t block_count = 0;
    off_t block_pointer;
    
    for (int i = 0; i < (int) BLOCK_SIZE / sizeof(off_t); i++) {
        if (lseek(file_handle, block_start_offset + (i * sizeof(off_t)), SEEK_SET) == -1) {
            return -1;
        }
        
        if (read(file_handle, &block_pointer, sizeof(off_t)) != sizeof(off_t)) {
            return -1;
        }
        
        if (block_pointer == 0) {
            break;
        }
        
        block_count++;
    }
    
    return block_count;
}

off_t get_nodeSize(int target_inode) {
    off_t total_size = 0;
    int curr_disk = 0;
    
    do {
        const char* disk_path = disks[curr_disk];
        if (!disk_path || *disk_path == '\0') {
            break;
        }

        int fd = open(disk_path, O_RDONLY);
        if (fd < 0) {
            return -1;
        }

        struct wfs_inode node = get_node(fd, target_inode);
        
        off_t block_count = 0;
        for (int i = 0; i < D_BLOCK && node.blocks[i] != 0; i++) {
            block_count++;
        }
        
        if (node.blocks[D_BLOCK]) {
            off_t indirect_blocks = read_indirect_blocks(fd, node.blocks[D_BLOCK]);
            if (indirect_blocks >= 0) {
                block_count += indirect_blocks;
            }
        }
        
        close(fd);
        total_size += block_count * BLOCK_SIZE;
        curr_disk++;
        
    } while (block_large.raid_mode == 0 || (block_large.raid_mode == 1 && curr_disk == 0));
    
    return total_size;
}

int writeCurrInodeDisk(int disk_fd, struct wfs_inode target_inode) {
    if (disk_fd < 0) {
        return -1;
    }

    const off_t inode_position = block_large.i_blocks_ptr + 
                                (target_inode.num * BLOCK_SIZE);

    if (lseek(disk_fd, inode_position, SEEK_SET) == -1 ||
        write(disk_fd, &target_inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
        return -1;
    }

    return target_inode.num;
}

int write_prev_inode_disk(int file_handle, struct wfs_inode inode_data) {
    const size_t bitmap_bytes = (block_large.num_inodes + 7) / 8;
    char* bitmap = malloc(bitmap_bytes);
    if (!bitmap) {
        return -ENOMEM;
    }

    return_code_bitmap(file_handle, bitmap);

    int available_inode = -1;
    for (size_t byte = 0; byte < bitmap_bytes; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            if ((bitmap[byte] & (1 << bit)) == 0) {
                available_inode = byte * 8 + bit;
                if (available_inode < block_large.num_inodes) {
                    bitmap[byte] |= (1 << bit);
                    goto found_inode;
                }
            }
        }
    }

found_inode:
    if (available_inode == -1) {
        free(bitmap);
        return -ENOSPC;
    }

    inode_data.num = available_inode;

    if (lseek(file_handle, block_large.i_bitmap_ptr, SEEK_SET) != -1 &&
        write(file_handle, bitmap, bitmap_bytes) == bitmap_bytes) {
        free(bitmap);
        return writeCurrInodeDisk(file_handle, inode_data);
    }

    free(bitmap);
    return -EIO;
}

int writeInodeToSingleDisk(const char* disk_path, struct wfs_inode target_inode) {
    int fd = -1, index = -1;
    goto open_disk;

    cleanup:
        if (fd != -1) close(fd);
        return index;

    open_disk:
        if ((fd = open(disk_path, O_RDWR)) == -1) {
            goto cleanup;
        }
        index = write_prev_inode_disk(fd, target_inode);
        goto cleanup;
}



int readDiskByInode1(int disk_fd, int target_inode, char* out_buffer, size_t read_size, off_t start_offset) {
    if (!out_buffer || read_size == 0) return 0;
    
    struct wfs_inode inode = get_node(disk_fd, target_inode);
    size_t bytes_read = 0;
    size_t current_position = 0;
    
    // Handle direct blocks
    for (int block_idx = 0; block_idx < D_BLOCK && bytes_read < read_size; block_idx++) {
        if (inode.blocks[block_idx] == 0) break;

        if (lseek(disk_fd, inode.blocks[block_idx], SEEK_SET) == -1) return -1;

        if (current_position + BLOCK_SIZE <= start_offset) {
            current_position += BLOCK_SIZE;
            continue;
        }

        size_t block_offset = (start_offset > current_position) ? 
                             (start_offset - current_position) : 0;
                             
        if (block_offset < BLOCK_SIZE) {
            if (lseek(disk_fd, inode.blocks[block_idx] + block_offset, SEEK_SET) == -1) return -1;
            
            size_t available_bytes = BLOCK_SIZE - block_offset;
            size_t bytes_to_read = MIN(available_bytes, read_size - bytes_read);
            
            ssize_t bytes = read(disk_fd, out_buffer + bytes_read, bytes_to_read);
            if (bytes == -1) return -1;
            
            bytes_read += bytes;
            current_position += block_offset + bytes;
            
            if (bytes_read == read_size) break;
        }
        current_position += (block_offset == 0) ? BLOCK_SIZE : 0;
    }

    // Handle indirect blocks if needed
    if (bytes_read < read_size && inode.blocks[D_BLOCK]) {
        off_t indirect_blocks[(int) BLOCK_SIZE / sizeof(off_t)];
        
        if (lseek(disk_fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) return -1;
        if (read(disk_fd, indirect_blocks, sizeof(indirect_blocks)) != sizeof(indirect_blocks)) return -1;

        for (int idx = 0; idx < (int) BLOCK_SIZE / sizeof(off_t) && bytes_read < read_size; idx++) {
            if (indirect_blocks[idx] == 0) break;
            
            if (current_position + BLOCK_SIZE <= start_offset) {
                current_position += BLOCK_SIZE;
                continue;
            }

            size_t block_offset = (start_offset > current_position) ?
                                (start_offset - current_position) : 0;
                                
            if (block_offset < BLOCK_SIZE) {
                if (lseek(disk_fd, indirect_blocks[idx] + block_offset, SEEK_SET) == -1) return -1;
                
                size_t available_bytes = BLOCK_SIZE - block_offset;
                size_t bytes_to_read = MIN(available_bytes, read_size - bytes_read);
                
                ssize_t bytes = read(disk_fd, out_buffer + bytes_read, bytes_to_read);
                if (bytes == -1) return -1;
                
                bytes_read += bytes;
                current_position += block_offset + bytes;
                
                if (bytes_read == read_size) break;
            }
            current_position += (block_offset == 0) ? BLOCK_SIZE : 0;
        }
    }

    return bytes_read;
}

struct read_context {
    int fd;
    size_t bytes_read;
    size_t offset_read;
    struct wfs_inode inode;
};

static int read_block_helper(struct read_context* read_ctx, off_t block_pos, 
                           size_t* curr_pos, const char* dest_buffer, 
                           size_t total_size, off_t target_offset) {
    if (lseek(read_ctx->fd, block_pos, SEEK_SET) == -1) {
        return -1;
    }

    if (read_ctx->offset_read < target_offset) {
        if (target_offset - read_ctx->offset_read >= BLOCK_SIZE) {
            read_ctx->offset_read += BLOCK_SIZE;
            return 0;
        }
        if (lseek(read_ctx->fd, target_offset - read_ctx->offset_read, SEEK_CUR) == -1) {
            return -1;
        }
        *curr_pos = target_offset - read_ctx->offset_read;
        read_ctx->offset_read = target_offset;
    }

    size_t bytes_to_read = total_size - read_ctx->bytes_read;
    if (bytes_to_read > BLOCK_SIZE - *curr_pos) {
        bytes_to_read = BLOCK_SIZE - *curr_pos;
    }

    if (read(read_ctx->fd, (void*)(dest_buffer + read_ctx->bytes_read), bytes_to_read) != bytes_to_read) {
        return -1;
    }

    read_ctx->bytes_read += bytes_to_read;
    read_ctx->offset_read += BLOCK_SIZE;
    return 1;
}

int readDiskByInode(const char* disk_path, int inode_num, const char* out_buffer, 
                    size_t read_size, off_t start_offset) {
    if (read_size == 0) return 0;
    
    struct read_context ctx = {0};
    
    ctx.fd = open(disk_path, O_RDWR);
    if (ctx.fd == -1) {
        return -1;
    }
    
    ctx.inode = get_node(ctx.fd, inode_num);
    
    // Handle direct blocks
    for (int i = 0; i < D_BLOCK && ctx.bytes_read < read_size; i++) {
        if (ctx.inode.blocks[i] == 0) {
            off_t inode_size = get_nodeSize(inode_num);
            if (inode_size < 7 * 512) {
                close(ctx.fd);
                return ctx.bytes_read;
            }
            break;
        }
        
        size_t position = 0;
        int result = read_block_helper(&ctx, ctx.inode.blocks[i], &position, 
                                     out_buffer, read_size, start_offset);
        if (result < 0) {
            close(ctx.fd);
            return -1;
        }
        if (ctx.bytes_read == read_size) {
            close(ctx.fd);
            return read_size;
        }
    }

    // Handle indirect blocks
    if (ctx.inode.blocks[D_BLOCK] != 0) {
        off_t indirect_offset;
        for (int i = 0; i < (int) BLOCK_SIZE / sizeof(off_t) && ctx.bytes_read < read_size; i++) {
            if (lseek(ctx.fd, ctx.inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1 ||
                read(ctx.fd, &indirect_offset, sizeof(off_t)) != sizeof(off_t)) {
                close(ctx.fd);
                return -1;
            }

            if (indirect_offset == 0) break;

            size_t position = 0;
            int result = read_block_helper(&ctx, indirect_offset, &position, 
                                         out_buffer, read_size, start_offset);
            if (result < 0) {
                close(ctx.fd);
                return -1;
            }
            if (ctx.bytes_read == read_size) {
                close(ctx.fd);
                return read_size;
            }
        }
    }

    close(ctx.fd);
    return ctx.bytes_read;
}

static int handle_raid0_read(int inode_num, char* buffer, size_t size, off_t wanted_offset) {
    const int disk_count = block_large.size;
    size_t bytes_remaining = size;
    size_t offset_remaining = wanted_offset;
    size_t total_bytes_read = 0;
    int current_disk = 0;
    int block_iteration = 0;
    
    while (bytes_remaining > 0 || offset_remaining > 0) {
        size_t block_offset = 0;
        if (offset_remaining > 0) {
            block_offset = MIN(offset_remaining, BLOCK_SIZE);
            offset_remaining -= block_offset;
        }
        
        size_t bytes_to_read = 0;
        if (bytes_remaining > 0) {
            bytes_to_read = MIN(bytes_remaining, BLOCK_SIZE - block_offset);
            bytes_remaining -= bytes_to_read;
        }
        
        off_t block_pos = block_offset + (BLOCK_SIZE * block_iteration);
        if (readDiskByInode(disks[current_disk], inode_num, 
                           buffer + total_bytes_read, 
                           bytes_to_read, block_pos) == -1) {
            return -1;
        }
        
        total_bytes_read += bytes_to_read;
        
        if (++current_disk >= disk_count) {
            current_disk = 0;
            block_iteration++;
        }
    }
    return 0;
}

static int handle_raid1_read(int inode_num, char* buffer, size_t size, off_t wanted_offset) {
    char* consensus_buffer = malloc(size);
    if (!consensus_buffer) return -1;
    
    int consensus_count = 0;
    int first_read = 1;
    
    for (int disk = 0; disk < block_large.size; disk++) {
        int fd = open(disks[0], O_RDWR);
        if (fd == -1) continue;
        
        if (readDiskByInode1(fd, inode_num, buffer, size, wanted_offset) != -1) {
            if (first_read) {
                memcpy(consensus_buffer, buffer, size);
                consensus_count = 1;
                first_read = 0;
            } else if (memcmp(consensus_buffer, buffer, size) == 0) {
                consensus_count++;
            } else {
                consensus_count--;
            }
        }
        close(fd);
    }
    
    free(consensus_buffer);
    return (consensus_count > 0) ? 0 : -1;
}

int realFileInode(int inode_num, char* buffer, size_t size, off_t wanted_offset) {
    if (!buffer || size == 0) return -1;
    
    switch (block_large.raid_mode) {
        case 0:
            return handle_raid0_read(inode_num, buffer, size, wanted_offset);
        case 1:
            return handle_raid1_read(inode_num, buffer, size, wanted_offset);
        default:
            return -1;
    }
}

struct write_context {
    int fd;
    size_t bytes_written;
    size_t offset_written;
    const char* buffer;
    size_t size;
    off_t wanted_offset;
};



int writeDiskByInode1(int disk_fd, int target_inode, const char* input_buffer, 
                      size_t write_size, off_t target_offset) {
    if (write_size == 0) return 0;

    struct write_context ctx = {
        .fd = disk_fd,
        .bytes_written = 0,
        .offset_written = 0,
        .buffer = input_buffer,
        .size = write_size,
        .wanted_offset = target_offset
    };

    struct wfs_inode inode = get_node(disk_fd, target_inode);

    // Handle direct blocks
    off_t* direct_blocks = inode.blocks;
    for (int block_idx = 0; block_idx < D_BLOCK; block_idx++) {
        if (ctx.bytes_written >= write_size) {
            break;
        }
        
        if (direct_blocks[block_idx] == 0) {
            direct_blocks[block_idx] = block_allocate(disk_fd);
            if (direct_blocks[block_idx] == -1 || 
                writeCurrInodeDisk(disk_fd, inode) == -1) {
                return -1;
            }
        }

        if (lseek(ctx.fd, direct_blocks[block_idx], SEEK_SET) == -1) {
            return -1;
        }

        size_t position = 0;
        if (ctx.offset_written < ctx.wanted_offset) {
            if (ctx.wanted_offset - ctx.offset_written >= BLOCK_SIZE) {
                ctx.offset_written += BLOCK_SIZE;
                continue;
            }
            if (lseek(ctx.fd, ctx.wanted_offset - ctx.offset_written, SEEK_CUR) == -1) {
                return -1;
            }
            position = ctx.wanted_offset - ctx.offset_written;
            ctx.offset_written = ctx.wanted_offset;
        }

        size_t bytes_to_write = ctx.size - ctx.bytes_written;
        if (bytes_to_write > BLOCK_SIZE - position) {
            bytes_to_write = BLOCK_SIZE - position;
        }

        if (write(ctx.fd, ctx.buffer + ctx.bytes_written, bytes_to_write) != bytes_to_write) {
            return -1;
        }

        ctx.bytes_written += bytes_to_write;
    }
    
    if (ctx.bytes_written == write_size) {
        return ctx.bytes_written + ctx.offset_written;
    }

    // Handle indirect blocks
    if (inode.blocks[D_BLOCK] == 0) {
        off_t data_block_offset = block_allocate(disk_fd);
        if (data_block_offset == -1) {
            return -1;
        }
        inode.blocks[D_BLOCK] = data_block_offset;
        writeCurrInodeDisk(disk_fd, inode);
    }

    for (int i = 0; i < (int) BLOCK_SIZE / sizeof(off_t) && ctx.bytes_written < write_size; i++) {
        off_t indirect_offset;
        if (lseek(disk_fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1 ||
            read(disk_fd, &indirect_offset, sizeof(off_t)) != sizeof(off_t)) {
            return -1;
        }

        if (indirect_offset == 0) {
            indirect_offset = block_allocate(disk_fd);
            if (indirect_offset == -1) {
                return -1;
            }
            if (lseek(disk_fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1 ||
                write(disk_fd, &indirect_offset, sizeof(off_t)) != sizeof(off_t)) {
                return -1;
            }
        }

        if (lseek(ctx.fd, indirect_offset, SEEK_SET) == -1) {
            return -1;
        }

        size_t position = 0;
        if (ctx.offset_written < ctx.wanted_offset) {
            if (ctx.wanted_offset - ctx.offset_written >= BLOCK_SIZE) {
                ctx.offset_written += BLOCK_SIZE;
                continue;
            }
            if (lseek(ctx.fd, ctx.wanted_offset - ctx.offset_written, SEEK_CUR) == -1) {
                return -1;
            }
            position = ctx.wanted_offset - ctx.offset_written;
            ctx.offset_written = ctx.wanted_offset;
        }

        size_t bytes_to_write = ctx.size - ctx.bytes_written;
        if (bytes_to_write > BLOCK_SIZE - position) {
            bytes_to_write = BLOCK_SIZE - position;
        }

        if (write(ctx.fd, ctx.buffer + ctx.bytes_written, bytes_to_write) != bytes_to_write) {
            return -1;
        }

        ctx.bytes_written += bytes_to_write;
        if (ctx.bytes_written == write_size) {
            return ctx.bytes_written + ctx.offset_written;
        }
    }

    return ctx.bytes_written + ctx.offset_written;
}

struct write_state {
    int fd;
    size_t bytes_written;
    size_t offset_written;
    const char* buffer;
    struct wfs_inode inode;
};

static int handle_block_write(struct write_state* write_ctx, off_t target_block_pos, 
                            size_t total_size, off_t desired_offset) {
    if (lseek(write_ctx->fd, target_block_pos, SEEK_SET) == -1) {
        return -1;
    }

    size_t block_position = 0;
    if (write_ctx->offset_written < desired_offset) {
        if (desired_offset - write_ctx->offset_written >= BLOCK_SIZE) {
            write_ctx->offset_written += BLOCK_SIZE;
            return 0;
        }
        if (lseek(write_ctx->fd, desired_offset - write_ctx->offset_written, SEEK_CUR) == -1) {
            return -1;
        }
        block_position = desired_offset - write_ctx->offset_written;
        write_ctx->offset_written = desired_offset;
    }

    size_t bytes_to_write = total_size - write_ctx->bytes_written;
    if (bytes_to_write > BLOCK_SIZE - block_position) {
        bytes_to_write = BLOCK_SIZE - block_position;
    }

    if (write(write_ctx->fd, write_ctx->buffer + write_ctx->bytes_written, bytes_to_write) != bytes_to_write) {
        return -1;
    }

    write_ctx->bytes_written += bytes_to_write;
    write_ctx->offset_written += BLOCK_SIZE;
    return 1;
}

static int allocate_and_write_block(struct write_state* state, int block_index) {
    off_t new_block = block_allocate(state->fd);
    if (new_block == -1) return -1;
    
    state->inode.blocks[block_index] = new_block;
    return writeCurrInodeDisk(state->fd, state->inode);
}

int writeDiskByInode(const char* disk_path, int inode_num, const char* input_buffer, 
                     size_t write_size, off_t target_offset) {
    if (!disk_path || !input_buffer || write_size == 0) return 0;

    struct write_state state = {0};
    state.buffer = input_buffer;

    state.fd = open(disk_path, O_RDWR);
    if (state.fd == -1) return -1;
    
    state.inode = get_node(state.fd, inode_num);

    // Handle direct blocks
    for (int block_idx = 0; block_idx < D_BLOCK && state.bytes_written < write_size; block_idx++) {
        if (state.inode.blocks[block_idx] == 0) {
            off_t inode_size = get_nodeSize(inode_num);
            if (inode_size >= 7 * 512) break;
            
            if (allocate_and_write_block(&state, block_idx) == -1) {
                close(state.fd);
                return -1;
            }
        }

        int result = handle_block_write(&state, state.inode.blocks[block_idx], 
                                      write_size, target_offset);
        if (result < 0) {
            close(state.fd);
            return -1;
        }
        if (state.bytes_written == write_size) {
            close(state.fd);
            return write_size;
        }
    }

    // Handle indirect blocks
    if (state.inode.blocks[D_BLOCK] == 0) {
        if (allocate_and_write_block(&state, D_BLOCK) == -1) {
            close(state.fd);
            return -1;
        }
    }

    for (int i = 0; i < (int) BLOCK_SIZE / sizeof(off_t) && state.bytes_written < write_size; i++) {
        off_t indirect_offset = 0;
        if (lseek(state.fd, state.inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1 ||
            read(state.fd, &indirect_offset, sizeof(off_t)) != sizeof(off_t)) {
            close(state.fd);
            return -1;
        }

        if (indirect_offset == 0) {
            indirect_offset = block_allocate(state.fd);
            if (indirect_offset == -1 ||
                lseek(state.fd, state.inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1 ||
                write(state.fd, &indirect_offset, sizeof(off_t)) != sizeof(off_t)) {
                close(state.fd);
                return -1;
            }
        }

        int result = handle_block_write(&state, indirect_offset, write_size, target_offset);
        if (result < 0) {
            close(state.fd);
            return -1;
        }
        if (state.bytes_written == write_size) {
            close(state.fd);
            return write_size;
        }
    }

    close(state.fd);
    return state.bytes_written;
}

static int handle_raid0_write(int inode_num, char* buffer, size_t size, off_t offset) {
    const int disk_count = block_large.size;
    size_t remaining_bytes = size;
    size_t remaining_offset = offset;
    size_t written = 0;
    int disk_index = 0;
    int block_count = 0;

    while (remaining_bytes > 0 || remaining_offset > 0) {
        size_t current_offset = 0;
        if (remaining_offset > 0) {
            current_offset = (remaining_offset < BLOCK_SIZE) ? 
                            remaining_offset : BLOCK_SIZE;
            remaining_offset -= current_offset;
        }

        size_t write_size = 0;
        if (remaining_bytes > 0) {
            size_t available = BLOCK_SIZE - current_offset;
            write_size = (remaining_bytes < available) ? 
                         remaining_bytes : available;
            remaining_bytes -= write_size;
        }

        if (current_offset > 0 || write_size > 0) {
            off_t block_pos = current_offset + (BLOCK_SIZE * block_count);
            if (writeDiskByInode(disks[disk_index], inode_num, 
                buffer + written, write_size, block_pos) == -1) {
                return -1;
            }
        }

        written += write_size;
        block_count += (disk_index + 1) / disk_count;
        disk_index = (disk_index + 1) % disk_count;
    }

    size_t final_size = written + offset;
    
    for (int count = 0; count < disk_count; count++) {
        int disk_fd = open(disks[count], O_RDWR);
        if (disk_fd == -1) {
            continue;
        }
        
        struct wfs_inode current_inode = get_node(disk_fd, inode_num);
        if (current_inode.size < final_size) {
            current_inode.size = final_size;
            writeCurrInodeDisk(disk_fd, current_inode);
        }
        
        close(disk_fd);
    }
    return 0;
}

static int handle_raid1_write(int inode_num, char* buffer, size_t size, off_t offset) {
    const int disk_count = block_large.size;
    int success_count = 0;
    
    for (int disk_idx = 0; disk_idx < disk_count; disk_idx++) {
        int disk_fd = open(disks[disk_idx], O_RDWR);
        if (disk_fd < 0) {
            continue;
        }
        
        int bytes_written = writeDiskByInode1(disk_fd, inode_num, buffer, size, offset);
        if (bytes_written == -1) {
            close(disk_fd);
            continue;
        }
        
        struct wfs_inode current_inode = get_node(disk_fd, inode_num);
        current_inode.size = (current_inode.size > bytes_written) ? 
                            current_inode.size : bytes_written;
        
        if (writeCurrInodeDisk(disk_fd, current_inode) != -1) {
            success_count++;
        }
        
        close(disk_fd);
    }
    
    return (success_count > 0) ? 0 : -1;
}

int writeFileInode(int inode_num, char* buffer, size_t size, off_t offset) {
    switch (block_large.raid_mode) {
        case 0:
            return handle_raid0_write(inode_num, buffer, size, offset);
        case 1:
        case 2:
            return handle_raid1_write(inode_num, buffer, size, offset);
        default:
            return -1;
    }
}

int create_dir(const char* dir_path, mode_t permissions) {
    if (dir_path == NULL) {
        return -EINVAL;
    }

    if (return_inodenumber_path(dir_path, false) >= 0) {
        fprintf(stderr, "Directory '%s' already exists\n", dir_path);
        return -EEXIST;
    }

    int parent_inode = return_inodenumber_path(dir_path, true);
    if (parent_inode < 0) {
        fprintf(stderr, "Parent directory not found\n");
        return -ENOENT;
    }

    struct wfs_inode dir_inode = {
        .mode = permissions,
        .uid = getuid(),
        .gid = getgid(),
        .size = 0,
        .nlinks = 2,
        .atim = time(NULL),
        .mtim = time(NULL),
        .ctim = time(NULL)
    };
    memset(dir_inode.blocks, 0, sizeof(dir_inode.blocks));

    int inode_index = -1;

    // Write inode to all disks in RAID configuration
    for (int disk_idx = 0; disks[disk_idx][0] != '\0'; disk_idx++) {
        inode_index = writeInodeToSingleDisk(disks[disk_idx], dir_inode);
        if (inode_index == -1 || inode_index == -ENOSPC) {
            return inode_index;
        }
    }

    if (inode_index < 0) {
        return inode_index;
    }

    // Extract directory name from path
    char dir_name[256];
    const char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        strcpy(dir_name, last_slash + 1);
    } else {
        strcpy(dir_name, dir_path);
    }

    // Find free directory entry slot
    const size_t DENTRY_SIZE = sizeof(struct wfs_dentry);
    off_t parent_size = get_nodeSize(parent_inode);
    char dir_buffer[parent_size];
    realFileInode(parent_inode, dir_buffer, parent_size, 0);

    off_t free_slot = 0;
    struct wfs_dentry temp_entry;
    for (; free_slot < parent_size; free_slot += DENTRY_SIZE) {
        memcpy(&temp_entry, dir_buffer + free_slot, DENTRY_SIZE);
        if (temp_entry.num == 0) {  
            break;
        }
    }

    // Create and write directory entry
    struct wfs_dentry dir_record = {
        .num = inode_index
    };
    strcpy(dir_record.name, dir_name);

    writeFileInode(parent_inode, (char*)&dir_record, DENTRY_SIZE, free_slot);
    return 0;
}


static int wfs_getattr(const char* filepath, struct stat* stat_buffer) {
    int inode = -1;
    int fd = -1;
    struct wfs_inode node;
    
    memset(stat_buffer, 0, sizeof(struct stat));

    if (!filepath || !stat_buffer) {
        return -ENOENT;
    }
        
    if (*filepath == '/' && *(filepath + 1) == '\0') {
        fd = open(disks[0], O_RDWR);
        if (fd < 0) {
            return -1;
        }
        close(fd);
        inode = 0;
    } 
    else {
        inode = return_inodenumber_path(filepath, false);
        if (inode < 0) {
            return -ENOENT;
        }
    }
    
    fd = open(disks[0], O_RDWR);
    if (fd < 0) {
        return -ENOENT;
    }
    
    node = get_node(fd, inode);
    close(fd);
    
    stat_buffer->st_mtime = node.mtim;
    stat_buffer->st_gid = node.gid;
    stat_buffer->st_atime = node.atim;
    stat_buffer->st_mode = node.mode;
    stat_buffer->st_size = node.size;
    stat_buffer->st_ctime = node.ctim;
    stat_buffer->st_uid = node.uid;

    fflush(stdout);
    return 0;
}

static int wfs_read(const char* file_path, char* out_buffer, size_t read_size, 
                   off_t start_offset, struct fuse_file_info* fuse_info) {
    int status = 0;
    int inode_num;
    
    if (!file_path || !out_buffer) {
        return -EINVAL;
    }

    inode_num = return_inodenumber_path(file_path, false);
    if (inode_num < 0) {
        status = -ENOENT;
        goto cleanup;
    }

    if (read_size == 0) {
        status = 0;
        goto cleanup;
    }

    realFileInode(inode_num, out_buffer, read_size, start_offset);
    status = read_size;

cleanup:
    return status;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    int status;
    
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    status = create_dir(path, mode | S_IFREG);
    
    if (status == -ENOSPC) {
        return -ENOSPC;
    }
    
    return 0;
}

static int wfs_mkdir(const char* path, mode_t mode) {
    int status;
    
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    status = create_dir(path, mode | S_IFDIR);
    
    if (status == -ENOSPC) {
        return -ENOSPC;
    }
    
    return 0;
}

static int clear_inode_bitmap(int fd, int inode_num) {
    size_t bitmap_size = (block_large.num_inodes + 7) / 8;
    char* bitmap = (char*)malloc(bitmap_size);
    if (!bitmap) return -ENOMEM;
    
    return_code_bitmap(fd, bitmap);
    bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));
    
    if (lseek(fd, block_large.i_bitmap_ptr, SEEK_SET) == -1 || 
        write(fd, bitmap, bitmap_size) != bitmap_size) {
        free(bitmap);
        return -EIO;
    }
    
    free(bitmap);
    return 0;
}

int removeItem(const char* path) {
    if (!path) return -EINVAL;
    
    int inode_par = return_inodenumber_path(path, true);
    int inode_sel = return_inodenumber_path(path, false);
    if (inode_par < 0 || inode_sel < 0) return -ENOENT;
    
    char* path_copy = strdup(path);
    char* filename = strrchr(path_copy, '/');
    filename = filename ? filename + 1 : path_copy;
    
    for (int disk_idx = 0; disk_idx < block_large.size; disk_idx++) {
        int fd = open(disks[disk_idx], O_RDWR);
        if (fd < 0) continue;
        
        clear_inode_bitmap(fd, inode_sel);
        struct wfs_inode target = get_node(fd, inode_sel);
        
        size_t bitmap_size = (block_large.num_data_blocks + 7) / 8;
        char* bitmap = (char*)malloc(bitmap_size);
        if (!bitmap) {
            close(fd);
            free(path_copy);
            return -ENOMEM;
        }
        
        if (lseek(fd, block_large.d_bitmap_ptr, SEEK_SET) == -1) {
            free(bitmap);
            close(fd);
            free(path_copy);
            return -EIO;
        }
        
        if (read(fd, bitmap, bitmap_size) != bitmap_size) {
            free(bitmap);
            close(fd);
            free(path_copy);
            return -EIO;
        }

        if (target.blocks[D_BLOCK]) {
            char indirect_buffer[BLOCK_SIZE];
            if (lseek(fd, target.blocks[D_BLOCK], SEEK_SET) != -1) {
                if (read(fd, indirect_buffer, BLOCK_SIZE) == BLOCK_SIZE) {
                    off_t* block_ptrs = (off_t*)indirect_buffer;
                    for (int i = 0; i < (int)BLOCK_SIZE / sizeof(off_t); i++) {
                        if (block_ptrs[i]) {
                            int block_num = (block_ptrs[i] - block_large.d_blocks_ptr) / BLOCK_SIZE;
                            bitmap[block_num / 8] &= ~(1 << (block_num % 8));
                        }
                    }
                }
            }
        }

        for (int i = 0; i <= D_BLOCK; i++) {
            if (target.blocks[i]) {
                int block_num = (target.blocks[i] - block_large.d_blocks_ptr) / BLOCK_SIZE;
                bitmap[block_num / 8] &= ~(1 << (block_num % 8));
            }
        }

        if (lseek(fd, block_large.d_bitmap_ptr, SEEK_SET) != -1) {
            write(fd, bitmap, bitmap_size);
        }
        
        free(bitmap);
        
        off_t parent_size = get_nodeSize(inode_par);
        char* dir_content = malloc(parent_size);
        if (dir_content) {
            realFileInode(inode_par, dir_content, parent_size, 0);
            
            struct wfs_dentry* entries = (struct wfs_dentry*)dir_content;
            for (size_t i = 0; i < parent_size / sizeof(struct wfs_dentry); i++) {
                if (strcmp(entries[i].name, filename) == 0) {
                    struct wfs_dentry empty = {.num = 0};
                    writeFileInode(inode_par, (char*)&empty, 
                                   sizeof(empty), i * sizeof(struct wfs_dentry));
                    break;
                }
            }
            free(dir_content);
        }
        close(fd);
    }
    
    free(path_copy);
    return 0;
}

static int wfs_unlink(const char* path) {
    int status = 0;
    
    if (!path) {
        return -EINVAL;
    }

    status = removeItem(path);
    
    if (status == -ENOENT) {
        return -ENOENT;
    }
    
    if (status == -EINVAL) {
        return -EINVAL;
    }

    return 0;
}

static int wfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    int status = 0;
    int inode_num;
    
    if (!path || !buf) {
        return -EINVAL;
    }

    if ((inode_num = return_inodenumber_path(path, true)) == -1) {
        return -ENOENT;
    }

    status = wfs_mknod(path, 0777, 0);
    if (status < 0) {
        return status;
    }

    inode_num = return_inodenumber_path(path, false);
    if (inode_num < 0) {
        return -ENOENT;
    }

    status = writeFileInode(inode_num, (char *)buf, size, offset);
    if (status < 0) {
        return status;
    }

    return size;
}

static int wfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    if (!path || !buf || !filler) {
        return -EINVAL;
    }

    int dir_inode = return_inodenumber_path(path, false);
    if (dir_inode < 0) {
        return -ENOENT;
    }

    off_t dir_size = get_nodeSize(dir_inode);
    if (dir_size <= 0) {
        return 0;
    }

    char* dir_buffer = malloc(dir_size);
    if (!dir_buffer) {
        return -ENOMEM;
    }

    if (realFileInode(dir_inode, dir_buffer, dir_size, 0) < 0) {
        free(dir_buffer);
        return -EIO;
    }

    struct wfs_dentry* curr_entry;
    size_t entry_size = sizeof(struct wfs_dentry);
    for (off_t pos = 0; pos < dir_size; pos += entry_size) {
        curr_entry = (struct wfs_dentry*)(dir_buffer + pos);
        if (curr_entry->num != 0) {
            if (filler(buf, curr_entry->name, NULL, 0) != 0) {
                break;
            }
        }
    }

    free(dir_buffer);
    return 0;
}

static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod = wfs_mknod,
  .mkdir = wfs_mkdir,
  .unlink = wfs_unlink,
  .rmdir = removeItem,
  .read = wfs_read,
  .write = wfs_write,
  .readdir = wfs_readdir,
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <mountpoint> [fuse options]\n", argv[0]);
        return -1;
    }

    int fuse_arg_index = argc - 1;
    {
        int scan_idx = 0;
        while (scan_idx < argc) {
            if (argv[scan_idx][0] == '-') {
                fuse_arg_index = scan_idx;
                break;
            }
            scan_idx++;
        }
    }

    int total_fuse_args = argc - fuse_arg_index;
    char* fuse_args[total_fuse_args];
    {
        int copy_idx = fuse_arg_index;
        int offset = 0;
        while (copy_idx < argc) {
            fuse_args[offset++] = argv[copy_idx++];
        }
    }

    {
        strcpy(disk, argv[1]);
        int primary_fd = open(disk, O_RDWR);
        if (primary_fd == -1) {
            perror("ERROR: COULD NOT OPEN DISK\n");
            return -1;
        }
        block_large = block_val(primary_fd);
        close(primary_fd);
    }


    {
        int j_val = 1;
        while (j_val < fuse_arg_index) {
            int i_val = 1;
            while (i_val < fuse_arg_index) {
                int temp_fd = open(argv[i_val], O_RDWR);
                if (temp_fd != -1) {
                    block_large = block_val(temp_fd);
                    if (block_large.count == (j_val - 1)) {
                        strcpy(disks[j_val - 1], argv[i_val]);
                    }
                    close(temp_fd);
                }
                i_val++;
            }
            j_val++;
        }
        strcpy(disks[fuse_arg_index], "\0");
    }

    return fuse_main(total_fuse_args, fuse_args, &ops, NULL);
}
