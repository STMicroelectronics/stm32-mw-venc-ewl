# STM32N6 VENC Example

For any additional information, please contact [Guillaume JANET](mailto:guillaume.janet@st.com) and CC [Jean-Christophe BATLLO](mailto:jean-christophe.batllo@st.com) and [Patrice LE FLOCH](mailto:patrice.lefloch@st.com).

This IAR project contains a working example of h264 video encoding on the STM32N6 Python MCU.

## Requirements

This example is meant to run on the STM32N657 discovery board.

It uses either thethe external NOR flash or an SD card to store the output stream.

It uses the MB1723 camera module for input.

The external flash can be programmed and read using STM32Cube Programmer with the relevent external memory loader.<br>
See [STM32N6 Teams Wiki](https://teams.microsoft.com/l/entity/com.microsoft.teamspace.tab.wiki/tab::614b9789-c8d3-46df-87d9-50ce4ede236a?context=%7B%22subEntityId%22%3A%22%7B%5C%22pageId%5C%22%3A2%2C%5C%22sectionId%5C%22%3A7%2C%5C%22origin%5C%22%3A2%7D%22%2C%22channelId%22%3A%2219%3AJHgEoj38GgxGYuvTAxhEYIwbGLb9TPpHIlBKinKTcNg1%40thread.tacv2%22%7D&tenantId=75e027c9-20d5-47d5-b82f-77d7cd041e8f) to learn how to install the external loader and Cube Programmer patch and program the external flash.

for the SD card, its content can be read using the `dd` command in git bash. example : `dd of=dump.bin if=[path to sd e.g. /dev/sdd1] ibs=512 obs=512 count=66000` to dump 66000 blocks of 512 bytes from an sd.

To read back encoded video, it can be converted from raw bytestream to mp4 using a tool called [ffmpeg](https://trac.ffmpeg.org/).


### Required changes to the BSP
For the example to work, modifications to the BSP have to be made :

 - The OV5640 camera driver should be checked out to tag mp2/v0.1.0
 - in the camera BSP : OV5640_SetMipiVirtualChannel, OV5640_Start, OV5640_Stop and OV5640_EnableModeMIPI functions should be commented or deleted
 - in the bsp conf file, USE_BSP_COM_FEATURE, USE_COM_LOG and USE_OV5640_SENSOR should be set to 1. USE_RAM_MEMORY_APS256XX should be set to 0
 - in the SD card BSP, replace SD_AF11_SDMMC1 with SD_AF11_SDMMC2

## How to use

To use the example and encode a video, follow these steps :
 - Run the example (check logs to know when encoding is done)
 - save the encoded byte stream size given in logs
 - Use Cube Programmer to read back the external flash for the stream size (0x7000 0000 in Cube Programmer) or dump the SD card contents
 - Save the memory as a file
 - Use the following ffmpeg command to convert the raw byte stream file to mp4 : `ffmpeg -f h264 -i [input file] -c copy [output file]`
 - read the output file using VLC or another video player

 ## Package Contents

### Firmware

 Latest complete STM32N6 Firmware package at the time of package creation. Includes LL, HAL, CMSIS, BSP etc..

### VSI_Stack

Full Verisilicom stack compatible with the N6 hardware configuration :
- h264 encoding
- jpeg encoding
- RGB to YUV conversion
- no VP8 encoding
- no video stabilization

### Utils

Contains the memory managemer used for pool-based buffer allocation.
Adapted for standalone use from FreeRTOS heap_4.c by WBA team.

### EWL

Encoder Wrapper Layer used to make the VSI stack compatible with STM32N6 hardware.<br>
EWLInit allocates a pool in heap. its size can be changed using the `EWL_DEFAULT_POOL_SIZE` macro in *ewl_impl.c* .
In this version, no concurrent access is permitted. Init can only be used to create one instance.

## Known limitations :
- External RAM not currently supported : Limits the supported resolutions
- Allocated memory usage outside the stack is currently not optimized
- Current implementation of EWLWaitHwReady is polling. Will hog CPU usage until async implementation with passive wait is done
- No review/quality process done on this delivery : please report issues and bugs to the author (see top of the readme)