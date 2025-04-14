# raidfs

A lightweight block-based filesystem written in C, designed to run in user space using [FUSE](https://github.com/libfuse/libfuse). Implements RAID 0 (striping), RAID 1 (mirroring), and RAID 1v (verified mirroring) across multiple disk image files.

## 🧱 Features

- Block-based disk layout with support for:
  - Superblock, inode bitmap, data bitmap
  - Direct and indirect data blocks
  - File and directory creation/deletion
- FUSE-backed file operations:
  - `getattr`, `mknod`, `mkdir`, `unlink`, `rmdir`, `read`, `write`, `readdir`
- RAID Support:
  - **RAID 0**: striped data across disks
  - **RAID 1**: full mirror
  - **RAID 1v**: mirrored + verified read consistency

## ⚙️ Build

```bash
make
```

## 🚀 Usage

### Create Disk Images

```bash
./create_disk.sh
```

This will generate one or more zeroed-out `.img` files for use as virtual disks.

### Format the Filesystem

```bash
./mkfs -r 1 -d test-disk1.img -d test-disk2.img -i 32 -b 200
```

- `-r` — RAID mode (`0`, `1`, or `1v`)
- `-d` — Disk image(s)
- `-i` — Number of inodes
- `-b` — Number of data blocks (must be a multiple of 32)

### Mount the Filesystem

```bash
mkdir mnt
./wfs test-disk1.img test-disk2.img -f -s mnt/
```

### Interact With It

Once mounted, use standard shell commands in the `mnt/` directory:

```bash
mkdir mnt/docs
echo "hello world" > mnt/docs/note.txt
cat mnt/docs/note.txt
rm mnt/docs/note.txt
```

### Unmount

```bash
./umount.sh mnt
```

## 🔧 Tools & Scripts

| File | Description |
|------|-------------|
| `create_disk.sh` | Quickly generate empty disk image files |
| `umount.sh` | Wrapper to safely unmount a FUSE mount |
| `wfsverify.py` | Python verifier for correctness checks |
| `wfs-check-metadata2.py` | Additional metadata integrity script |

## 📁 File Structure

```
.
├── mkfs.c            # Filesystem formatter
├── wfs.c             # Main FUSE-based FS implementation
├── wfs.h             # Shared FS definitions
├── utils.c           # Helpers for metadata & bitmap handling
├── create_disk.sh    # Disk generator script
├── umount.sh         # Unmount utility
├── test-disk*.img    # Disk image files
├── wfsverify.py      # FS checker
├── Makefile          # Build configuration
├── mnt/              # Mount point
```

## 🧪 Debug Tips

- Run with `-f` to see logs in the terminal
- Use `xxd` or `hexdump` to inspect raw disk contents
- Use `gdb --args ./wfs ...` for debugging

## 📜 License

MIT
