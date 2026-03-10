
# Project 4: MEMEfs — A Custom FUSE Filesystem

MEMEfs is a custom FUSE-based filesystem designed to provide a simple storage solution with a single-level directory structure. It supports basic file operations such as reading, writing, creating, and deleting files within a 128 KiB volume. It uses an 8.3 filename format, limiting filenames to 8 characters plus a 3 character extension. It is constrained by a maximum of 256 blocks and 224 directory entries.

## How to Build?

```bash
# Step 1: Compile memefs & mkmemefs

make all

# Step 2: Run mkmemefs to create an memefs image

./mkmemefs <image_filename> <volume-name>

# Step 3: Create Test Dir (under /tmp). Note that this dir only needs to be created once. DO NOT PULL FILES IN THIS DIR (IT NEEDS TO REMAIN EMPTY). YOU'VE BEEN WARNED. 

make create_dir

# Step 4: Mount the Filesystem

make mount_memefs

```

# Explain the Build Process

The build process starts with compiling the MEMEfs source code and the image creation tool then generating a filesystem image and finally mounting it using FUSE.

Compile mkmemefs:

The make build_mkmemefs command compiles the mkmemefs.c source file into an executable. This is used to create the MEMEfs image.

Create the MEMEfs Image:

Running ./mkmemefs <image_filename> <volume-name> generates a filesystem image with the specified volume label.

Create the Mount Directory:

The make create_dir command creates the /tmp/memefs directory which acts as the mount point. This step is only required once.

Mount the Filesystem:

The make mount_memefs command mounts the filesystem image at /tmp/memefs which makes the filesystem accessible. Make debug can also be used to mount and debug memefs in the foreground.

# Explain Memefs Source Code
The MEMEfs source code implements a FUSE-based filesystem with a single-level directory structure. It includes the following file operations:

- getattr: Gets file attributes like size, permissions, and timestamps

- readdir: Lists all files in the root directory

- open: Opens a file for reading or writing

- read: Reads file data using the FAT to locate blocks

- create: Creates a new file and sets up its metadata in the directory entry

- unlink: Deletes a file by marking its directory entry and FAT blocks as free

- write: Writes data to a file, allocating additional blocks using the FAT as needed

- truncate: Adjusts file size by either freeing or allocating blocks in the FAT

The filesystem uses a FAT to manage block allocation and stores file metadata in directory entries. Timestamps are maintained in BCD format with helper functions to generate and convert them for the struct stat.

# References
[IBM: l-fuse](https://developer.ibm.com/articles/l-fuse/)
[ntohl](https://linux.die.net/man/3/ntohl)
[Wikipedia: Endianness](https://en.wikipedia.org/wiki/Endianness#File_systems)
[Linux manual](https://man7.org/linux/man-pages/man3/ntohl.3.html)
[Linux Kernel API](https://docs.kernel.org/core-api/kernel-api.html)
[Libfuse hello.c](https://www.google.com/url?q=https://github.com/libfuse/libfuse/blob/master/example/hello.c&sa=D&source=docs&ust=1747018252028784&usg=AOvVaw36UoUVmQyXrxpc9FMMJvH4)
[FUSE](https://docs.kernel.org/filesystems/fuse.html)
[FAT](https://wiki.osdev.org/FAT)

## Author
- [@jdbowler](https://github.com/jdbowler)