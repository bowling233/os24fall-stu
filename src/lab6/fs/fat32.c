#include "fat32.h"
#include "printk.h"
#include "virtio.h"
#include "string.h"
#include "mbr.h"
#include "mm.h"

struct fat32_bpb fat32_header;
struct fat32_volume fat32_volume;

uint8_t fat32_buf[VIRTIO_BLK_SECTOR_SIZE];
uint8_t fat32_table_buf[VIRTIO_BLK_SECTOR_SIZE];

uint64_t cluster_to_sector(uint64_t cluster)
{
    return (cluster - 2) * fat32_volume.sec_per_cluster + fat32_volume.first_data_sec;
}

uint64_t sector_to_cluster(uint64_t sector)
{
    return (sector - fat32_volume.first_data_sec) / fat32_volume.sec_per_cluster + 2;
}

uint32_t next_cluster(uint64_t cluster)
{
    uint64_t fat_offset = cluster * 4;
    uint64_t fat_sector = fat32_volume.first_fat_sec + fat_offset / VIRTIO_BLK_SECTOR_SIZE;
    virtio_blk_read_sector(fat_sector, fat32_table_buf);
    int index_in_sector = fat_offset % (VIRTIO_BLK_SECTOR_SIZE / sizeof(uint32_t));
    return *(uint32_t *)(fat32_table_buf + index_in_sector);
}

void fat32_init(uint64_t lba, uint64_t size)
{
    // 从第 lba 个扇区读取 FAT32 BPB
    virtio_blk_read_sector(lba, (void *)&fat32_header);
    // 记录第一个 FAT 表所在的扇区号
    // 即 lba 扇区为 BPB，这之后紧接着的是一些保留扇区，然后是第一个 FAT 表所在的扇区（这个表会占很多个扇区，需要从 bpb 读取），接下来是第二个 FAT 表所在的扇区（为第一个 FAT 表的备份），然后接下来是数据区。
    fat32_volume.first_fat_sec = lba + fat32_header.rsvd_sec_cnt;
    // 每个簇的扇区数
    fat32_volume.sec_per_cluster = fat32_header.sec_per_clus;
    // 记录第一个数据簇所在的扇区号
    fat32_volume.first_data_sec = fat32_volume.first_fat_sec + fat32_header.num_fats * fat32_header.fat_sz32;
    Log("first_data_sec: %d", fat32_volume.first_data_sec);
    // 记录每个 FAT 表占的扇区数（并未用到）
    fat32_volume.fat_sz = fat32_header.fat_sz32;

    // check if FAT volume is valid
    // 你可以尝试从 fat32_volume.first_fat_sec 读取一个扇区到 fat32_buf 中，如果正确的话，开头四个字节应该是 F8FFFF0F。
    virtio_blk_read_sector(fat32_volume.first_fat_sec, fat32_buf);
    if (*(uint32_t *)fat32_buf != 0x0fFFFFf8)
    {
        Err("fat32_init: FAT volume is invalid\n");
    }
}

int is_fat32(uint64_t lba)
{
    virtio_blk_read_sector(lba, (void *)&fat32_header);
    if (fat32_header.boot_sector_signature != 0xaa55)
    {
        return 0;
    }
    return 1;
}

int next_slash(const char *path)
{ // util function to be used in fat32_open_file
    int i = 0;
    while (path[i] != '\0' && path[i] != '/')
    {
        i++;
    }
    if (path[i] == '\0')
    {
        return -1;
    }
    return i;
}

void to_upper_case(char *str)
{ // util function to be used in fat32_open_file
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (str[i] >= 'a' && str[i] <= 'z')
        {
            str[i] -= 32;
        }
    }
}

struct fat32_file fat32_open_file(const char *path)
{
    Log("%s", path);
    struct fat32_file file = {.cluster = 0, .dir = {.cluster = 0, .index = 0}};
    /* open the file according to path */
    // 我们需要读取出被打开的文件所在的簇和目录项位置的信息，来供后面 read write lseek 使用，目标是获取到
    // 你需要遍历数据区开头的根目录扇区，找到 name 和 path 末尾的 filename 相匹配的 fat32_dir_entry 目录项结构体，再从其中得到这些信息。
    // 如果是使用 memcmp 来逐一比较 FAT32 文件系统根目录下的各个文件名和想要打开的文件名，则需要 8 个字节完全匹配。但是需要注意的是，FAT32 文件系统根目录下的文件名并不是以 "\0" 结尾的字符串
    // 此外需要注意的是，需要将文件名统一转换为大写或小写，因为我们的实现是不区分大小写的。

    char true_name[12];
    memset(true_name, 0, 12);
    // split path to file
    strcpy(true_name, path + 1 + next_slash(path + 1) + 1);
    to_upper_case(true_name);
    Log("true_name: %s", true_name);

    struct fat32_dir_entry *dir_entry = (struct fat32_dir_entry *)fat32_buf;
    uint64_t root_dir_sec = fat32_volume.first_data_sec;

    for (uint64_t i = 0; true; i += 1)
    {
        virtio_blk_read_sector(root_dir_sec + i, fat32_buf);
        for (int j = 0; j < FAT32_ENTRY_PER_SECTOR; j++)
        {
            // 6.1 DIR_Name[0] = 0x00 also indicates the directory entry is free (available). However, DIR_Name[0] = 0x00 is an additional indicator that all directory entries following the current free entry are also free.
            if (dir_entry[j].name[0] == 0x00)
            {
                goto end;
            }
            // 6.1 DIR_Name[0]3 = 0xE5 indicates the directory entry is free (available).
            if (dir_entry[j].name[0] == 0xe5)
            {
                continue;
            }
            char name[12];
            memset(name, 0, 12);
            // Each of the above two components are “trailing space padded” if required (using value: 0x20).
            for (int k = 0; k < 8; k++)
            {
                name[k] = dir_entry[j].name[k] == ' ' ? '\0' : dir_entry[j].name[k];
            }
            Log("name: %s", name);
            // Ignore extension
            /*
            name[8] = '.';
            for (int k = 0; k < 3; k++)
            {
                name[k + 9] = dir_entry[j].ext[k] == ' ' ? '\0' : dir_entry[j].ext[k];
            }
            */
            to_upper_case(name);
            Log("result %d", strcmp(name, true_name));
            if (strcmp(name, true_name) == 0)
            {
                Log("file found");
                file.cluster = dir_entry[j].startlow;
                file.dir = (struct fat32_dir){.cluster = sector_to_cluster(root_dir_sec + i), .index = j};
                return file;
            }
        }
    }
end:
    return file;
}

int64_t fat32_lseek(struct file *file, int64_t offset, uint64_t whence)
{
    if (whence == SEEK_SET)
    { // The file offset is set to offset bytes.
        file->cfo = offset;
    }
    else if (whence == SEEK_CUR)
    {
        // The file offset is set to its current location plus offset bytes.
        file->cfo = file->cfo + offset;
    }
    else if (whence == SEEK_END)
    {
        // The file offset is set to the size of the file plus offset bytes.
        struct fat32_dir_entry* dir_entries = (struct fat32_dir_entry*)fat32_buf;
        virtio_blk_read_sector(cluster_to_sector(file->fat32_file.dir.cluster), fat32_buf);
        file->cfo = dir_entries[file->fat32_file.dir.index].size + offset;
    }
    else
    {
        printk("fat32_lseek: whence not implemented\n");
        while (1)
            ;
    }
    return file->cfo;
}

uint64_t fat32_table_sector_of_cluster(uint32_t cluster)
{
    return fat32_volume.first_fat_sec + cluster / (VIRTIO_BLK_SECTOR_SIZE / sizeof(uint32_t));
}

int64_t fat32_read(struct file *file, void *buf, uint64_t len)
{
    Log("file->cfo = %d, len = %d", file->cfo, len);
    /* read content to buf, and return read length */
    struct fat32_file *fat32_file = &(file->fat32_file);
    uint64_t cluster = fat32_file->cluster;
    uint64_t cfo = file->cfo;
    uint64_t read_len = 0;

    // Get the directory entry to determine the file size
    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry *)fat32_buf;
    virtio_blk_read_sector(cluster_to_sector(fat32_file->dir.cluster), fat32_buf);
    uint64_t file_size = dir_entries[fat32_file->dir.index].size;

    // Adjust len if it exceeds the file size
    if (cfo + len > file_size)
    {
        len = file_size - cfo;
    }

    while (cfo >= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE)
    {
        cluster = next_cluster(cluster);
        cfo -= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE;
    }

    while (read_len < len)
    {
        uint64_t sector = cluster_to_sector(cluster);
        virtio_blk_read_sector(sector, fat32_buf);
        uint64_t offset = cfo % (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE);
        uint64_t to_read = (len - read_len) < (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE - offset) ? (len - read_len) : (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE - offset);
        memcpy(buf + read_len, fat32_buf + offset, to_read);
        read_len += to_read;
        cfo += to_read;
        if (cfo >= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE)
        {
            cluster = next_cluster(cluster);
            cfo -= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE;
        }
    }

    file->cfo += read_len;
    return read_len;
}

int64_t fat32_write(struct file *file, const void *buf, uint64_t len)
{
    Log("file->cfo = %d, len = %d", file->cfo, len);
    /* write content to file, and return written length */
    struct fat32_file *fat32_file = &(file->fat32_file);
    uint64_t cluster = fat32_file->cluster;
    uint64_t cfo = file->cfo;
    uint64_t write_len = 0;

    // Get the directory entry to determine the file size
    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry *)fat32_buf;
    virtio_blk_read_sector(cluster_to_sector(fat32_file->dir.cluster), fat32_buf);
    uint64_t file_size = dir_entries[fat32_file->dir.index].size;

    // Adjust len if it exceeds the file size
    if (cfo + len > file_size)
    {
        len = file_size - cfo;
    }

    while (cfo >= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE)
    {
        cluster = next_cluster(cluster);
        cfo -= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE;
    }

    while (write_len < len)
    {
        uint64_t sector = cluster_to_sector(cluster);
        virtio_blk_read_sector(sector, fat32_buf);
        uint64_t offset = cfo % (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE);
        uint64_t to_write = (len - write_len) < (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE - offset) ? (len - write_len) : (fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE - offset);
        memcpy(fat32_buf + offset, buf + write_len, to_write);
        virtio_blk_write_sector(sector, fat32_buf);
        write_len += to_write;
        cfo += to_write;
        if (cfo >= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE)
        {
            cluster = next_cluster(cluster);
            cfo -= fat32_volume.sec_per_cluster * VIRTIO_BLK_SECTOR_SIZE;
        }
    }

    file->cfo += write_len;
    return write_len;
}