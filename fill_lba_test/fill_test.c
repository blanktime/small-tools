#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <time.h>

// #define CLEAR_LINE "\033[K"
//  #define patten_size 65536 // 64K
#define patten_size 1048576 // 1M
#define block_size 512

int write_data(const char *device_path, int percentage);
int verify_data(const char *device_path, int percentage);

int main(int argc, char **argv)
{
    // const char *device_path = "/dev/sdb";
    char *path = "/dev/sdb";
    // unsigned char data_value = 0x5a;
    int percentage = 90;

    if (argc != 3)
    {
        printf("command format: sudo ./fill_test [path] [percentage(0-100)]\n");
        return 1;
    }
    printf("argc: %d, argv: %s\n", argc, *argv);
    path = argv[1];
    printf("path: %s\n", path);

    if (atoi(argv[2]) == 0)
    {
        return 0;
    }
    else
    {
        percentage = atoi(argv[2]);
        printf("percentage: %d\n", percentage);
    }

    int write_result = write_data(path, percentage);

    if (write_result == 0)
    {
        printf("\nWrite all data success!\n");
        int verify_result = verify_data(path, percentage);

        if (verify_result == 0)
        {
            printf("\nAll data verify success!\n");
        }
        else
        {
            printf("\nData verify fail\n");
        }
    }
    else
    {
        printf("\nWrite all data fail!\n");
    }

    return 0;
}

void create_pattern(unsigned char *pattern, unsigned long lba)
{
    pattern[0] = (lba >> 24) & 0XFF;
    pattern[1] = (lba >> 16) & 0XFF;
    pattern[2] = (lba >> 8) & 0XFF;
    pattern[3] = lba & 0XFF;
}

void fill_pattern(unsigned char *data, unsigned char y1, unsigned char y2, unsigned char y3,
                  unsigned char y4, int count)
{
    int i;
    unsigned char tab[4] = {y1, y2, y3, y4};

    for (i = 0; i < count; i++)
    {
        data[i] = tab[i & 0x03];
    }
}

// write
int write_data(const char *device_path, int percentage)
{
    int fd = open(device_path, O_RDWR | O_SYNC);

    if (fd == -1)
    {
        perror("Open device error");
        return -1;
    }
    else
    {
        printf("Open device success!\n");
    }

    off_t device_size = lseek(fd, 0, SEEK_END);
    off_t write_size = (device_size * percentage) / 100;

    unsigned char *buffer = (unsigned char *)malloc(patten_size);
    if (buffer == NULL)
    {
        perror("Failed to allocate memory");
        close(fd);
        return -1;
    }
    else
    {
        printf("Allocate memory success!\n");
    }

    // memset(buffer, value, patten_size);

    // seek start
    lseek(fd, 0, SEEK_SET);

    unsigned char pattern[4];
    clock_t start_write_time = time(NULL); // record start time
    for (off_t i = 0; i < write_size; i += patten_size)
    {
        for (off_t j = i; j < i + patten_size; j += block_size)
        {
            create_pattern(pattern, j / block_size);
            fill_pattern(buffer + j - i, pattern[0], pattern[1], pattern[2], pattern[3], block_size);
            // memset(buffer + j - i, j / block_size, block_size);
            //  printf("j = %d, buffer: %p\n", j, buffer);
        }
        if (write(fd, buffer, patten_size) == -1)
        {
            perror("\nWrite data error");
            printf("\rlba is %ld", i / block_size);
            free(buffer);
            close(fd);
            return -1;
        }
        // else
        // {
        //     printf("Write data success at lba %ld!\n", i / block_size);
        // }
        double elapsed_time = difftime(time(NULL), start_write_time);
        printf("\rwrite progress: %ld%%, running time: %.1fs\r", (i * 100) / write_size, elapsed_time);
        fflush(stdout);
    }
    printf("\rwrite progress: 100%%,");
    // printf("\r%s\r", CLEAR_LINE);
    // fflush(stdout);
    free(buffer);
    close(fd);

    return 0;
}

int verify_pattern(unsigned char *data, unsigned char y1, unsigned char y2, unsigned char y3,
                   unsigned char y4, int count)
{
    int i;
    unsigned char tab[4] = {y1, y2, y3, y4};

    for (i = 0; i < count; i++)
    {
        if (data[i] != tab[i & 0x03])
        {
            return 1;
        }
    }

    return 0;
}

// verify
int verify_data(const char *device_path, int percentage)
{
    int fd = open(device_path, O_RDONLY | O_SYNC);
    int ret = 0;

    if (fd == -1)
    {
        perror("Open device error");
        return -1;
    }
    else
    {
        printf("Open device success!\n");
    }

    unsigned char *buffer = (unsigned char *)malloc(patten_size);

    if (buffer == NULL)
    {
        perror("Failed to allocate memory");
        close(fd);
        return -1;
    }
    else
    {
        printf("Allocate memory success!\n");
    }

    // fill size
    off_t device_size = lseek(fd, 0, SEEK_END);
    off_t verify_size = (device_size * percentage) / 100;

    lseek(fd, 0, SEEK_SET);

    int error_lba = 0;

    unsigned char pattern[4];
    clock_t start_verify_time = time(NULL); // record start time
    for (off_t i = 0; i < verify_size; i += patten_size)
    {
        if (read(fd, buffer, patten_size) == 0)
        {
            perror("\nRead data error");
            free(buffer);
            close(fd);
            return -1;
        }
        // else{
        //     printf("Read data success, lba is %ld!\n", i / block_size);
        // }

        for (off_t j = i; j < patten_size + i; j += block_size)
        {
            create_pattern(pattern, j / block_size);
            ret = verify_pattern(buffer + j - i, pattern[0], pattern[1], pattern[2], pattern[3], block_size);
            if (ret)
            {
                printf("Verify fail - lba 0x%lx, offset %ld\n", j / block_size, j % block_size);
            }
            // if (buffer[j - i] != (j / block_size))
            // {
            //     error_lba = i / block_size;
            //     printf("\nVerify fail, lba is %ld, offset is %ld!\n", j / block_size, j % block_size);
            //     break;
            // }
            // else
            // {
            //     printf("Verify success at %ld\n", i / block_size);
            // }
        }

        // update progress bar
        double elapsed_time = difftime(time(NULL), start_verify_time);
        printf("\rverify progress: %ld%%, running time: %.1fs\r", (i * 100) / verify_size, elapsed_time);
        fflush(stdout);
    }
    printf("\rverify progress: 100%%,");
    // clear progress bar
    // printf("\r%s\r", CLEAR_LINE);
    // fflush(stdout);

    free(buffer);
    close(fd);

    return error_lba;
}
