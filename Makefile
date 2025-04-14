BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`


.PHONY: all
all: $(BINS)
	./umount.sh mnt
	mkdir -p mnt
	echo "1..."
	mkdir -p /tmp/$(whoami)
	echo "2..."
	truncate -s 1M /tmp/$(whoami)/test-disk1
	echo "3..."
	truncate -s 1M /tmp/$(whoami)/test-disk2
	echo "4..."
	./mkfs -r 1 -d /tmp/$(whoami)/test-disk1 -d /tmp/$(whoami)/test-disk2 -i 32 -b 200
	echo "5..."
	./wfs /tmp/$(whoami)/test-disk1 /tmp/$(whoami)/test-disk2 -s mnt



wfs:
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
mkfs: mkfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c

.PHONY: clean
clean:
	rm -rf $(BINS)

r: wfs.c mkfs
	rm -rf $(BINS)
	$(CC) $(CFLAGS) -o mkfs mkfs.c
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
	./umount.sh mnt
	echo "1..."
	mkdir -p /tmp/$(whoami)
	echo "2..."
	truncate -s 1M /tmp/$(whoami)/test-disk1
	echo "3..."
	truncate -s 1M /tmp/$(whoami)/test-disk2
	echo "4..."
	./mkfs -r 1 -d /tmp/$(whoami)/test-disk1 -d /tmp/$(whoami)/test-disk2 -i 32 -b 200
	echo "5..."
	./wfs /tmp/$(whoami)/test-disk1 /tmp/$(whoami)/test-disk2 -s -d mnt

m: wfs.c mkfs
	rm -rf $(BINS)
	$(CC) $(CFLAGS) -o mkfs mkfs.c
	$(CC) $(CFLAGS) wfs.c $(FUSE_CFLAGS) -o wfs
	echo "1..."
	mkdir -p /tmp/$(whoami)
	echo "2..."
	truncate -s 1M /tmp/$(whoami)/test-disk1
	echo "3..."
	truncate -s 1M /tmp/$(whoami)/test-disk2
	echo "4..."
	./mkfs -r 1 -d /tmp/$(whoami)/test-disk1 -d /tmp/$(whoami)/test-disk2 -i 32 -b 200
	echo "5..."
	./wfs /tmp/$(whoami)/test-disk1 /tmp/$(whoami)/test-disk2 -s -d mnt

t1:
	mkdir -p /tmp/$(whoami); truncate -s 1M /tmp/$(whoami)/test-disk1; truncate -s 1M /tmp/$(whoami)/test-disk2; ../solution/mkfs -r 1 -d /tmp/$(whoami)/test-disk1 -d /tmp/$(whoami)/test-disk2 -i 32 -b 224
	./wfs-check-metadata2.py --mode mkfs --inodes 32 --blocks 224 --disks /tmp/$(whoami)/test-disk1 /tmp/$(whoami)/test-disk2

u:
	./create_disk.sh
	$(CC) $(CFLAGS) -o utils wfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c
	./mkfs -r 1 -d test-disk1.img -d test-disk2.img -i 32 -b 224
	./utils > output.txt
	./utils

x:
	./create_disk.sh
	$(CC) $(CFLAGS) -o utils wfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c
	./mkfs -r 0 -d test-disk1.img -d test-disk2.img -i 32 -b 224
	# ./utils > output.txt
	./utils

x3:
	./create_disk.sh
	$(CC) $(CFLAGS) -o utils wfs.c
	$(CC) $(CFLAGS) -o mkfs mkfs.c
	./mkfs -r 0 -d test-disk1.img -d test-disk2.img -d test-disk3.img -i 32 -b 224
	# ./utils > output.txt
	./utils