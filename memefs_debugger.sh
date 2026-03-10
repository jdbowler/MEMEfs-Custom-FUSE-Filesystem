#!/bin/bash

IMG="$1"
BLOCK_SIZE=512

if [[ ! -f "$IMG" ]]; then
	echo "Usage: $0 <memefs.img>"
	exit 1
fi

dump_block() {
	local block_num=$1
	local label=$2
	echo "== $label (Block $block_num) =="
	dd if="$IMG" bs=$BLOCK_SIZE skip=$block_num count=1 2>/dev/null | hexdump -C
	echo ""
}

dump_range() {
	local start=$1
	local count=$2
	local label=$3

	echo "== $label (Blocks $start to $((start + count - 1))) =="
	dd if="$IMG" bs=$BLOCK_SIZE skip=$start count=$count 2>/dev/null | hexdump -C
	echo ""
}

echo "Inspecting MEMEfs image: $IMG"
echo "Block size: $BLOCK_SIZE bytes"
echo "=================================="

dump_block 0 "Backup Superblock"
dump_block 255 "Main Superblock"
dump_block 254 "Main FAT"
dump_block 239 "Backup FAT"
dump_range 253 14 "Directory Region"
dump_range 1 18 "Reserved Blocks"
dump_range 1 220 "User Data Blocks"
