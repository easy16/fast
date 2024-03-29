/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <dirent.h>
#include "shared_func.h"
#include "logger.h"

char *formatDatetime(const time_t nTime, \
	const char *szDateFormat, \
	char *buff, const int buff_size)
{
	static char szDateBuff[128];
	struct tm *tmTime;
	int size;

	tmTime = localtime(&nTime);
	if (buff == NULL)
	{
		buff = szDateBuff;
		size = sizeof(szDateBuff);
	}
	else
	{
		size = buff_size;
	}

	buff[0] = '\0';
	strftime(buff, size, szDateFormat, tmTime);
	
	return buff;
}

int getCharLen(const char *s)
{
	unsigned char *p;
	int count = 0;
	
	p = (unsigned char *)s;
	while (*p != '\0')
	{
		if (*p > 127)
		{
			if (*(++p) != '\0')
			{
				p++;
			}
		}
		else
		{
			p++;
		}
		
		count++;
	}
	
	return count;
}

char *replaceCRLF2Space(char *s)
{
	char *p = s;
	
	while (*p != '\0')
	{
		if (*p == '\r' || *p == '\n')
		{
			*p = ' ';
		}
		
		p++;
	}
	
	return s;
}

char *getAppAbsolutePath(const char *exeName, char *szAbsPath, const int pathSize)
{
	char *p;
	char *szPath;
	int nPathLen;
	char cwd[256];
	
	szPath = (char *)malloc(strlen(exeName) + 1);
	
	p = rindex(exeName, '/');
	if (p == NULL)
	{
		szPath[0] = '\0';
	}
	else
	{
		nPathLen = p - exeName;
		strncpy(szPath, exeName, nPathLen);
		szPath[nPathLen] = '\0';
	}
	
	if (szPath[0] == '/')
	{
		snprintf(szAbsPath, pathSize, "%s", szPath);
	}
	else
	{
		if (getcwd(cwd, 256) == NULL)
		{
			free(szPath);
			return NULL;
		}
		
		nPathLen = strlen(cwd);
		if (cwd[nPathLen - 1] == '/')	//去除路径最后的/
		{
			cwd[nPathLen - 1] = '\0';
		}
		
		if (szPath[0] != '\0')
		{
			snprintf(szAbsPath, pathSize, "%s/%s", cwd, szPath);
		}
		else
		{
			snprintf(szAbsPath, pathSize, "%s", cwd);
		}
	}
	
	free(szPath);
	
	return szAbsPath;
}

int getProccessCount(const char *progName, const bool bAllOwners)
{
	int *pids = NULL;
	return getUserProcIds(progName, bAllOwners, pids, 0);
}

int getUserProcIds(const char *progName, const bool bAllOwners, int pids[], const int arrSize)
{
	char path[80]="/proc";
	char fullpath[80];
	struct stat statbuf;
	struct dirent *dirp;
	DIR  *dp;
	int  myuid=getuid();
	int  fd;
	char filepath[80];
	char buf[256];
	char *ptr;
	int  nbytes;
	char procname[64];
	int  cnt=0;
	char *pTargetProg;
	
	if ((dp = opendir(path)) == NULL)
	{
		return -1;
	}
	
	pTargetProg = (char *)malloc(strlen(progName) + 1);
	ptr = rindex(progName, '/');
	if (ptr == NULL)
	{
		strcpy(pTargetProg, progName);
	}
	else
	{
		strcpy(pTargetProg, ptr + 1);
	}
	
	while( (dirp = readdir(dp)) != NULL )
	{
		if (strcmp(dirp->d_name, ".")==0 || strcmp(dirp->d_name, "..")==0 )
		{
			continue;
		}
		
		sprintf(fullpath, "%s/%s", path, dirp->d_name);
		memset(&statbuf, 0, sizeof(statbuf));
		if (lstat(fullpath, &statbuf)<0)
		{
			continue;
		}
		
		if ((bAllOwners || (statbuf.st_uid == myuid)) && S_ISDIR(statbuf.st_mode))
		{
			sprintf(filepath, "%s/cmdline", fullpath);
			if ((fd = open(filepath, O_RDONLY))<0)
			{
				continue;
			}
			
			memset(buf, 0, 256);
			if ((nbytes = read(fd, buf, 255)) < 0){
				close(fd);
				continue;
			}
			close(fd);
			
			if (*buf == '\0')
			{
				continue;
			}
			
			ptr = rindex(buf, '/');
			if (ptr == NULL)
			{
				snprintf(procname, 64, "%s", buf);
			}
			else
			{
				snprintf(procname, 64, "%s", ptr + 1);
			}
			
			if (strcmp(procname, pTargetProg) == 0)
			{				
				if (pids != NULL)
				{
					if (cnt >= arrSize)
					{
						break;
					}
					pids[cnt] = atoi(dirp->d_name);
				}
				
				cnt++;
			}
		}
	}
	free(pTargetProg);
	
	closedir(dp);
	return cnt;
}

char *toLowercase(char *src)
{
	char *p;
	
	p = src;
	while (*p != '\0')
	{
		*p = tolower(*p);
		p++;
	}
	
	return src;
}

char *toUppercase(char *src)
{
	char *p;
	
	p = src;
	while (*p != '\0')
	{
		*p = toupper(*p);
		p++;
	}
	
	return src;	
}

void daemon_init(bool bCloseFiles)
{
	pid_t pid;
	int i;
	
	if((pid=fork())!=0)
	{
		exit(0);
	}
	
	setsid();
	
	if((pid=fork())!=0)
	{
		exit(0);
	}
	
	chdir("/");
	
	if (bCloseFiles)
	{
		for(i=0;i<64;i++)
		{
			close(i);
		}
	}

	return;
}

char *bin2hex(const char *s, const int len, char *szHexBuff)
{
	unsigned char *p;
	int nLen;
	int i;
	
	nLen = 0;
	p = (unsigned char *)s;
	for (i=0; i<len; i++)
	{
		nLen += sprintf(szHexBuff + nLen, "%02X", *p);
		p++;
	}
	
	szHexBuff[nLen] = '\0';
	return szHexBuff;
}

char *hex2bin(const char *s, char *szBinBuff, int *nDestLen)
{
        char buff[3];
	char *p;
	int nSrcLen;
	int i;
	
	nSrcLen = strlen(s);
        if (nSrcLen == 0)
        {
          *nDestLen = 0;
          szBinBuff[0] = '\0';
          return szBinBuff;
        }

	*nDestLen = nSrcLen / 2;
	p = (char *)s;
        buff[2] = '\0';
	for (i=0; i<*nDestLen; i++)
	{
		memcpy(buff, p, 2);
		szBinBuff[i] = strtol(buff, NULL, 16);
		p += 2;
	}
	
	szBinBuff[*nDestLen] = '\0';
	return szBinBuff;
}

void printBuffHex(const char *s, const int len)
{
	unsigned char *p;
	int i;
	
	p = (unsigned char *)s;
	for (i=0; i<len; i++)
	{
		printf("%02X", *p);
		p++;
	}
	printf("\n");
}

char *trim_left(char *pStr)
{
	int ilength;
	int i;
	char *pTemp;
	char ch;
	int nDestLen;
	
	ilength = strlen(pStr);
	
	for ( i=0; i<ilength; i++ )
	{
		ch = pStr[i];
		if (!(' ' == ch || '\n' == ch || '\r' == ch || '\t' == ch))
		{
			break;
		}
	}
	
	if ( 0 == i)
	{
		return pStr;
	}
	
	nDestLen = ilength - i;
	pTemp = (char *)malloc(nDestLen + 1);
	strcpy(pTemp, pStr + i);
	strcpy(pStr, pTemp);
	free(pTemp);
	return pStr;
}

char *trim_right(char *pStr)
{
	int len;
	char *p;
	char *pEnd;
	char ch;

	len = strlen(pStr);
	if (len == 0)
	{
		return pStr;
	}

	pEnd = pStr + len - 1;
	for (p = pEnd;  p>=pStr; p--)
	{
		ch = *p;
		if (!(' ' == ch || '\n' == ch || '\r' == ch || '\t' == ch))
		{
			break;
		}
	}

	if (p != pEnd)
	{
		*(p+1) = '\0';
	}

	return pStr;
}

char *trim(char *pStr)
{
	trim_right(pStr);
	trim_left(pStr);
	return pStr;
}

char *formatDateYYYYMMDDHHMISS(const time_t t, char *szDateBuff, const int nSize)
{
	time_t timer = t;
	struct tm *tm = localtime(&timer);
	
	snprintf(szDateBuff, nSize, "%04d%02d%02d%02d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
	return szDateBuff;
}

int getOccurCount(const char *src, const char seperator)
{
	int count;
	char *p;

	count = 0;
	p = strchr(src, seperator);
	while (p != NULL)
	{
		count++;
		p = strchr(p + 1, seperator);
	}

	return count;
}

char **split(char *src, const char seperator, const int nMaxCols, int *nColCount)
{
	char **pCols;
	char **pCurrent;
	char *p;
	int i;
	int nLastIndex;

	if (src == NULL)
	{
		*nColCount = 0;
		return NULL;
	}

	*nColCount = 1;
	p = strchr(src, seperator);
	
	while (p != NULL)
	{
		(*nColCount)++;
		p = strchr(p + 1, seperator);
	}

	if (nMaxCols > 0 && (*nColCount) > nMaxCols)
	{
		*nColCount = nMaxCols;
	}
	
	pCurrent = pCols = (char **)malloc(sizeof(char *) * (*nColCount));
	p = src;
	nLastIndex = *nColCount - 1;
	for (i=0; i<*nColCount; i++)
	{
		*pCurrent = p;
		pCurrent++;

		p = strchr(p, seperator);
		if (i != nLastIndex)
		{
			*p = '\0';
			p++;	//跳过分隔符
		}
	}

	return pCols;
}

void freeSplit(char **p)
{
	if (p != NULL)
	{
		free(p);
	}
}

int splitEx(char *src, const char seperator, char **pCols, const int nMaxCols)
{
	char *p;
	char **pCurrent;
	int count = 0;

	if (nMaxCols <= 0)
	{
		return 0;
	}

	p = src;
	pCurrent = pCols;

	while (true)
	{
		*pCurrent = p;
		pCurrent++;

		count++;
		if (count >= nMaxCols)
		{
			break;
		}

		p = strchr(p, seperator);
		if (p == NULL)
		{
			break;
		}

		*p = '\0';
		p++;
	}

	return count;
}

int my_strtok(char *src, const char *delim, char **pCols, const int nMaxCols)
{
    char *p;
    char **pCurrent;
    int count;
    bool bWordEnd;
    
    if (src == NULL || pCols == NULL)
    {
        return -1;
    }

    if (nMaxCols <= 0)
    {
        return 0;
    }
    
    p = src;
    pCurrent = pCols;
    
    //跳过前导分隔符
    while (*p != '\0')
    {
        if (strchr(delim, *p) == NULL)
        {
            break;
        }
        p++;
    }
    
    if (*p == '\0')
    {
        return 0;
    }
    
    *pCurrent = p;
    bWordEnd = false;
    count = 1;
    if (count >= nMaxCols)
    {
        return count;
    }
    
    while (*p != '\0')
    {
        if (strchr(delim, *p) != NULL)
        {
            *p = '\0';
            bWordEnd = true;
        }
        else
        {
            if (bWordEnd)
            {
                pCurrent++;
                *pCurrent = p;
                
                count++;
                if (count >= nMaxCols)
                {
                    break;
                }

                bWordEnd = false;
            }
        }
        
        p++;    //跳过分隔符
    }

    return count;
}

char int2base62(const int i)
{
  #define _BASE62_COUNT  62
  static char base62[_BASE62_COUNT] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                     'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                     'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
                     'U', 'V', 'W', 'X', 'Y', 'Z',
                     'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
                     'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
                     'u', 'v', 'w', 'x', 'y', 'z'
                    };

  if (i < 0 || i >= _BASE62_COUNT)
  {
    return ' ';
  }

  return base62[i];
}

FILE *openConfFile(const char *szFilename)
{
	FILE *fp;
	char szFullFilename[256];
	
	sprintf(szFullFilename, FDFS_BASE_FILE_PATH"%s", szFilename);
	fp = fopen(szFullFilename, "r");
	
	return fp;
}

bool fileExists(const char *filename)
{
	return access(filename, 0) == 0;
}

bool isDir(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) != 0)
	{
		return false;
	}

	return S_ISDIR(buf.st_mode);
}

bool isFile(const char *filename)
{
	struct stat buf;
	if (stat(filename, &buf) != 0)
	{
		return false;
	}

	return S_ISREG(buf.st_mode);
}

void chopPath(char *filePath)
{
	int lastIndex;
	if (*filePath == '\0')
	{
		return;
	}

	lastIndex = strlen(filePath) - 1;
	if (filePath[lastIndex] == '/')
	{
		filePath[lastIndex] = '\0';
	}
}

int init_pthread_lock(pthread_mutex_t *pthread_lock)
{
	pthread_mutexattr_t mat;

	if (pthread_mutexattr_init(&mat) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutexattr_init fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}
	if (pthread_mutexattr_settype(&mat, PTHREAD_MUTEX_ERRORCHECK) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutexattr_settype fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}
	if (pthread_mutex_init(pthread_lock, &mat) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_init fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}
	if (pthread_mutexattr_destroy(&mat) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call thread_mutexattr_destroy fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}

	return 0;
}

int getFileContent(const char *filename, char **buff, int *file_size)
{
	FILE *fp;
	
	fp = fopen(filename, "rb");
	if (fp == NULL)
	{
		*buff = NULL;
		*file_size = 0;
		return errno != 0 ? errno : ENOENT;
	}

	if (fseek(fp, 0, SEEK_END) != 0)
	{
		*buff = NULL;
		*file_size = 0;
		fclose(fp);
		return errno != 0 ? errno : ENOENT;
	}

	*file_size = ftell(fp);
	if (*file_size < 0)
	{
		*buff = NULL;
		*file_size = 0;
		fclose(fp);
		return errno != 0 ? errno : ENOENT;
	}

	*buff = (char *)malloc(*file_size + 1);
	if (*buff == NULL)
	{
		*file_size = 0;
		fclose(fp);
		return errno != 0 ? errno : ENOMEM;
	}

	rewind(fp);
	if (fread(*buff, 1, *file_size, fp) != *file_size)
	{
		free(*buff);
		*buff = NULL;
		*file_size = 0;
		fclose(fp);
		return errno != 0 ? errno : ENOENT;
	}

	(*buff)[*file_size] = '\0';
	fclose(fp);

	return 0;
}

int writeToFile(const char *filename, const char *buff, const int file_size)
{
	FILE *fp;
	fp = fopen(filename, "wb");
	if (fp == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if (fwrite(buff, 1, file_size, fp) != file_size)
	{
		logError("file: "__FILE__", line: %d, " \
			"write file %s fail, " \
			"errno: %d, error info: %s", \
			__LINE__, filename, \
			errno, strerror(errno));
		fclose(fp);
		return errno != 0 ? errno : ENOENT;
	}

	fclose(fp);
	return 0;
}

void int2buff(const int n, char *buff)
{
	unsigned char *p;
	p = (unsigned char *)buff;
	*p++ = (n >> 24) & 0xFF;
	*p++ = (n >> 16) & 0xFF;
	*p++ = (n >> 8) & 0xFF;
	*p++ = n & 0xFF;
}

int buff2int(const unsigned char *buff)
{
	return  ((*buff) << 24) | (*(buff+1) << 16) |  \
		(*(buff+2) << 8) | *(buff+3);
}

int fd_gets(int fd, char *buff, const int size, int once_bytes)
{
	char *pDest;
	char *p;
	char *pEnd;
	int read_bytes;
	int remain_bytes;
	int rewind_bytes;

	if (once_bytes <= 0)
	{
		once_bytes = 1;
	}

	pDest = buff;
	remain_bytes = size - 1;
	while (remain_bytes > 0)
	{
		if (once_bytes > remain_bytes)
		{
			once_bytes = remain_bytes;
		}

		read_bytes = read(fd, pDest, once_bytes);
		if (read_bytes < 0)
		{
			return -1;
		}
		if (read_bytes == 0)
		{
			break;
		}

		pEnd = pDest + read_bytes;
		for (p=pDest; p<pEnd; p++)
		{
			if (*p == '\n')
			{
				break;
			}
		}

		if (p < pEnd)
		{
			pDest = p + 1;  //find \n, skip \n
			rewind_bytes = pEnd - pDest;
			if (lseek(fd, -1 * rewind_bytes, SEEK_CUR) < 0)
			{
				return -1;
			}

			break;
		}

		pDest = pEnd;
		remain_bytes -= read_bytes;
	}

	*pDest = '\0';
	return pDest - buff;
}

