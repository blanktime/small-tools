#!/bin/bash

# source path
source_file="/home/admin/Desktop/256M.DAT"

# storge path
usb_mount_point="/media/admin/U"

# check if storage mount
if [ ! -d "$usb_mount_point" ]; then
    echo "can not find storage!"
    exit 1
fi

# loop
while true; do
    # get storage space
    usb_available_space=$(df -B1 --output=avail "$usb_mount_point" | tail -n 1)

    # get copy file size
    source_file_size=$(stat -c %s "$source_file")

    # if not space to use, then delete earilest files
    while [ "$usb_available_space" -lt "$source_file_size" ]; do
        oldest_file=$(ls -1t "$usb_mount_point" | tail -n 1)
        rm "$usb_mount_point/$oldest_file"
        echo "no more space and delete $oldest_file"
        usb_available_space=$(df -B1 --output=avail "$usb_mount_point" | tail -n 1)
    done

     # add time stamp
    timestamp=$(date +"%Y%m%d%H%M%S")
    target_file="$usb_mount_point/file_${timestamp}.dat"

    cp "$source_file" "$target_file"
    echo "copy $source_file to $target_file"
    
	sync
done

