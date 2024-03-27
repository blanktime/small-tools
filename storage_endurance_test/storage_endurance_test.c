#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#include "mmc.h"
#include "mmc_cmds.h"
#include "ioctl.h"

#define MAX_RW_TEST_SECTORS 32768 // 16M
#define SECTOR_SIZE 512
#define BLKDISCARD _IO(0x12, 119)
#define BLKSECDISCARD _IO(0x12, 125)
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A 268 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B 269 /* RO */
#define NUM_PATHS_TO_CHECK 5

FILE *pFile;

/* For Generate Random value */
static long holdrand = 1L;
#define GET_RANDOM() ((((holdrand = holdrand * 214013L + 2531011L) >> 16)))

// eMMC format from start to start+count
static int emmc_format(int fd, int start, int count)
{
	int ret = 0;
	// unsigned long long range[2];
	unsigned long long range[2];

	range[0] = (long unsigned int)start * SECTOR_SIZE;
	range[1] = (long unsigned int)count * SECTOR_SIZE;
	ret = ioctl(fd, BLKDISCARD, range);
	if (ret < 0)
	{
		printf("%s: D %llu ~ %llu failed, sector count %d\n", __FUNCTION__, range[0], range[0] + range[1] - 1, count);
		return -1;
	}
	else
	{
		printf("Partition %llu ~ %llu was erased, sector count %d\n", range[0], range[0] + range[1] - 1, count);
	}
	return 0;
}

// write log
int write_log(FILE *pFile, const char *format, ...)
{
	va_list arg;
	int done;

	va_start(arg, format);
	// done = vfprintf (stdout, format, arg);

	time_t time_log = time(NULL);
	struct tm *tm_log = localtime(&time_log);
	fprintf(pFile, "%04d-%02d-%02d %02d:%02d:%02d ", tm_log->tm_year + 1900, tm_log->tm_mon + 1, tm_log->tm_mday, tm_log->tm_hour, tm_log->tm_min, tm_log->tm_sec);

	done = vfprintf(pFile, format, arg);
	va_end(arg);

	fflush(pFile);
	return done;
}

// read_extcsd
int read_extcsd(int fd, __u8 *ext_csd)
{
	int ret = 0;
	struct mmc_ioc_cmd idata;
	memset(&idata, 0, sizeof(idata));
	memset(ext_csd, 0, sizeof(__u8) * 512);
	idata.write_flag = 0;
	idata.opcode = MMC_SEND_EXT_CSD;
	idata.arg = 0;
	idata.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;
	idata.blksz = 512;
	idata.blocks = 1;
	mmc_ioc_cmd_set_data(idata, ext_csd);

	ret = ioctl(fd, MMC_IOC_CMD, &idata);
	if (ret)
	{
		perror("ioctl");
	}

	return ret;
}

// Partitions of operations
const char *path[NUM_PATHS_TO_CHECK] = {
	"/dev/block/by-name/vendor_boot_b",
	"/dev/block/by-name/otp",
	"/dev/block/by-name/uboot_b",
};

int main(int argc, char **argv)
{
	int test_mode, total_test_sector;
	double tmp;
	double life_val = 0;
	unsigned char *wptr = NULL;
	int i, j, pattern_idx;
	int loops = 0, total_test_cycles = 0;
	int num, data_count = 0;
	int fd = -1, fd_device = -1, ret = 0, res;
	int w_num = 0;
	unsigned long dwSeed = 0;
	char *device = "/dev/block/mmcblk0"; // eMMC device
	unsigned char *test_pattern = NULL;
	unsigned long long range[2];
	unsigned char ext_csd[512], life_time, life_flag = 0;
	struct hd_geometry g;
	unsigned long start_lba;
	unsigned long long w_all_sector = 0;
	long double w_all_sector_GB;
	unsigned char sectors;
	char pattern[16][16] = {
		{0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
		{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0},
		{0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11},
		{0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22},
		{0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33},
		{0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44},
		{0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55},
		{0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
		{0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77},
		{0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
		{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99},
		{0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA},
		{0xCC, 0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB},
		{0xDD, 0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC},
		{0xEE, 0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD},
		{0xFF, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE},
	};
	system("rm -rf ./log_storage_life.txt");
	system("rm -rf ./w_dat.txt");
	pFile = fopen("./log_storage_life.txt", "a");

	write_log(pFile, "eMMC s_test start...\n");

	// Invalid parameter
	if (argc < 1 || argc > 4)
	{
		printf("command format: %s [mode] [test cycles][life_time]\r\n", argv[0]);
		write_log(pFile, "command format: %s [mode] [test cycles][life_time]\r\n", argv[0]);
		printf("[mode] 0: normal mode, 1: infinite loop\r\n");
		write_log(pFile, "[mode] 0: normal mode, 1: infinite loop\r\n");
		printf("[life_time] 0: do not care life time, 1~11: corresponding life time\r\n");
		write_log(pFile, "[life_time] 0: do not care life time, 1~11: corresponding life time\r\n");
		return -1;
	}
	// printf("argc:%d, argv:%s\r\n", argc, *argv);
	// write_log(pFile, "argc:%d, argv:%s\r\n", argc, *argv);

	// test mode and test cycle
	test_mode = atoi(argv[1]);
	if (test_mode == 0)
	{
		if (argc != 4)
		{
			printf("command format: [mode] [test cycles]\r\n");
			write_log(pFile, "command format: [mode] [test cycles]\r\n");
			return -1;
		}
		total_test_cycles = atoi(argv[2]);
		printf("normal test mode, total_test_cycles: %d\r\n", total_test_cycles);
		write_log(pFile, "normal test mode, total_test_cycles: %d\r\n", total_test_cycles);
	}
	else if (test_mode == 1)
	{
		printf("Infinite loop, run until eMMC wears out!\r\n");
		write_log(pFile, "Infinite loop, run until eMMC wears out!\r\n");
	}
	else
	{
		printf("test mode %d not support yet\r\n", test_mode);
		write_log(pFile, "test mode %d not support yet\r\n", test_mode);
		return -1;
	}

	// life time
	life_time = atoi(argv[3]);
	if (life_time > 11 || life_time < 0)
	{
		printf("life time %d not support\r\n", life_time);
		write_log(pFile, "life time %d not support\r\n", life_time);
		return -1;
	}
	else if (life_time == 0)
	{
		printf("Do not care life time\r\n");
		write_log(pFile, "Do not care life time\r\n");
		life_flag = 0;
	}
	else
	{
		printf("need reach life time is %d\r\n", life_time);
		write_log(pFile, "need reach life time is %d\r\n", life_time);
		life_flag = 1;
	}

	// used to read ecsd
	if (life_time)
	{
		fd_device = open(device, O_RDWR);
		if (fd_device < 0)
		{
			perror("open");
			printf("open %s fail: %d\n", device, life_flag);
			exit(1);
		}
		else
		{
			printf("open %s to get life time\n", device);
		}
	}

	// open partition
	for (i = 0; i < NUM_PATHS_TO_CHECK; i++)
	{
		if (path[i] == NULL)
			continue;

		fd = open(path[i], O_RDWR | O_SYNC);
		if (fd < 0)
		{
			printf("Could not open file %s for s_l test, trying next option\n", path[i]);
			write_log(pFile, "Could not open file %s for s_l test, trying next option\n", path[i]);
		}
		else
		{
			printf("Successfully open %s for s_l test.\n", path[i]);
			write_log(pFile, "Successfully open %s for s_l test.\n", path[i]);
			break;
		}
	}
	if (fd < 0)
	{
		printf("No partitions were found in for s_l test after find %d path, need to prvode mode path.\n", NUM_PATHS_TO_CHECK);
		write_log(pFile, "No partitions were found in for s_l test after find %d path, need to prvode mode path.\n", NUM_PATHS_TO_CHECK);
		goto error;
	}
	else
	{
		printf("%s: open %s ok for s_l test.\n", __FUNCTION__, path[i]);
	}

	// get partition size
	res = ioctl(fd, BLKGETSIZE, &total_test_sector);
	if (res < 0)
	{
		printf("Failed to get partition size");
		write_log(pFile, "Failed to get partition size");
		system("dmesg > /data/dmesg.txt");
		goto error;
	}
	printf("total_test_sector: %d\n", total_test_sector);
	write_log(pFile, "total_test_sector: %d\n", total_test_sector);

	test_pattern = (unsigned char *)calloc(total_test_sector, SECTOR_SIZE);
	if (test_pattern == NULL)
	{
		printf("allocate memory for test pattern failed!");
		write_log(pFile, "allocate memory for test pattern failed!");
		goto error;
	}

	// print start lba
	if (!ioctl(fd, HDIO_GETGEO, &g))
	{
		start_lba = g.start;
		sectors = g.sectors;
		printf("test start lba: %lu\n", start_lba);
		write_log(pFile, "test start lba: %lu\n", start_lba);
	}

	// start loop
	for (loops = 0; (test_mode == 1) || (loops < total_test_cycles) || life_flag; loops++)
	{
		printf("Loop %d\r\n", loops + 1);
		write_log(pFile, "Loop %d\r\n", loops + 1);

		// // print eMMC basic information
		// system("cat /sys/block/mmcblk0/device/name");
		// system("cat /sys/block/mmcblk0/size");
		// system("cat /sys/block/mmcblk0/device/life_time");

		// print lifetime information
		if (life_time)
		{
			ret = read_extcsd(fd_device, ext_csd);
			if (ret)
			{
				printf("Could not read e_csd from %s\n", device);
				write_log(pFile, "Could not read e_csd from %s\n", device);
				goto error;
			}
			else
			{
				printf("life_flag %d, A: %02x, B: %02x\n", life_flag, ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A], ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B]);
				write_log(pFile, "life_flag %d, A: %02x, B: %02x\n", life_flag, ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A], ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B]);
			}
		}
		if (life_flag && (ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A] >= life_time || ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B] >= life_time))
		{
			printf("Device life time estimation reached, test stop!!\r\n");
			write_log(pFile, "Device life time estimation reached, test stop!!\r\n");
			life_flag = 0;
			break;
		}
		else
		{
			printf("Device life time estimation %d is not reach, test continue\n", life_time);
			write_log(pFile, "Device life time estimation %d is not reach, test continue\n", life_time);
		}

		// Erase
		// printf("Start to D %s\r\n", path[i]);
		// write_log(pFile, "Start to D %s\r\n", path[i]);

		// if (emmc_format(fd, 0, total_test_sector) < 0)
		// {
		// 	printf("<%d> %s: erase EMMC failed!\n", loops + 1, __FUNCTION__);
		// 	write_log(pFile, "<%d> %s: erase EMMC failed!\n", loops + 1, __FUNCTION__);
		// 	system("dmesg > /data/dmesg.txt");
		// 	goto error;
		// }

		// Write to full
		printf("Start to w\r\n");
		write_log(pFile, "Start to w\r\n");
		lseek(fd, 0, SEEK_SET);
		memset(test_pattern, 0, total_test_sector * SECTOR_SIZE);

		data_count = total_test_sector * SECTOR_SIZE;
		pattern_idx = loops % 16;
		for (j = 0; j < data_count; j = j + 16)
		{
			memcpy(test_pattern + j, &pattern[pattern_idx], 16);
		}
		wptr = test_pattern;
		printf("w_num: %d, start lba: %lu, total_test_sector: %d\r\n", w_num, start_lba, total_test_sector);
		write_log(pFile, "w_num: %d, start lba: %lu, total_test_sector: %d\r\n", w_num, start_lba, total_test_sector);

		ret = write(fd, wptr, data_count);
		if (ret <= 0)
		{
			printf("<%d> %s [ERROR]: start lba: %lu, (w_data: %llu sectors)\n", loops + 1, __FUNCTION__, start_lba, w_all_sector);
			write_log(pFile, "<%d> %s [ERROR]: start lba: %lu, (w_data: %llu sectors)\n", loops + 1, __FUNCTION__, start_lba, w_all_sector);
			system("dmesg > /data/dmesg.txt");
			goto error;
		}

		w_all_sector += total_test_sector;

		tmp = 1024 * 1024 * 1024 / SECTOR_SIZE;
		w_all_sector_GB = w_all_sector / tmp;

		tmp = 32.0 * 1024.0 * 1024.0 * 1024.0 / SECTOR_SIZE / total_test_sector;
		life_val = (loops + 1) / tmp;

		printf("w_num = %d, total_test_sector = %d, life value is: %lf\n", w_num, total_test_sector, life_val);
		write_log(pFile, "w_num = %d, total_test_sector = %d, life value is: %lf\n", w_num, total_test_sector, life_val);
		printf("<%d> Test finished and OK, W_Data %Lf GB(%llu sectors)\r\n\n", loops + 1, w_all_sector_GB, w_all_sector);
		write_log(pFile, "<%d> Test finished and OK, W_Data %Lf GB(%llu sectors)\r\n\n", loops + 1, w_all_sector_GB, w_all_sector);

		w_num++;
	}

	printf("eMMC s_test done, life value is %lf\n", life_val);
	write_log(pFile, "eMMC s_test done, life value is %lf\n", life_val);

error:
	if (test_pattern)
	{
		free(test_pattern);
	}
	if (fd >= 0)
	{
		close(fd);
		fd = -1;
	}
	return -1;
}
