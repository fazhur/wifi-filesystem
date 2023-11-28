#include <iostream>
#include <cstdio>
#include <vector>
#include <thread>
#include <string>
#include <map>
#include <filesystem>
#include <netinet/in.h>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#define INVALID_SOCKET (-1)

namespace fs = std::filesystem;


struct PARTITION_ENTRY
{
    uint8_t state = 0x0;
    uint8_t beginHead = 0;
    unsigned short beginSector = 6;
    unsigned short beginCylinder = 10;
    uint8_t type = 0x00;
    uint8_t endHead = 0;
    unsigned short endSector = 6;
    unsigned short endCylinder = 10;
    unsigned int sectorsBetweenMBRAndPartition = 0;
    unsigned int sectorsCount = 0;
};

struct MBR
{
    uint8_t executableCode[446] = {};
    PARTITION_ENTRY partitionEntry1 = {};
    PARTITION_ENTRY partitionEntry2 = {};
    PARTITION_ENTRY partitionEntry3 = {};
    PARTITION_ENTRY partitionEntry4 = {};
    uint8_t bootRecordSignature[2] = { 0x55, 0xAA };
};

struct FAT32
{
    uint8_t jumpCode[3] = { 235, 88, 144 };
    char oemName[8] = { 'M', 'S', 'D', 'O', 'S', '5', '.', '0'};
    unsigned short bytesPerSector = 512;
    uint8_t sectorsPerCluster = 0;
    unsigned short reservedSectors = 1;
    uint8_t numberCopiesFat = 1;
    unsigned short maximumRootDirectoryEnties = 0;
    unsigned short numberOfSectorsFat16Small = 0;
    uint8_t mediaDescriptor = 0xF8;
    unsigned short sectorsPerFat16 = 0;
    unsigned short sectorsPerTrack = 1;
    unsigned short numberOfHeads = 1;
    unsigned int numberOfHiddenSectors = 0;
    unsigned int numberOfSectorsInPartition = 0;
    unsigned int numberOfSectorsPerFat = 0;
    unsigned short flags = 0;
    unsigned short version = 0;
    unsigned int clusterNumberOfRootDir = 0;
    unsigned short sectorNumberFSInformation = 0;
    unsigned short sectorNumberBackupBoot = 0;
    uint8_t reserved[12] = {};
    uint8_t logicalDriveNumberPartition = 0;
    uint8_t unused = 0;
    uint8_t extendedSignature = 0x29;
    unsigned int serialNumber = 0x12345678;
    char volumeName[11] = { 'V', 'I', 'R', 'T', 'U', 'A', 'L', ' ', 'F', 'A', 'T'};
    char filSysType[8] = { 'F', 'A', 'T', '3', '2', ' ', ' ', ' '};
    uint8_t executableCode[420] = {};
    uint8_t bootRecordSignature[2] = { 0x55, 0xAA };
};

struct FAT32_INFORMATION
{
    unsigned int firstSignature = 0x52526141;
    uint8_t unknown[480] = {};
    unsigned int fsInfoSignature = 0x72724161;
    unsigned int numberOfFreeClusters = 0;
    unsigned int numberMostRecentlyAllocated = 0;
    uint8_t reserved[12] = {};
    uint8_t unknown2[2] = {};
    uint8_t bootRecordSignature[2] = { 0x55, 0xAA };
};

struct FAT32_FILEDATE
{
    unsigned short day : 5;
    unsigned short month : 4;
    unsigned short yearFrom1980 : 7;
};

struct FAT32_FILETIME
{
    unsigned short secondsHalf : 5;
    unsigned short minutes : 6;
    unsigned short hours : 5;
};

struct FAT32_DIR_ENTRY {
    char DIR_Name[11];
    uint8_t DIR_Attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTime_Tenth;
    FAT32_FILETIME DIR_CrtTime;  
    FAT32_FILEDATE DIR_CrtDate;
    FAT32_FILEDATE DIR_LstAccDate;
    unsigned short DIR_FstClusHI;
    FAT32_FILETIME DIR_WrtTime;
    FAT32_FILEDATE DIR_WrtDate;
    unsigned short DIR_FstClusLO;
    unsigned int DIR_FileSize;
} ;

const uint8_t READ_ONLY = 0x01;
const uint8_t HIDDEN = 0x02;
const uint8_t SYSTEM = 0x04;
const uint8_t VOLUME_ID = 0x08;
const uint8_t DIRECTORY = 0x10;
const uint8_t ARCHIVE = 0x20;
const uint8_t ATTR_LONG_NAME = 0x0F;

struct FAT32_LFN_ENTRY {
    uint8_t part = 0;
    wchar_t name1[5] = {};
    uint8_t attr = ATTR_LONG_NAME;
    uint8_t type = 0;
    uint8_t checksum = 0;
    wchar_t name2[6] = {};
    unsigned short fstCLusLO = 0;
    wchar_t name3[2] = {};
};
std::string format_size(int64_t number)
{
    if (number < 1024)
    {
        return std::to_string(number) + " bytes";
    }
    number /= 1024;
    if (number < 1024)
    {
        return std::to_string(number) + "K";
    }
    number /= 1024;
    if (number < 1024)
    {
        return std::to_string(number) + "MB";
    }
    number /= 1024;
    if (number < 1024)
    {
        return std::to_string(number) + "GB";
    }
    number /= 1024;
    if (number < 1024)
    {
        return std::to_string(number) + "TB";
    }
    return std::to_string(number);
}

struct SVIRTUAL_FILE
{
    std::wstring fileName;
    std::wstring filePath;
    int64_t addressBegin = 0;
    int64_t addressEnd = 0;
    int64_t fileSize = 0;
    unsigned int clusterBegin = 0;
    FILE* file_handle = nullptr;
};

struct SDIRECTORY_ENTRY
{
    fs::path full_path;
    FAT32_DIR_ENTRY entry = {};
    std::vector<FAT32_LFN_ENTRY> lfn_entries = {};
};

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "Ukrainian");

    fs::path executable_path(argv[0]);

    if (!fs::exists(Settings::content_path))
    {
        std::cout << "Error: path " << Settings::content_path << " not exist" << std::endl;
        return 1;
    }

    CFAT32 fat32image;
    fat32image.create(Settings::content_path);

    int server_fd, new_socket, valread;
    struct sockaddr_in address = {};
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        std::cout << "Error: Create socket failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(Settings::port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        std::cout << "Error: Can't bind on port " << Settings::port << std::endl;
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0)
    {
        std::cout << "Error: Can't listen on port " << Settings::port << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Server: Started on port " << Settings::port << std::endl;

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) >= 0)
    {
        std::cout << "Server: Client connected" << std::endl;

        while (true)
        {
            remote_request request = {};
            char *buffer = (char *)&request;
            int length = sizeof(request);

            bool closed = false;
            while (length > 0)
            {
                int bytes_recv = recv(new_socket, buffer, length, 0);
                if (bytes_recv <= 0)
                {
                    closed = true;
                    break;
                }
                length -= bytes_recv;
                buffer += bytes_recv;
            }
            if (closed)
            {
                break;
            }

            if (request.length == 0)
            {
                std::vector<uint8_t> answer_data = fat32image.get_init_answer();
                std::cout << "Client: Init request" << std::endl;

                if (send(new_socket, (char *)&answer_data[0], answer_data.size(), 0) != answer_data.size())
                {
                    std::cout << "Error: Cant send data to client" << std::endl;
                    closed = true;
                }
            }
            else
            {
                std::cout << "Client: Data request (addr=" << request.position << ", size=" << request.length << ")" << std::endl;
std::vector<uint8_t> buffer;
                if (request.length > 16777216)
                {
                    std::cout << "Error: request.length > 16777216" << std::endl;
                    closed = true;
                }
                buffer.resize(request.length);
                fat32image.read_data(request.position, &buffer[0], request.length);
                std::cout << buffer.data() << std::endl;
                if (send(new_socket, (char *)&buffer[0], request.length, 0) != request.length)
                {
                    std::cout << "Error: Cant send data to client" << std::endl;
                    closed = true;
                }
            }
            if (closed)
            {
                break;
            }
        }
        close(new_socket);
        std::cout << "Client: Connection closed" << std::endl;
    }

    close(server_fd);

    return 0;
}
