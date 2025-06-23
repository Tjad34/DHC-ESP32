cmake_minimum_required(VERSION 3.5)

# Include FAT filesystem component
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components/fatfs)

# Set the custom partition table
set(PARTITION_TABLE ${CMAKE_CURRENT_LIST_DIR}/partitions.csv)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(dhc_esp32)

# Create FAT filesystem image
# fatfs_create_partition_image(storage data FLASH_IN_PROJECT) 