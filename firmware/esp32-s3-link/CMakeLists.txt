cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(rx3_link_bridge)

fatfs_create_spiflash_image(rx3disk msc-root FLASH_IN_PROJECT)
