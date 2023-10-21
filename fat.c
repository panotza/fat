#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

typedef uint8_t bool;
#define true 1
#define false 0

#define return_defer(value) \
    do                      \
    {                       \
        result = (value);   \
        goto defer;         \
    } while (0)

typedef struct
{
    uint8_t BootJumpInstruction[3]; // The first three bytes EB 3C 90 disassemble to JMP SHORT 3C NOP
    uint8_t OemIdentifier[8];       // OEM identifier. The first 8 Bytes (3 - 10) is the version of DOS being used. The next eight Bytes 29 3A 63 7E 2D 49 48 and 43 read out the name of the version.
    uint16_t BytesPerSector;        // The number of Bytes per sector (remember, all numbers are in the little-endian format).
    uint8_t SectorsPerCluster;      // Number of sectors per cluster.
    uint16_t RevervedSectors;       // Number of reserved sectors. The boot record sectors are included in this value.
    uint8_t FatCount;               // Number of File Allocation Tables (FAT's) on the storage media. Often this value is 2.
    uint16_t DirEntryCount;         // Number of root directory entries (must be set so that the root directory occupies entire sectors).
    uint16_t TotalSectors;          // The total sectors in the logical volume. If this value is 0, it means there are more than 65535 sectors in the volume, and the actual count is stored in the Large Sector Count entry at 0x20.
    uint8_t MediaDescriptorType;    // This Byte indicates the media descriptor type.
    uint16_t SectorsPerFat;         // Number of sectors per FAT. FAT12/FAT16 only.
    uint16_t SectorsPerTrack;       // Number of sectors per track.
    uint16_t Heads;                 // Number of heads or sides on the storage media.
    uint32_t HiddenSectors;         // Number of hidden sectors. (i.e. the LBA of the beginning of the partition.)
    uint32_t LargSectorCount;       // Large sector count. This field is set if there are more than 65535 sectors in the volume, resulting in a value which does not fit in the Number of Sectors entry at 0x13.
} __attribute__((packed)) BootRecord;

typedef struct
{
    uint8_t Filename[8 + 3]; // 3 is extension
    uint8_t Attributes;
    uint8_t _Reserved;
    uint8_t CreationTimeTenths;
    uint16_t CreationTime;
    uint16_t CreationDate;
    uint16_t LastAccessDate;
    uint16_t FirstClusterHigh;
    uint16_t LastWriteTime;
    uint16_t LastWriteDate;
    uint16_t FirstClusterLow;
    uint32_t FileSize; // File Size (in bytes)
} __attribute__((packed)) DirectoryEntry;

BootRecord g_boot_record;
uint8_t *g_fat = NULL;
uint32_t g_root_directory_end;
DirectoryEntry *g_root_directory = NULL;

void prettyPrintBootRecord(BootRecord *br)
{
    printf("BootJumpInstruction:");
    for (size_t i = 0; i < sizeof(br->BootJumpInstruction) / sizeof(br->BootJumpInstruction[0]); i++)
    {
        printf(" %02X", br->BootJumpInstruction[i]);
    }
    printf("\n");
    printf("OemIdentifier:");
    for (size_t i = 0; i < sizeof(br->OemIdentifier) / sizeof(br->OemIdentifier[0]); i++)
    {
        printf(" %02X", br->OemIdentifier[i]);
    }
    printf("\n");
    printf("BytesPerSector: %d\n", br->BytesPerSector);
    printf("SectorsPerCluster: %d\n", br->SectorsPerCluster);
    printf("RevervedSectors: %d\n", br->RevervedSectors);
    printf("FatCount: %d\n", br->FatCount);
    printf("DirEntryCount: %d\n", br->DirEntryCount);
    printf("TotalSectors: %d\n", br->TotalSectors);
    printf("MediaDescriptorType: %d\n", br->MediaDescriptorType);
    printf("SectorsPerFat: %d\n", br->SectorsPerFat);
    printf("SectorsPerTrack: %d\n", br->SectorsPerTrack);
    printf("Heads: %d\n", br->Heads);
    printf("HiddenSectors: %d\n", br->HiddenSectors);
    printf("LargSectorCount: %d\n", br->LargSectorCount);
}

void prettyPrintDirectoryEntry(DirectoryEntry *e)
{
    printf("======================\n");
    printf("Filename:");
    for (size_t i = 0; i < sizeof(e->Filename) / sizeof(e->Filename[0]); i++)
    {
        putc(e->Filename[i], stdout);
    }
    printf("\n");
    printf("    Attributes: %d\n", e->Attributes);
    printf("    _Reserved: %d\n", e->_Reserved);
    printf("    CreationTimeTenths: %d\n", e->CreationTimeTenths);
    printf("    CreationTime: %d\n", e->CreationTime);
    printf("    CreationDate: %d\n", e->CreationDate);
    printf("    LastAccessDate: %d\n", e->LastAccessDate);
    printf("    FirstClusterHigh: %d\n", e->FirstClusterHigh);
    printf("    LastWriteTime: %d\n", e->LastWriteTime);
    printf("    LastWriteDate: %d\n", e->LastWriteDate);
    printf("    FirstClusterLow: %d\n", e->FirstClusterLow);
    printf("    FileSize: %d\n", e->FileSize);
}

bool readBootRecord(BootRecord *out, FILE *disk)
{
    return fread(out, sizeof(BootRecord), 1, disk) > 0;
}

bool readSector(void *out, FILE *disk, uint32_t lba, uint32_t count)
{
    printf("seek: 0x%02X\n", lba * g_boot_record.BytesPerSector);
    printf("read: bps=%d; count=%d\n", g_boot_record.BytesPerSector, count);
    bool ok = true;
    ok = ok && (fseek(disk, lba * g_boot_record.BytesPerSector, SEEK_SET) == 0);
    ok = ok && (fread(out, g_boot_record.BytesPerSector, count, disk) == count);
    return ok;
}

bool readFat(FILE *disk)
{
    g_fat = (uint8_t *)malloc(g_boot_record.SectorsPerFat * g_boot_record.BytesPerSector);
    return readSector(g_fat, disk, g_boot_record.RevervedSectors, g_boot_record.SectorsPerFat);
}

bool readRootDirectory(FILE *disk)
{
    uint32_t lba = g_boot_record.RevervedSectors + g_boot_record.SectorsPerFat * g_boot_record.FatCount;
    uint32_t size = sizeof(DirectoryEntry) * g_boot_record.DirEntryCount;
    uint32_t sectors = (size / g_boot_record.BytesPerSector);
    if (size % g_boot_record.BytesPerSector > 0)
        sectors++;

    g_root_directory_end = lba + sectors;
    g_root_directory = (DirectoryEntry *)malloc(sectors * g_boot_record.BytesPerSector);
    return readSector(g_root_directory, disk, lba, sectors);
}

DirectoryEntry *findFile(const char *filename)
{
    for (uint32_t i = 0; i < g_boot_record.DirEntryCount; i++)
    {
        if (strlen(filename) > 11)
            continue;

        if (memcmp(filename, g_root_directory[i].Filename, 11) == 0)
            return &g_root_directory[i];
    }
    return NULL;
}

bool readFile(uint8_t *buf, FILE *disk, DirectoryEntry *x)
{
    printf("=== Reading ===\n");

    bool ok = true;
    uint16_t currentCluster = x->FirstClusterLow;
    do
    {
        uint32_t lba = g_root_directory_end + (currentCluster - 2) * g_boot_record.SectorsPerCluster;
        printf("currentCluster: %d\n", currentCluster);
        printf("lba: %d\n", lba);

        ok = ok && readSector(buf, disk, lba, g_boot_record.SectorsPerCluster);
        buf += g_boot_record.SectorsPerCluster * g_boot_record.BytesPerSector;

        uint32_t fatIndex = currentCluster * 3 / 2;
        if (currentCluster % 2 == 0)
            currentCluster = *(uint16_t *)(g_fat + fatIndex) & 0x0FFF;
        else
            currentCluster = *(uint16_t *)(g_fat + fatIndex) >> 4;
    } while (ok && currentCluster < 0x0FF8);
    return ok;
}

int main(int argc, char **argv)
{
    int result = 0;
    FILE *disk = NULL;
    uint8_t *buf = NULL;

    if (argc < 3)
    {
        printf("Syntax: %s <disk image> <file name>\n", argv[0]);
        return_defer(2);
    }

    disk = fopen(argv[1], "rb");
    if (!disk)
    {
        fprintf(stderr, "Cannot open disk image %s\n", argv[1]);
        return_defer(1);
    }

    if (!readBootRecord(&g_boot_record, disk))
    {
        fprintf(stderr, "Cannot read boot record\n");
        return_defer(1);
    }

    prettyPrintBootRecord(&g_boot_record);

    if (!readFat(disk))
    {
        fprintf(stderr, "Cannot read FAT\n");
        return_defer(1);
    }

    if (!readRootDirectory(disk))
    {
        fprintf(stderr, "Cannot read root directory\n");
        return_defer(1);
    }

    DirectoryEntry *f = findFile(argv[2]);
    if (!f)
    {
        fprintf(stderr, "File \"%s\" not found\n", argv[2]);
        return_defer(1);
    }

    prettyPrintDirectoryEntry(f);

    buf = (uint8_t *)malloc(f->FileSize + g_boot_record.BytesPerSector);
    if (!readFile(buf, disk, f))
    {
        fprintf(stderr, "Read file error\n");
        return_defer(1);
    }

    printf("===content===\n");
    for (size_t i = 0; i < f->FileSize; i++)
    {
        if (isprint(buf[i]))
            fputc(buf[i], stdout);
        else
            printf("<%02x>", buf[i]);
    }
    printf("\n");

defer:
    if (buf)
        free(buf);
    free(g_fat);
    free(g_root_directory);
    if (disk)
        fclose(disk);
    return result;
}