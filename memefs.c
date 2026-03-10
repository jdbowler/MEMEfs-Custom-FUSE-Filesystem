#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <time.h>

static int img_fd;

typedef struct {
    char signature[16];
    uint8_t mounted_flag;
    uint8_t reserved[3];
    uint32_t version;
    uint8_t creation_timestamp[8];
    uint16_t main_fat_block;
    uint16_t main_fat_size;
    uint16_t backup_fat_block;
    uint16_t backup_fat_size;
    uint16_t dir_start_block;
    uint16_t dir_size;
    uint16_t user_blocks_count;
    uint16_t first_user_block;
    char volume_label[16];
    uint8_t unused[448];
} memefs_superblock_t;

static memefs_superblock_t superblock;

typedef struct {
    uint16_t type_perm;
    uint16_t start_block;
    char filename[11];
    uint8_t unused;
    uint8_t timestamp[8];
    uint32_t file_size;
    uint16_t owner_uid;
    uint16_t group_gid;
} memefs_dir_entry_t;

#define NUM_DIR_ENTRIES 224
static memefs_dir_entry_t directory[NUM_DIR_ENTRIES];

#define NUM_BLOCKS 256
static uint16_t fat[NUM_BLOCKS];

static void entry_convert(const memefs_dir_entry_t *entry, memefs_dir_entry_t *be_entry) {
    be_entry->type_perm = htons(entry->type_perm);
    be_entry->start_block = htons(entry->start_block);
    memcpy(be_entry->filename, entry->filename, 11);
    be_entry->unused = entry->unused;
    memcpy(be_entry->timestamp, entry->timestamp, 8);
    be_entry->file_size = htonl(entry->file_size);
    be_entry->owner_uid = htons(entry->owner_uid);
    be_entry->group_gid = htons(entry->group_gid);
}

static uint8_t to_bcd(uint8_t num) {
    if (num > 99) {
        return 0xFF;
    }

    return ((num / 10) << 4) | (num % 10);
}

static void generate_bcd_timestamp(uint8_t bcd_time[8]) {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    int full_year = utc->tm_year + 1900;
    bcd_time[0] = to_bcd(full_year / 100);
    bcd_time[1] = to_bcd(full_year % 100);
    bcd_time[2] = to_bcd(utc->tm_mon + 1);
    bcd_time[3] = to_bcd(utc->tm_mday);
    bcd_time[4] = to_bcd(utc->tm_hour);
    bcd_time[5] = to_bcd(utc->tm_min);
    bcd_time[6] = to_bcd(utc->tm_sec);
    bcd_time[7] = 0x00;
}

static time_t bcd_to_time(const uint8_t bcd_time[8]) {
    struct tm tm = {0};
    tm.tm_year = (bcd_time[0] >> 4) * 1000 + (bcd_time[0] & 0x0F) * 100 + (bcd_time[1] >> 4) * 10 + (bcd_time[1] & 0x0F) - 1900;
    tm.tm_mon = ((bcd_time[2] >> 4) * 10 + (bcd_time[2] & 0x0F)) - 1;
    tm.tm_mday = (bcd_time[3] >> 4) * 10 + (bcd_time[3] & 0x0F);
    tm.tm_hour = (bcd_time[4] >> 4) * 10 + (bcd_time[4] & 0x0F);
    tm.tm_min = (bcd_time[5] >> 4) * 10 + (bcd_time[5] & 0x0F);
    tm.tm_sec = (bcd_time[6] >> 4) * 10 + (bcd_time[6] & 0x0F);

    return mktime(&tm);
}

static int load_superblock(void) {
    if (pread(img_fd, &superblock, sizeof(superblock), 255 * 512) != sizeof(superblock)) {
        return -1;
    }

    superblock.version = ntohl(superblock.version);
    superblock.main_fat_block = ntohs(superblock.main_fat_block);
    superblock.main_fat_size = ntohs(superblock.main_fat_size);
    superblock.backup_fat_block = ntohs(superblock.backup_fat_block);
    superblock.backup_fat_size = ntohs(superblock.backup_fat_size);
    superblock.dir_start_block = ntohs(superblock.dir_start_block);
    superblock.dir_size = ntohs(superblock.dir_size);
    superblock.user_blocks_count = ntohs(superblock.user_blocks_count);
    superblock.first_user_block = ntohs(superblock.first_user_block);

    if (memcmp(superblock.signature, "?MEMEFS++CMSC421", 16) != 0) {
        return -1;
    }

    return 0;
}

static int load_directory(void) {
    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        uint16_t block = superblock.dir_start_block - (i / 16);
        off_t offset = block * 512 + (i % 16) * 32;

        if (pread(img_fd, &directory[i], sizeof(memefs_dir_entry_t), offset) != sizeof(memefs_dir_entry_t)) {
            return -1;
        }
        
        directory[i].type_perm = ntohs(directory[i].type_perm);
        directory[i].start_block = ntohs(directory[i].start_block);
        directory[i].file_size = ntohl(directory[i].file_size);
        directory[i].owner_uid = ntohs(directory[i].owner_uid);
        directory[i].group_gid = ntohs(directory[i].group_gid);
    }
    return 0;
}

static int load_fat(void) {
    uint16_t fat_block = superblock.main_fat_block;

    if (pread(img_fd, fat, sizeof(fat), fat_block * 512) != sizeof(fat)) {
        return -1;
    }

    for (int i = 0; i < NUM_BLOCKS; i++) {
        fat[i] = ntohs(fat[i]);
    }

    return 0;
}

static int save_directory(void) {
    uint16_t dir_start = superblock.dir_start_block;

    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        uint16_t block = dir_start - (i / 16);
        off_t offset = block * 512 + (i % 16) * 32;
        memefs_dir_entry_t be_entry;
        entry_convert(&directory[i], &be_entry);

        if (pwrite(img_fd, &be_entry, sizeof(be_entry), offset) != sizeof(be_entry)) {
            return -1;
        }
    }

    return 0;
}

static int save_fat(void) {
    uint16_t fat_block = superblock.main_fat_block;
    uint16_t temp_fat[NUM_BLOCKS];

    for (int i = 0; i < NUM_BLOCKS; i++) {
        temp_fat[i] = htons(fat[i]);
    }

    if (pwrite(img_fd, temp_fat, sizeof(temp_fat), fat_block * 512) != sizeof(temp_fat)) {
        return -1;
    }

    fat_block = superblock.backup_fat_block;

    if (pwrite(img_fd, temp_fat, sizeof(temp_fat), fat_block * 512) != sizeof(temp_fat)) {
        return -1;
    }

    return 0;
}

static void get_filename(const memefs_dir_entry_t *entry, char *name) {
    int name_len = strnlen(entry->filename, 8);
    memcpy(name, entry->filename, name_len);
    int ext_len = strnlen(entry->filename + 8, 3);
    
    if (ext_len > 0) {
        name[name_len] = '.';
        memcpy(name + name_len + 1, entry->filename + 8, ext_len);
        name[name_len + 1 + ext_len] = '\0';
    } else {
        name[name_len] = '\0';
    }
}

static memefs_dir_entry_t *find_dir_entry(const char *path) {
    if (path[0] != '/' || path[1] == '\0') {
        return NULL;
    }

    const char *filename = path + 1;

    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (directory[i].type_perm == 0) {
            continue;
        }

        char entry_name[13];
        get_filename(&directory[i], entry_name);

        if (strcmp(entry_name, filename) == 0) {
            return &directory[i];
        }
    }
    return NULL;
}

static uint16_t find_free_block(void) {
    for (uint16_t i = superblock.first_user_block; i < NUM_BLOCKS; i++) {
        if (fat[i] == 0x0000) {
            return i;
        }
    }

    return 0xFFFF;
}

static uint16_t allocate_block(void) {
    uint16_t block = find_free_block();

    if (block == 0xFFFF) {
        return 0xFFFF;
    }

    fat[block] = 0xFFFF;
    return block;
}

static void free_block_chain(uint16_t start_block) {
    uint16_t current = start_block;

    while (current != 0xFFFF) {
        uint16_t next = fat[current];
        fat[current] = 0x0000;
        current = next;
    }
}

static void set_filename(memefs_dir_entry_t *entry, const char *filename) {
    const char *dot = strchr(filename, '.');

    if (dot == NULL) {
        int name_len = strlen(filename);

        if (name_len > 8) {
            name_len = 8;
        }

        memcpy(entry->filename, filename, name_len);
        memset(entry->filename + name_len, 0, 8 - name_len);
        memset(entry->filename + 8, 0, 3);
    } else {
        int name_len = dot - filename;

        if (name_len > 8) {
            name_len = 8;
        }

        memcpy(entry->filename, filename, name_len);
        memset(entry->filename + name_len, 0, 8 - name_len);
        int ext_len = strlen(dot + 1);

        if (ext_len > 3) {
            ext_len = 3;
        }

        memcpy(entry->filename + 8, dot + 1, ext_len);
        memset(entry->filename + 8 + ext_len, 0, 3 - ext_len);
    }
}

static uint16_t get_block_for_offset(memefs_dir_entry_t *entry, size_t offset, int allocate) {
    size_t block_size = 512;
    uint16_t block_index = offset / block_size;
    uint16_t current_block = entry->start_block;
    uint16_t prev_block = 0xFFFF;

    if (current_block == 0xFFFF && allocate && block_index == 0) {
        current_block = allocate_block();

        if (current_block == 0xFFFF) {
            return 0xFFFF;
        }

        entry->start_block = current_block;
    }

    for (uint16_t i = 0; i < block_index && current_block != 0xFFFF; i++) {
        prev_block = current_block;
        current_block = fat[current_block];
    }

    if (current_block == 0xFFFF && allocate) {
        if (prev_block == 0xFFFF) {
            return 0xFFFF;
        }

        current_block = allocate_block();

        if (current_block == 0xFFFF) {
            return 0xFFFF;
        }

        fat[prev_block] = current_block;
    }

    return current_block;
}

static int memefs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = 0;
        stbuf->st_gid = 0;
        time_t creation_time = bcd_to_time(superblock.creation_timestamp);
        stbuf->st_atime = creation_time;
        stbuf->st_mtime = creation_time;
        stbuf->st_ctime = creation_time;
        return 0;
    }

    memefs_dir_entry_t *entry = find_dir_entry(path);

    if (entry == NULL) {
        return -ENOENT;
    }

    stbuf->st_mode = S_IFREG | (entry->type_perm & 0777);
    stbuf->st_nlink = 1;
    stbuf->st_size = entry->file_size;
    stbuf->st_uid = entry->owner_uid;
    stbuf->st_gid = entry->group_gid;
    time_t file_time = bcd_to_time(entry->timestamp);
    stbuf->st_atime = file_time;
    stbuf->st_mtime = file_time;
    stbuf->st_ctime = file_time;

    return 0;
}

static int memefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (directory[i].type_perm != 0) {
            char name[13];
            get_filename(&directory[i], name);
            filler(buf, name, NULL, 0, 0);
        }
    }

    return 0;
}

static int memefs_open(const char *path, struct fuse_file_info *fi) {
    (void) fi;

    if (strcmp(path, "/") == 0) {
        return -EISDIR;
    }

    memefs_dir_entry_t *entry = find_dir_entry(path);

    if (entry == NULL) {
        return -ENOENT;
    }

    return 0;
}

static int memefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    memefs_dir_entry_t *entry = find_dir_entry(path);

    if (entry == NULL) {
        return -ENOENT;
    }

    if (offset >= entry->file_size) {
        return 0;
    }

    size_t bytes_to_read = entry->file_size - offset;

    if (bytes_to_read > size) {
        bytes_to_read = size;
    }

    size_t bytes_read = 0;
    uint16_t block = entry->start_block;
    size_t current_offset = 0;

    while (current_offset + 512 <= (size_t)offset && block != 0xFFFF) {
        current_offset += 512;
        block = fat[block];
    }

    if (block == 0xFFFF) {
        return 0;
    }

    while (bytes_read < bytes_to_read && block != 0xFFFF) {
        size_t block_offset = (offset + bytes_read) % 512;
        size_t to_read = 512 - block_offset;

        if (to_read > bytes_to_read - bytes_read) {
            to_read = bytes_to_read - bytes_read;
        }

        if (pread(img_fd, buf + bytes_read, to_read, block * 512 + block_offset) != (ssize_t)to_read) {
            return -EIO;
        }

        bytes_read += to_read;
        block = fat[block];
    }

    return bytes_read;
}

static int memefs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    
    int free_entry = -1;

    for (int i = 0; i < NUM_DIR_ENTRIES; i++) {
        if (directory[i].type_perm == 0) {
            free_entry = i;
            break;
        }
    }

    if (free_entry == -1) {
        return -ENOSPC;
    }

    memefs_dir_entry_t *entry = &directory[free_entry];
    entry->type_perm = 0xFE00 | (mode & 0777);
    entry->start_block = 0xFFFF;
    set_filename(entry, path + 1);
    entry->file_size = 0;
    entry->owner_uid = getuid();
    entry->group_gid = getgid();
    generate_bcd_timestamp(entry->timestamp);

    if (save_directory() < 0) {
        return -EIO;
    }

    return 0;
}

static int memefs_unlink(const char *path) {
    memefs_dir_entry_t *entry = find_dir_entry(path);
    
    if (entry == NULL) {
        return -ENOENT;
    }

    if (entry->start_block != 0xFFFF) {
        free_block_chain(entry->start_block);
    }

    entry->type_perm = 0;

    if (save_fat() < 0 || save_directory() < 0) {
        return -EIO;
    }

    return 0;
}

static int memefs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    
    memefs_dir_entry_t *entry = find_dir_entry(path);

    if (entry == NULL) {
        return -ENOENT;
    }

    size_t new_end = offset + size;

    if (new_end > entry->file_size) {
        entry->file_size = new_end;
    }

    size_t bytes_written = 0;

    while (bytes_written < size) {
        uint16_t block = get_block_for_offset(entry, offset + bytes_written, 1);

        if (block == 0xFFFF) {
            return -ENOSPC;
        }

        size_t block_offset = (offset + bytes_written) % 512;
        size_t to_write = 512 - block_offset;

        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }

        if (pwrite(img_fd, buf + bytes_written, to_write, block * 512 + block_offset) != (ssize_t)to_write) {
            return -EIO;
        }

        bytes_written += to_write;
    }

    generate_bcd_timestamp(entry->timestamp);

    if (save_fat() < 0 || save_directory() < 0) {
        return -EIO;
    }

    return bytes_written;
}

static int memefs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    
    memefs_dir_entry_t *entry = find_dir_entry(path);

    if (entry == NULL) {
        return -ENOENT;
    }

    if (size < entry->file_size) {
        uint16_t block = entry->start_block;
        size_t current_size = 0;
        uint16_t prev_block = 0xFFFF;

        while (block != 0xFFFF && current_size < (size_t)size) {
            current_size += 512;
            prev_block = block;
            block = fat[block];
        }

        if (block != 0xFFFF) {
            fat[prev_block] = 0xFFFF;
            free_block_chain(block);
        }
    }

    entry->file_size = size;
    generate_bcd_timestamp(entry->timestamp);

    if (save_fat() < 0 || save_directory() < 0) {
        return -EIO;
    }

    return 0;
}

static int memefs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void) path;
    (void) tv;
    (void) fi;

    return 0;
}

static struct fuse_operations memefs_oper = {
    .getattr  = memefs_getattr,
    .readdir  = memefs_readdir,
    .open     = memefs_open,
    .read     = memefs_read,
    .create   = memefs_create,
    .unlink   = memefs_unlink,
    .write    = memefs_write,
    .truncate = memefs_truncate,
    .utimens  = memefs_utimens,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <filesystem image> <mount point>\n", argv[0]);
        return 1;
    }

    img_fd = open(argv[1], O_RDWR);
    if (img_fd < 0) {
        perror("Failed to open filesystem image");
        return 1;
    }

    if (load_superblock() < 0) {
        fprintf(stderr, "Failed to load superblock\n");
        close(img_fd);
        return 1;
    }

    if (load_directory() < 0) {
        fprintf(stderr, "Failed to load directory\n");
        close(img_fd);
        return 1;
    }

    if (load_fat() < 0) {
        fprintf(stderr, "Failed to load FAT\n");
        close(img_fd);
        return 1;
    }

    return fuse_main(argc - 1, argv + 1, &memefs_oper, NULL);
}