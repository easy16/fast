/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

//storage_sync.c

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "fdfs_define.h"
#include "logger.h"
#include "fdfs_global.h"
#include "sockopt.h"
#include "shared_func.h"
#include "ini_file_reader.h"
#include "tracker_types.h"
#include "tracker_proto.h"
#include "storage_global.h"
#include "storage_func.h"
#include "storage_sync.h"
#include "tracker_client_thread.h"

#define SYNC_BINLOG_FILE_MAX_SIZE	1024 * 1024 * 1024
#define SYNC_BINLOG_FILE_PREFIX		"binlog"
#define SYNC_BINLOG_INDEX_FILENAME	SYNC_BINLOG_FILE_PREFIX".index"
#define SYNC_MARK_FILE_EXT		".mark"
#define SYNC_BINLOG_FILE_EXT_FMT	".%03d"
#define SYNC_DIR_NAME			"sync"
#define MARK_ITEM_BINLOG_FILE_INDEX	"binlog_index"
#define MARK_ITEM_BINLOG_FILE_OFFSET	"binlog_offset"
#define MARK_ITEM_NEED_SYNC_OLD		"need_sync_old"
#define MARK_ITEM_SYNC_OLD_DONE		"sync_old_done"
#define MARK_ITEM_UNTIL_TIMESTAMP	"until_timestamp"
#define MARK_ITEM_SCAN_ROW_COUNT	"scan_row_count"
#define MARK_ITEM_SYNC_ROW_COUNT	"sync_row_count"

FILE *g_fp_binlog = NULL;
int g_binlog_index = 0;
static int binlog_file_size = 0;

int g_storage_sync_thread_count = 0;
static pthread_mutex_t sync_thread_lock;

static int storage_write_to_mark_file(BinLogReader *pReader);
static int storage_binlog_reader_skip(BinLogReader *pReader);
static void storage_reader_destroy(BinLogReader *pReader);

/**
9 bytes: filename bytes
9 bytes: file size
FDFS_GROUP_NAME_MAX_LEN bytes: group_name
filename bytes : filename
file size bytes: file content
**/
static int storage_sync_copy_file(TrackerServerInfo *pStorageServer, \
			const BinLogRecord *pRecord, const char proto_cmd)
{
	TrackerHeader header;
	int result;
	int in_bytes;
	int file_size;
	char *file_buff;
	char *p;
	char *pBuff;
	char full_filename[MAX_PATH_SIZE];
	char out_buff[sizeof(TrackerHeader)+FDFS_GROUP_NAME_MAX_LEN+256];
	char in_buff[1];

	snprintf(full_filename, sizeof(full_filename), \
			"%s/data/%s", g_base_path, pRecord->filename);
	if (!fileExists(full_filename))
	{
		if (pRecord->op_type == STORAGE_OP_TYPE_SOURCE_CREATE_FILE)
		{
			logError("file: "__FILE__", line: %d, " \
				"sync data file, file: %s not exists, " \
				"maybe deleted later?", \
				__LINE__, full_filename);
		}

		return 0;
	}

	if ((result=getFileContent(full_filename, \
			&file_buff, &file_size)) != 0)
	{
		return result;
	}

	//printf("sync create file: %s\n", pRecord->filename);
	while (1)
	{
		sprintf(header.pkg_len, "%x", 2 * TRACKER_PROTO_PKG_LEN_SIZE + \
				FDFS_GROUP_NAME_MAX_LEN + \
				pRecord->filename_len + file_size);
		header.cmd = proto_cmd;
		header.status = 0;
		memcpy(out_buff, &header, sizeof(TrackerHeader));

		p = out_buff + sizeof(TrackerHeader);
		sprintf(p, "%x", pRecord->filename_len);
		p += TRACKER_PROTO_PKG_LEN_SIZE;
		sprintf(p, "%x", file_size);
		p += TRACKER_PROTO_PKG_LEN_SIZE;
		sprintf(p, "%s", pStorageServer->group_name);
		p += FDFS_GROUP_NAME_MAX_LEN;
		memcpy(p, pRecord->filename, pRecord->filename_len);
		p += pRecord->filename_len;

		if(tcpsenddata(pStorageServer->sock, out_buff, \
			p - out_buff, g_network_timeout) != 1)
		{
			logError("file: "__FILE__", line: %d, " \
				"sync data to storage server %s:%d fail, " \
				"errno: %d, error info: %s", \
				__LINE__, pStorageServer->ip_addr, \
				pStorageServer->port, \
				errno, strerror(errno));

			result = errno != 0 ? errno : EPIPE;
			break;
		}

		if((file_size > 0) && (tcpsenddata(pStorageServer->sock, \
			file_buff, file_size, g_network_timeout) != 1))
		{
			logError("file: "__FILE__", line: %d, " \
				"sync data to storage server %s:%d fail, " \
				"errno: %d, error info: %s", \
				__LINE__, pStorageServer->ip_addr, \
				pStorageServer->port, \
				errno, strerror(errno));

			result = errno != 0 ? errno : EPIPE;
			break;
		}

		pBuff = in_buff;
		if ((result=tracker_recv_response(pStorageServer, \
			&pBuff, 0, &in_bytes)) != 0)
		{
			break;
		}

		break;
	}

	free(file_buff);

	//printf("sync create file end!\n");
	if (result == EEXIST)
	{
		if (pRecord->op_type == STORAGE_OP_TYPE_SOURCE_CREATE_FILE)
		{
			logError("file: "__FILE__", line: %d, " \
				"storage server ip: %s:%d, data file: %s " \
				"already exists, maybe some mistake?", \
				__LINE__, pStorageServer->ip_addr, \
				pStorageServer->port, pRecord->filename);
		}

		return 0;
	}
	else
	{
		return result;
	}
}

/**
send pkg format:
FDFS_GROUP_NAME_MAX_LEN bytes: group_name
remain bytes: filename
**/
static int storage_sync_delete_file(TrackerServerInfo *pStorageServer, \
			const BinLogRecord *pRecord)
{
	TrackerHeader header;
	int result;
	char full_filename[MAX_PATH_SIZE];
	char out_buff[sizeof(TrackerHeader)+FDFS_GROUP_NAME_MAX_LEN+32];
	char in_buff[1];
	char *pBuff;
	int in_bytes;

	snprintf(full_filename, sizeof(full_filename), \
			"%s/data/%s", g_base_path, pRecord->filename);
	if (fileExists(full_filename))
	{
		if (pRecord->op_type == STORAGE_OP_TYPE_SOURCE_DELETE_FILE)
		{
			logError("file: "__FILE__", line: %d, " \
				"sync data file, file: %s exists, " \
				"maybe created later?", \
				__LINE__, full_filename);
		}

		return 0;
	}

	while (1)
	{
	memset(out_buff, 0, sizeof(out_buff));
	snprintf(out_buff + sizeof(TrackerHeader), sizeof(out_buff) - \
		sizeof(TrackerHeader),  "%s", g_group_name);
	memcpy(out_buff + sizeof(TrackerHeader) + FDFS_GROUP_NAME_MAX_LEN, \
		pRecord->filename, pRecord->filename_len);

	sprintf(header.pkg_len, "%x", FDFS_GROUP_NAME_MAX_LEN + \
			pRecord->filename_len);
	header.cmd = STORAGE_PROTO_CMD_SYNC_DELETE_FILE;
	header.status = 0;
	memcpy(out_buff, &header, sizeof(TrackerHeader));

	if (tcpsenddata(pStorageServer->sock, out_buff, \
		sizeof(TrackerHeader) + FDFS_GROUP_NAME_MAX_LEN + \
		pRecord->filename_len, g_network_timeout) != 1)
	{
		logError("FILE: "__FILE__", line: %d, " \
			"send data to storage server %s:%d fail, " \
			"errno: %d, error info: %s", \
			__LINE__, pStorageServer->ip_addr, \
			pStorageServer->port, \
			errno, strerror(errno));
		result = errno != 0 ? errno : EPIPE;
		break;
	}

	pBuff = in_buff;
	result = tracker_recv_response(pStorageServer, &pBuff, 0, &in_bytes);
	if (result == ENOENT)
	{
		result = 0;
	}
	
	break;
	}

	return result;
}

#define STARAGE_CHECK_IF_NEED_SYNC_OLD(pReader, pRecord) \
	if ((!pReader->need_sync_old) || pReader->sync_old_done || \
		(pRecord->timestamp > pReader->until_timestamp)) \
	{ \
		return 0; \
	} \

static int storage_sync_data(BinLogReader *pReader, \
			TrackerServerInfo *pStorageServer, \
			const BinLogRecord *pRecord)
{
	int result;
	switch(pRecord->op_type)
	{
		case STORAGE_OP_TYPE_SOURCE_CREATE_FILE:
			result = storage_sync_copy_file(pStorageServer, \
				pRecord, STORAGE_PROTO_CMD_SYNC_CREATE_FILE);
			break;
		case STORAGE_OP_TYPE_SOURCE_DELETE_FILE:
			result = storage_sync_delete_file( \
				pStorageServer, pRecord);
			break;
		case STORAGE_OP_TYPE_SOURCE_UPDATE_FILE:
			result = storage_sync_copy_file(pStorageServer, \
				pRecord, STORAGE_PROTO_CMD_SYNC_UPDATE_FILE);
			break;
		case STORAGE_OP_TYPE_REPLICA_CREATE_FILE:
			STARAGE_CHECK_IF_NEED_SYNC_OLD(pReader, pRecord)
			result = storage_sync_copy_file(pStorageServer, \
				pRecord, STORAGE_PROTO_CMD_SYNC_CREATE_FILE);
			break;
		case STORAGE_OP_TYPE_REPLICA_DELETE_FILE:
			STARAGE_CHECK_IF_NEED_SYNC_OLD(pReader, pRecord)
			result = storage_sync_delete_file( \
				pStorageServer, pRecord);
			break;
		case STORAGE_OP_TYPE_REPLICA_UPDATE_FILE:
			STARAGE_CHECK_IF_NEED_SYNC_OLD(pReader, pRecord)
			result = storage_sync_copy_file(pStorageServer, \
				pRecord, STORAGE_PROTO_CMD_SYNC_UPDATE_FILE);
			break;
		default:
			return EINVAL;
	}

	if (result == 0)
	{
		pReader->sync_row_count++;
	}

	return result;
}

static int write_to_binlog_index()
{
	char full_filename[MAX_PATH_SIZE];
	FILE *fp;

	snprintf(full_filename, sizeof(full_filename), \
			"%s/data/"SYNC_DIR_NAME"/%s", g_base_path, \
			SYNC_BINLOG_INDEX_FILENAME);
	if ((fp=fopen(full_filename, "wb")) == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if (fprintf(fp, "%d", g_binlog_index) <= 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"write to file \"%s\" fail, " \
			"errno: %d, error info: %s",  \
			__LINE__, full_filename, \
			errno, strerror(errno));
		fclose(fp);
		return errno != 0 ? errno : ENOENT;
	}

	fclose(fp);
	return 0;
}

static char *get_writable_binlog_filename(char *full_filename)
{
	static char buff[MAX_PATH_SIZE];

	if (full_filename == NULL)
	{
		full_filename = buff;
	}

	snprintf(full_filename, MAX_PATH_SIZE, \
			"%s/data/"SYNC_DIR_NAME"/"SYNC_BINLOG_FILE_PREFIX"" \
			SYNC_BINLOG_FILE_EXT_FMT, \
			g_base_path, g_binlog_index);
	return full_filename;
}

static int open_next_writable_binlog()
{
	char full_filename[MAX_PATH_SIZE];

	storage_sync_destroy();

	get_writable_binlog_filename(full_filename);
	if (fileExists(full_filename))
	{
		if (unlink(full_filename) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"unlink file \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, full_filename, \
				errno, strerror(errno));
			return errno != 0 ? errno : ENOENT;
		}

		logError("file: "__FILE__", line: %d, " \
			"binlog file \"%s\" already exists, truncate", \
			__LINE__, full_filename);
	}

	g_fp_binlog = fopen(full_filename, "a");
	if (g_fp_binlog == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	return 0;
}

int storage_sync_init()
{
	char data_path[MAX_PATH_SIZE];
	char sync_path[MAX_PATH_SIZE];
	char full_filename[MAX_PATH_SIZE];
	char file_buff[64];
	int bytes;
	int result;
	FILE *fp;

	snprintf(data_path, sizeof(data_path), \
			"%s/data", g_base_path);
	if (!fileExists(data_path))
	{
		if (mkdir(data_path, 0755) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"mkdir \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, data_path, \
				errno, strerror(errno));
			return errno != 0 ? errno : ENOENT;
		}
	}

	snprintf(sync_path, sizeof(sync_path), \
			"%s/"SYNC_DIR_NAME, data_path);
	if (!fileExists(sync_path))
	{
		if (mkdir(sync_path, 0755) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"mkdir \"%s\" fail, " \
				"errno: %d, error info: %s", \
				__LINE__, sync_path, \
				errno, strerror(errno));
			return errno != 0 ? errno : ENOENT;
		}
	}

	snprintf(full_filename, sizeof(full_filename), \
			"%s/%s", sync_path, SYNC_BINLOG_INDEX_FILENAME);
	if ((fp=fopen(full_filename, "rb")) != NULL)
	{
		if ((bytes=fread(file_buff, 1, sizeof(file_buff)-1, fp)) <= 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"read file \"%s\" fail, bytes read: %d", \
				__LINE__, full_filename, bytes);
			return errno != 0 ? errno : ENOENT;
		}

		file_buff[bytes] = '\0';
		g_binlog_index = atoi(file_buff);
		fclose(fp);
		if (g_binlog_index < 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"in file \"%s\", binlog_index: %d < 0", \
				__LINE__, full_filename, g_binlog_index);
			return EINVAL;
		}
	}
	else
	{
		g_binlog_index = 0;
		if ((result=write_to_binlog_index()) != 0)
		{
			return result;
		}
	}

	get_writable_binlog_filename(full_filename);
	g_fp_binlog = fopen(full_filename, "a");
	if (g_fp_binlog == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"open file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	binlog_file_size = ftell(g_fp_binlog);
	if (binlog_file_size < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"ftell file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		storage_sync_destroy();
		return errno != 0 ? errno : ENOENT;
	}

	/*
	//printf("full_filename=%s, binlog_file_size=%d\n", \
			full_filename, binlog_file_size);
	*/
	
	if ((result=init_pthread_lock(&sync_thread_lock)) != 0)
	{
		return result;
	}

	load_local_host_ip_addrs();

	return 0;
}

int storage_sync_destroy()
{
	if (g_fp_binlog != NULL)
	{
		fclose(g_fp_binlog);
		g_fp_binlog = NULL;
	}

	if (pthread_mutex_destroy(&sync_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_destroy fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}

	return 0;
}

int storage_binlog_write(const char op_type, const char *filename)
{
	int fd;
	struct flock lock;
	int write_bytes;
	int result;

	fd = fileno(g_fp_binlog);
	
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 10;
	if (fcntl(fd, F_SETLKW, &lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"lock binlog file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, get_writable_binlog_filename(NULL), \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}
	
	write_bytes = fprintf(g_fp_binlog, "%d %c %s\n", \
			(int)time(NULL), op_type, filename);
	if (write_bytes <= 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"write to binlog file \"%s\" fail, " \
			"errno: %d, error info: %s",  \
			__LINE__, get_writable_binlog_filename(NULL), \
			errno, strerror(errno));
		result = errno != 0 ? errno : ENOENT;
	}
	else if (fflush(g_fp_binlog) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"sync to binlog file \"%s\" fail, " \
			"errno: %d, error info: %s",  \
			__LINE__, get_writable_binlog_filename(NULL), \
			errno, strerror(errno));
		result = errno != 0 ? errno : ENOENT;
	}
	else
	{
		binlog_file_size += write_bytes;
		if (binlog_file_size >= SYNC_BINLOG_FILE_MAX_SIZE)
		{
			g_binlog_index++;
			if ((result=write_to_binlog_index()) == 0)
			{
				result = open_next_writable_binlog();
			}

			binlog_file_size = 0;
			if (result != 0)
			{
				g_continue_flag = false;
				logError("file: "__FILE__", line: %d, " \
					"open binlog file \"%s\" fail, " \
					"process exit!", \
					__LINE__, \
					get_writable_binlog_filename(NULL));
			}
		}
		else
		{
			result = 0;
		}
	}

	lock.l_type = F_UNLCK;
	if (fcntl(fd, F_SETLKW, &lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"unlock binlog file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, get_writable_binlog_filename(NULL), \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	return result;
}

static char *get_binlog_readable_filename(BinLogReader *pReader, \
		char *full_filename)
{
	static char buff[MAX_PATH_SIZE];

	if (full_filename == NULL)
	{
		full_filename = buff;
	}

	snprintf(full_filename, MAX_PATH_SIZE, \
			"%s/data/"SYNC_DIR_NAME"/"SYNC_BINLOG_FILE_PREFIX"" \
			SYNC_BINLOG_FILE_EXT_FMT, \
			g_base_path, pReader->binlog_index);
	return full_filename;
}

static int storage_open_readable_binlog(BinLogReader *pReader)
{
	char full_filename[MAX_PATH_SIZE];

	if (pReader->binlog_fd >= 0)
	{
		close(pReader->binlog_fd);
	}

	get_binlog_readable_filename(pReader, full_filename);
	pReader->binlog_fd = open(full_filename, O_RDONLY);
	if (pReader->binlog_fd < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"open binlog file \"%s\" fail, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if (pReader->binlog_offset > 0 && \
	    lseek(pReader->binlog_fd, pReader->binlog_offset, SEEK_SET) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"seek binlog file \"%s\" fail, file offset=%d, " \
			"errno: %d, error info: %s", \
			__LINE__, full_filename, pReader->binlog_offset, \
			errno, strerror(errno));

		close(pReader->binlog_fd);
		pReader->binlog_fd = -1;
		return errno != 0 ? errno : ESPIPE;
	}

	return 0;
}

static char *get_mark_filename(const void *pArg, \
			char *full_filename)
{
	const BinLogReader *pReader;
	static char buff[MAX_PATH_SIZE];

	pReader = (const BinLogReader *)pArg;
	if (full_filename == NULL)
	{
		full_filename = buff;
	}

	snprintf(full_filename, MAX_PATH_SIZE, \
			"%s/data/"SYNC_DIR_NAME"/%s_%d%s", g_base_path, \
			pReader->ip_addr, g_server_port, SYNC_MARK_FILE_EXT);
	return full_filename;
}

static int storage_report_storage_status(const char *ip_addr, \
			const char status)
{
	FDFSStorageBrief briefServers[1];
	TrackerServerInfo trackerServer;
	TrackerServerInfo *pGlobalServer;
	TrackerServerInfo *pTServer;
	TrackerServerInfo *pTServerEnd;
	int result;
	int i;

	strcpy(briefServers[0].ip_addr, ip_addr);
	briefServers[0].status = status;

	pTServer = &trackerServer;
	pTServerEnd = g_tracker_servers + g_tracker_server_count;
	for (pGlobalServer=g_tracker_servers; pGlobalServer<pTServerEnd; \
			pGlobalServer++)
	{
		memcpy(pTServer, pGlobalServer, sizeof(TrackerServerInfo));
		for (i=0; i<3; i++)
		{
			pTServer->sock = socket(AF_INET, SOCK_STREAM, 0);
			if(pTServer->sock < 0)
			{
				logError("file: "__FILE__", line: %d, " \
					"socket create failed, errno: %d, " \
					"error info: %s.", \
					__LINE__, errno, strerror(errno));
				result = errno != 0 ? errno : EPERM;
				break;
			}

			if (connectserverbyip(pTServer->sock, \
				pTServer->ip_addr, \
				pTServer->port) == 1)
			{
				break;
			}

			close(pTServer->sock);
			pTServer->sock = -1;
			sleep(10);
		}

		if (pTServer->sock < 0)
		{
			continue;
		}

		if (tracker_report_join(pTServer) != 0)
		{
			close(pTServer->sock);
			continue;
		}

		if ((result=tracker_sync_diff_servers(pTServer, \
			briefServers, 1)) != 0)
		{
		}

		tracker_quit(pTServer);
		close(pTServer->sock);
	}

	return 0;
}

static int storage_reader_sync_init_req(BinLogReader *pReader)
{
	TrackerServerInfo *pTrackerServers;
	TrackerServerInfo *pTServer;
	TrackerServerInfo *pTServerEnd;
	char tracker_client_ip[FDFS_IPADDR_SIZE];
	int result;

	pTrackerServers = (TrackerServerInfo *)malloc( \
		sizeof(TrackerServerInfo) * g_tracker_server_count);
	if (pTrackerServers == NULL)
	{
		return errno != 0 ? errno : ENOMEM;
	}

	memcpy(pTrackerServers, g_tracker_servers, \
		sizeof(TrackerServerInfo) * g_tracker_server_count);
	pTServerEnd = pTrackerServers + g_tracker_server_count;
	for (pTServer=pTrackerServers; pTServer<pTServerEnd; pTServer++)
	{
		pTServer->sock = -1;
	}

	result = EINTR;
	pTServer = pTrackerServers;
	while (1)
	{
		while (g_continue_flag)
		{
			pTServer->sock = socket(AF_INET, SOCK_STREAM, 0);
			if(pTServer->sock < 0)
			{
				logError("file: "__FILE__", line: %d, " \
					"socket create failed, errno: %d, " \
					"error info: %s.", \
					__LINE__, errno, strerror(errno));
				g_continue_flag = false;
				result = errno != 0 ? errno : EPERM;
				break;
			}

			if (connectserverbyip(pTServer->sock, \
				pTServer->ip_addr, \
				pTServer->port) == 1)
			{
				break;
			}

			close(pTServer->sock);

			pTServer++;
			if (pTServer >= pTServerEnd)
			{
				pTServer = pTrackerServers;
			}

			sleep(g_heart_beat_interval);
		}

		if (!g_continue_flag)
		{
			break;
		}

		getSockIpaddr(pTServer->sock, \
				tracker_client_ip, FDFS_IPADDR_SIZE);
		insert_into_local_host_ip(tracker_client_ip);

		/*
		//printf("file: "__FILE__", line: %d, " \
			"tracker_client_ip: %s\n", \
			__LINE__, tracker_client_ip);
		//print_local_host_ip_addrs();
		*/

		if (tracker_report_join(pTServer) != 0)
		{
			close(pTServer->sock);
			sleep(g_heart_beat_interval);
			continue;
		}

		if ((result=tracker_sync_src_req(pTServer, pReader)) != 0)
		{
			tracker_quit(pTServer);
			close(pTServer->sock);
			sleep(g_heart_beat_interval);
			continue;
		}

		tracker_quit(pTServer);
		close(pTServer->sock);

		break;
	}

	/*
	//printf("need_sync_old=%d, until_timestamp=%d\n", \
		pReader->need_sync_old, pReader->until_timestamp);
	*/

	return result;
}

static int storage_reader_init(FDFSStorageBrief *pStorage, \
			BinLogReader *pReader)
{
	char full_filename[MAX_PATH_SIZE];
	IniItemInfo *items;
	int nItemCount;
	int result;
	bool bFileExist;

	memset(pReader, 0, sizeof(BinLogReader));
	pReader->mark_fd = -1;
	pReader->binlog_fd = -1;

	strcpy(pReader->ip_addr, pStorage->ip_addr);
	get_mark_filename(pReader, full_filename);
	bFileExist = fileExists(full_filename);
	if (bFileExist)
	{
		if ((result=iniLoadItems(full_filename, &items, &nItemCount)) \
			 != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"load from mark file \"%s\" fail, " \
				"error code: %d", \
				__LINE__, full_filename, result);
			return result;
		}

		if (nItemCount < 7)
		{
			iniFreeItems(items);
			logError("file: "__FILE__", line: %d, " \
				"in mark file \"%s\", item count: %d < 7", \
				__LINE__, full_filename, nItemCount);
			return ENOENT;
		}

		pReader->binlog_index = iniGetIntValue( \
				MARK_ITEM_BINLOG_FILE_INDEX, \
				items, nItemCount, -1);
		pReader->binlog_offset = iniGetIntValue( \
				MARK_ITEM_BINLOG_FILE_OFFSET, \
				items, nItemCount, -1);
		pReader->need_sync_old = iniGetBoolValue(   \
				MARK_ITEM_NEED_SYNC_OLD, \
				items, nItemCount);
		pReader->sync_old_done = iniGetBoolValue(  \
				MARK_ITEM_SYNC_OLD_DONE, \
				items, nItemCount);
		pReader->until_timestamp = iniGetIntValue( \
				MARK_ITEM_UNTIL_TIMESTAMP, \
				items, nItemCount, -1);
		pReader->scan_row_count = iniGetIntValue( \
				MARK_ITEM_SCAN_ROW_COUNT, \
				items, nItemCount, 0);
		pReader->sync_row_count = iniGetIntValue( \
				MARK_ITEM_SYNC_ROW_COUNT, \
				items, nItemCount, 0);

		if (pReader->binlog_index < 0)
		{
			iniFreeItems(items);
			logError("file: "__FILE__", line: %d, " \
				"in mark file \"%s\", " \
				"binlog_index: %d < 0", \
				__LINE__, full_filename, \
				pReader->binlog_index);
			return EINVAL;
		}
		if (pReader->binlog_offset < 0)
		{
			iniFreeItems(items);
			logError("file: "__FILE__", line: %d, " \
				"in mark file \"%s\", " \
				"binlog_offset: %d < 0", \
				__LINE__, full_filename, \
				pReader->binlog_offset);
			return EINVAL;
		}

		iniFreeItems(items);
	}
	else
	{
		if ((result=storage_reader_sync_init_req(pReader)) != 0)
		{
			return result;
		}
	}

	pReader->last_write_row_count = pReader->scan_row_count;

	pReader->mark_fd = open(full_filename, O_WRONLY | O_CREAT, 0644);
	if (pReader->mark_fd < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"open mark file \"%s\" fail, " \
			"error no: %d, error info: %s", \
			__LINE__, full_filename, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	if ((result=storage_open_readable_binlog(pReader)) != 0)
	{
		close(pReader->mark_fd);
		pReader->mark_fd = -1;
		return result;
	}

	if (!bFileExist)
	{
        	if (!pReader->need_sync_old && pReader->until_timestamp > 0)
		{
			if ((result=storage_binlog_reader_skip(pReader)) != 0)
			{
				storage_reader_destroy(pReader);
				return result;
			}
		}

		if ((result=storage_write_to_mark_file(pReader)) != 0)
		{
			storage_reader_destroy(pReader);
			return result;
		}
	}

	return 0;
}

static void storage_reader_destroy(BinLogReader *pReader)
{
	if (pReader->mark_fd >= 0)
	{
		close(pReader->mark_fd);
		pReader->mark_fd = -1;
	}

	if (pReader->binlog_fd >= 0)
	{
		close(pReader->binlog_fd);
		pReader->binlog_fd = -1;
	}
}

static int storage_write_to_mark_file(BinLogReader *pReader)
{
	char buff[256];
	int len;
	int result;

	len = sprintf(buff, 
		"%s=%d\n"  \
		"%s=%d\n"  \
		"%s=%d\n"  \
		"%s=%d\n"  \
		"%s=%d\n"  \
		"%s=%d\n"  \
		"%s=%d\n", \
		MARK_ITEM_BINLOG_FILE_INDEX, pReader->binlog_index, \
		MARK_ITEM_BINLOG_FILE_OFFSET, pReader->binlog_offset, \
		MARK_ITEM_NEED_SYNC_OLD, pReader->need_sync_old, \
		MARK_ITEM_SYNC_OLD_DONE, pReader->sync_old_done, \
		MARK_ITEM_UNTIL_TIMESTAMP, pReader->until_timestamp, \
		MARK_ITEM_SCAN_ROW_COUNT, pReader->scan_row_count, \
		MARK_ITEM_SYNC_ROW_COUNT, pReader->sync_row_count);
	if ((result=storage_write_to_fd(pReader->mark_fd, get_mark_filename, \
		pReader, buff, len)) == 0)
	{
		pReader->last_write_row_count = pReader->scan_row_count;
	}

	return result;
}

static int rewind_to_prev_rec_end(BinLogReader *pReader, \
			const int record_length)
{
	if (lseek(pReader->binlog_fd, -1 * record_length, \
			SEEK_CUR) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"seek binlog file \"%s\"fail, " \
			"file offset: %d, " \
			"errno: %d, error info: %s", \
			__LINE__, get_binlog_readable_filename(pReader, NULL), \
			pReader->binlog_offset, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOENT;
	}

	return 0;
}

static int storage_binlog_read(BinLogReader *pReader, \
			BinLogRecord *pRecord, int *record_length)
{
	char line[256];
	char *cols[3];
	int result;

	while (1)
	{
		if ((*record_length=fd_gets(pReader->binlog_fd, line, \
			sizeof(line), 38)) < 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"read a line from binlog file \"%s\" fail, " \
				"file offset: %d, " \
				"error no: %d, error info: %s", \
				__LINE__, \
				get_binlog_readable_filename(pReader, NULL), \
				pReader->binlog_offset, errno, strerror(errno));
			return errno != 0 ? errno : ENOENT;
		}

		if (*record_length == 0)
		{
			if (pReader->binlog_index < g_binlog_index) //rotate
			{
				pReader->binlog_index++;
				pReader->binlog_offset = 0;
				if ((result=storage_open_readable_binlog( \
						pReader)) != 0)
				{
					return result;
				}

				if ((result=storage_write_to_mark_file( \
						pReader)) != 0)
				{
					return result;
				}

				continue;  //read next binlog
			}

			return ENOENT;
		}

		break;
	}

	if (line[*record_length-1] != '\n')
	{
		if ((result=rewind_to_prev_rec_end(pReader, \
				*record_length)) != 0)
		{
			return result;
		}

		logError("file: "__FILE__", line: %d, " \
			"get a line from binlog file \"%s\" fail, " \
			"file offset: %d, " \
			"no new line char, line length: %d", \
			__LINE__, get_binlog_readable_filename(pReader, NULL), \
			pReader->binlog_offset, *record_length);
		return ENOENT;
	}

	if ((result=splitEx(line, ' ', cols, 3)) < 3)
	{
		logError("file: "__FILE__", line: %d, " \
			"read data from binlog file \"%s\" fail, " \
			"file offset: %d, " \
			"read item count: %d < 3", \
			__LINE__, get_binlog_readable_filename(pReader, NULL), \
			pReader->binlog_offset, result);
		return ENOENT;
	}

	pRecord->timestamp = atoi(cols[0]);
	pRecord->op_type = *(cols[1]);
	pRecord->filename_len = strlen(cols[2]) - 1; //need trim new line \n
	if (pRecord->filename_len > sizeof(pRecord->filename)-1)
	{
		logError("file: "__FILE__", line: %d, " \
			"item \"filename\" in binlog " \
			"file \"%s\" is invalid, file offset: %d, " \
			"filename length: %d > %d", \
			__LINE__, get_binlog_readable_filename(pReader, NULL), \
			pReader->binlog_offset, \
			pRecord->filename_len, sizeof(pRecord->filename)-1);
		return EINVAL;
	}

	memcpy(pRecord->filename, cols[2], pRecord->filename_len);
	pRecord->filename[pRecord->filename_len] = '\0';

	/*
	//printf("timestamp=%d, op_type=%c, filename=%s(%d), line length=%d, " \
		"offset=%d\n", \
		pRecord->timestamp, pRecord->op_type, \
		pRecord->filename, strlen(pRecord->filename), \
		*record_length, pReader->binlog_offset);
	*/

	return 0;
}


static int storage_binlog_reader_skip(BinLogReader *pReader)
{
	BinLogRecord record;
	int result;
	int record_len;

	while (1)
	{
		result = storage_binlog_read(pReader, \
				&record, &record_len);
		if (result != 0)
		{
			if (result == ENOENT)
			{
				return 0;
			}

			return result;
		}

		if (record.timestamp >= pReader->until_timestamp)
		{
			result = rewind_to_prev_rec_end( \
					pReader, record_len);
			break;
		}

		pReader->binlog_offset += record_len;
	}

	return result;
}

static void* storage_sync_thread_entrance(void* arg)
{
	FDFSStorageBrief *pStorage;
	BinLogReader reader;
	BinLogRecord record;
	TrackerServerInfo storage_server;
	char local_ip_addr[FDFS_IPADDR_SIZE];
	int result;
	int record_len;

	memset(local_ip_addr, 0, sizeof(local_ip_addr));
	memset(&reader, 0, sizeof(reader));

	pStorage = (FDFSStorageBrief *)arg;

	strcpy(storage_server.ip_addr, pStorage->ip_addr);
	strcpy(storage_server.group_name, g_group_name);
	storage_server.port = g_server_port;
	storage_server.sock = -1;
	while (g_continue_flag)
	{
		if (storage_reader_init(pStorage, &reader) != 0)
		{
			g_continue_flag = false;
			break;
		}

		while (g_continue_flag && \
			(pStorage->status != FDFS_STORAGE_STATUS_ACTIVE && \
			pStorage->status != FDFS_STORAGE_STATUS_WAIT_SYNC && \
			pStorage->status != FDFS_STORAGE_STATUS_SYNCING))
		{
			sleep(10);
		}

		while (g_continue_flag)
		{
			storage_server.sock = \
				socket(AF_INET, SOCK_STREAM, 0);
			if(storage_server.sock < 0)
			{
				logError("file: "__FILE__", line: %d," \
					" socket create failed, " \
					"errno: %d, error info: %s.", \
					__LINE__, \
					errno, strerror(errno));
				g_continue_flag = false;
				break;
			}

			if (connectserverbyip(storage_server.sock, \
				storage_server.ip_addr, \
				g_server_port) == 1)
			{
				break;
			}

			sleep(5);
			close(storage_server.sock);
			storage_server.sock = -1;
		}

		if (!g_continue_flag)
		{
			break;
		}

		getSockIpaddr(storage_server.sock, \
			local_ip_addr, FDFS_IPADDR_SIZE);

		/*
		//printf("file: "__FILE__", line: %d, " \
			"storage_server.ip_addr=%s, " \
			"local_ip_addr: %s\n", \
			__LINE__, storage_server.ip_addr, local_ip_addr);
		*/

		if (strcmp(local_ip_addr, storage_server.ip_addr) == 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"ip_addr %s belong to the local host," \
				" sync thread exit.", \
				__LINE__, storage_server.ip_addr);
			tracker_quit(&storage_server);
			break;
		}

		if (pStorage->status == FDFS_STORAGE_STATUS_WAIT_SYNC)
		{
			pStorage->status = FDFS_STORAGE_STATUS_SYNCING;
			storage_report_storage_status(pStorage->ip_addr, \
				pStorage->status);
		}
		if (pStorage->status == FDFS_STORAGE_STATUS_SYNCING)
		{
			if (reader.need_sync_old && reader.sync_old_done)
			{
				pStorage->status = FDFS_STORAGE_STATUS_ONLINE;
				storage_report_storage_status(  \
					pStorage->ip_addr, \
					pStorage->status);
			}
		}

		while (g_continue_flag)
		{
			result = storage_binlog_read(&reader, \
					&record, &record_len);
			if (result == ENOENT)
			{
				if (reader.need_sync_old && \
					!reader.sync_old_done)
				{
				reader.sync_old_done = true;
				result = storage_write_to_mark_file(&reader);
				if (result != 0)
				{
					g_continue_flag = false;
					break;
				}

				if (pStorage->status == \
					FDFS_STORAGE_STATUS_SYNCING)
				{
					pStorage->status = \
						FDFS_STORAGE_STATUS_ONLINE;
					storage_report_storage_status(  \
						pStorage->ip_addr, \
						pStorage->status);
				}
				}

				usleep(g_sync_wait_usec);
				continue;
			}
			else if (result != 0)
			{
				g_continue_flag = false;
				break;
			}

			if ((result=storage_sync_data(&reader, \
				&storage_server, &record)) != 0)
			{
				if ((result=rewind_to_prev_rec_end( \
					&reader, record_len)) != 0)
				{
					g_continue_flag = false;
				}

				break;
			}

			reader.binlog_offset += record_len;
			if (++reader.scan_row_count % 100 == 0)
			{
				result = storage_write_to_mark_file( \
					&reader);
				if (result != 0)
				{
					g_continue_flag = false;
					break;
				}
			}
		}

		if (reader.last_write_row_count != \
			reader.scan_row_count)
		{
			if ((result=storage_write_to_mark_file( \
					&reader)) != 0)
			{
				g_continue_flag = false;
				break;
			}
		}

		close(storage_server.sock);
		storage_server.sock = -1;
		storage_reader_destroy(&reader);

		if (!g_continue_flag)
		{
			break;
		}

		sleep(60);
	}

	if (storage_server.sock >= 0)
	{
		close(storage_server.sock);
	}
	storage_reader_destroy(&reader);

	if (pthread_mutex_lock(&sync_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
	}
	g_storage_sync_thread_count--;
	if (pthread_mutex_unlock(&sync_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
	}

	return NULL;
}

int storage_sync_thread_start(const FDFSStorageBrief *pStorage)
{
	pthread_attr_t pattr;
	pthread_t tid;

	if (is_local_host_ip(pStorage->ip_addr)) //can't self sync to self
	{
		return 0;
	}

	pthread_attr_init(&pattr);
	pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);

	/*
	//printf("start storage ip_addr: %s, g_storage_sync_thread_count=%d\n", \
			pStorage->ip_addr, g_storage_sync_thread_count);
	*/

	if (pthread_create(&tid, &pattr, storage_sync_thread_entrance, \
		(void *)pStorage) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"create thread failed, errno: %d, " \
			"error info: %s", \
			__LINE__, errno, strerror(errno));
		return errno != 0 ? errno : EAGAIN;
	}

	if (pthread_mutex_lock(&sync_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_lock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
	}
	g_storage_sync_thread_count++;
	if (pthread_mutex_unlock(&sync_thread_lock) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call pthread_mutex_unlock fail, " \
			"errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
	}

	pthread_attr_destroy(&pattr);

	return 0;
}

