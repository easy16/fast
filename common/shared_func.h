/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#ifndef SHARED_FUNC_H
#define SHARED_FUNC_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "fdfs_define.h"

#ifdef __cplusplus
extern "C" {
#endif

char *toLowercase(char *src);
char *toUppercase(char *src);

char *formatDatetime(const time_t nTime, \
	const char *szDateFormat, \
	char *buff, const int buff_size);

int getCharLen(const char *s);
char *replaceCRLF2Space(char *s);

char *getAppAbsolutePath(const char *exeName, char *szAbsPath, \
				const int pathSize);

int getProccessCount(const char *progName, const bool bAllOwners);

int getUserProcIds(const char *progName, const bool bAllOwners, \
			int pids[], const int arrSize);

void daemon_init(bool bCloseFiles);

char *bin2hex(const char *s, const int len, char *szHexBuff);
char *hex2bin(const char *s, char *szBinBuff, int *nDestLen);
void printBuffHex(const char *s, const int len);
char int2base62(const int i);

void int2buff(const int n, char *buff);
int buff2int(const unsigned char *buff);

char *trim_left(char *pStr);
char *trim_right(char *pStr);
char *trim(char *pStr);

int getOccurCount(const char *src, const char seperator);
char **split(char *src, const char seperator, const int nMaxCols, \
		int *nColCount);
void freeSplit(char **p);

int splitEx(char *src, const char seperator, char **pCols, const int nMaxCols);
int my_strtok(char *src, const char *delim, char **pCols, const int nMaxCols);

FILE *openConfFile(const char *szFilename);

bool fileExists(const char *filename);
bool isDir(const char *filename);
bool isFile(const char *filename);
void chopPath(char *filePath);
int getFileContent(const char *filename, char **buff, int *file_size);
int writeToFile(const char *filename, const char *buff, const int file_size);
int fd_gets(int fd, char *buff, const int size, int once_bytes);

int init_pthread_lock(pthread_mutex_t *pthread_lock);

#ifdef __cplusplus
}
#endif

#endif
