#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/hdreg.h>

#define CHUNK_SIZE (4 * 1024)
#define SECTORSIZE 512
#define NUM_PATHS_TO_CHECK 5

// 填充函数
int fillPartition(int fd, size_t partitionSize)
{
	char buffer[CHUNK_SIZE];
	lseek(fd, 0, SEEK_SET);
	memset(buffer, 0x5a, CHUNK_SIZE);

	size_t remainingSize = partitionSize;
	while (remainingSize > 0)
	{
		ssize_t bytesWritten = write(fd, buffer, CHUNK_SIZE);
		if (bytesWritten == -1)
		{
			perror("write CHUNK_SIZE failed!");
			return -1;
		}
		else
		{
			printf("write CHUNK_SIZE success!\n");
		}
		remainingSize -= bytesWritten;
	}
	return 0;
}

// 顺序discard函数
int sequentialDiscard(int fd, int start, size_t partitionSize, size_t *total_discard_times)
{
	size_t numChunks = partitionSize / CHUNK_SIZE;
	for (size_t i = 0; i < numChunks; i++)
	{
		unsigned long range[2];
		range[0] = (unsigned long)(start * SECTORSIZE + i * CHUNK_SIZE);
		range[1] = (unsigned long)CHUNK_SIZE;
		if (ioctl(fd, BLKDISCARD, range) == -1)
		{
			perror("SequentialDiscard failed!");
			printf("%s: Discard %lu ~ %lu failed\n", __FUNCTION__, range[0], range[0] + range[1] - 1);
			return -1;
		}
		(*total_discard_times)++;
		printf("SequentialDiscard times: %d, total_discard_times is %lu\n", i + 1, *total_discard_times);
	}

	return 0;
}

// 随机discard函数
int randomDiscard(int fd, int start, size_t partitionSize, size_t *total_discard_times)
{
	size_t numChunks = partitionSize / CHUNK_SIZE;
	for (size_t i = 0; i < numChunks; i++)
	{
		size_t randomOffset = rand() % numChunks;
		unsigned long range[2];
		range[0] = (unsigned long)(start * SECTORSIZE + randomOffset * CHUNK_SIZE);
		range[1] = CHUNK_SIZE;
		if (ioctl(fd, BLKDISCARD, range) == -1)
		{
			perror("RandomDiscard failed!");
			printf("%s: Discard %lu ~ %lu failed\n", __FUNCTION__, range[0], range[0] + range[1] - 1);
			return -1;
		}
		(*total_discard_times)++;
		printf("RandomDiscard times: %d, total_discard_times is %lu\n", i + 1, *total_discard_times);
	}

	return 0;
}

const char *path[NUM_PATHS_TO_CHECK] = {
	"/dev/block/by-name/vendor_boot_bak",
	"/dev/block/by-name/uboot_bak",
	"/dev/block/by-name/misc",
	"/dev/block/by-name/otp",
	"/dev/block/by-name/recovery",
};

int main(int argc, char **argv)
{
	int res, total_test_cycles, total_test_sector;

	if (argc != 2)
	{
		printf("command format:./io_test [test cycles]\n");
		return -1;
	}

	total_test_cycles = atoi(argv[1]); // 将字符串转换为一个整数（类型为int）
	printf("test_cycles: %d\n", total_test_cycles);
	if (total_test_cycles == 0)
	{

		printf("Command format:./io_test [test cycles]\n");
		printf("Please input right test cycles\n");
		return -1;
	}

	int fd = -1;
	for (int i = 0; i < NUM_PATHS_TO_CHECK; i++)
	{
		if (path[i] == NULL)
			continue;

		fd = open(path[i], O_RDWR | O_SYNC);
		if (fd < 0)
		{
			printf("Could not open file %s for test, trying next option\n", path[i]);
		}
		else
		{
			printf("Successfully open %s for test.\n", path[i]);
			break;
		}
	}

	/* total_test_sector */
	res = ioctl(fd, BLKGETSIZE, &total_test_sector);
	if (res < 0)
	{
		printf("Failed to get partition size!!\n");
		goto exit;
	}
	printf("total_test_sector: %d\n", total_test_sector);

	int loops = total_test_cycles;
	size_t total_discard_times = 0;
	for (int j = 0; j < loops; j++)
	{
		// 第一次填充分区
		res = fillPartition(fd, total_test_sector * SECTORSIZE);
		if (res)
		{
			printf("FillPartition failed!!\n");
			goto exit;
		}

		// 第一次顺序discard
		res = sequentialDiscard(fd, 0, total_test_sector * SECTORSIZE, &total_discard_times);
		if (res)
		{
			printf("sequentialDiscard failed!!\n");
			goto exit;
		}

		// 第二次填充分区
		res = fillPartition(fd, total_test_sector * SECTORSIZE);
		if (res)
		{
			printf("FillPartition failed!!\n");
			goto exit;
		}

		// 第二次随机discard（与顺序discard次数一致）
		res = randomDiscard(fd, 0, total_test_sector * SECTORSIZE, &total_discard_times);
		if (res)
		{
			printf("randomDiscard failed!!\n");
			goto exit;
		}
	}
	printf("total_discard_times is %lu\n", total_discard_times);
	printf("io test finished!!\n");

exit:
	close(fd);
	return 0;
}
