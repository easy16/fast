/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "fdfs_define.h"
#include "shared_func.h"
#include "logger.h"
#include "fdfs_global.h"
#include "sockopt.h"
#include "tracker_types.h"
#include "tracker_proto.h"

int tracker_recv_response(TrackerServerInfo *pTrackerServer, \
		char **buff, const int buff_size, \
		int *in_bytes)
{
	TrackerHeader resp;
	bool bMalloced;

	if (tcprecvdata(pTrackerServer->sock, &resp, \
		sizeof(resp), g_network_timeout) != 1)
	{
		logError("file: "__FILE__", line: %d, " \
			"server: %s:%d, recv data fail, " \
			"errno: %d, error info: %s", \
			__LINE__, pTrackerServer->ip_addr, \
			pTrackerServer->port, \
			errno, strerror(errno));
		*in_bytes = 0;
		return errno != 0 ? errno : EPIPE;
	}

	if (resp.status != 0)
	{
		*in_bytes = 0;
		return resp.status;
	}

	resp.pkg_len[TRACKER_PROTO_PKG_LEN_SIZE-1] = '\0';
	*in_bytes = strtol(resp.pkg_len, NULL, 16);
	if (*in_bytes < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"server: %s:%d, recv package size %d " \
			"is not correct", \
			__LINE__, pTrackerServer->ip_addr, \
			pTrackerServer->port, *in_bytes);
		*in_bytes = 0;
		return errno != 0 ? errno : EINVAL;
	}
	if (*in_bytes == 0)
	{
		return resp.status;
	}

	if (*buff == NULL)
	{
		*buff = (char *)malloc((*in_bytes) + 1);
		if (*buff == NULL)
		{
			*in_bytes = 0;
			return errno != 0 ? errno : ENOMEM;
		}

		bMalloced = true;
	}
	else 
	{
		if (*in_bytes > buff_size)
		{
			logError("file: "__FILE__", line: %d, " \
				"server: %s:%d, recv body bytes: %d" \
				" exceed max: %d", \
				__LINE__, pTrackerServer->ip_addr, \
				pTrackerServer->port, *in_bytes, buff_size);
			*in_bytes = 0;
			return errno != 0 ? errno : EINVAL;
		}

		bMalloced = false;
	}

	if (tcprecvdata(pTrackerServer->sock, *buff, \
		*in_bytes, g_network_timeout) != 1)
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server: %s:%d, recv data fail, " \
			"errno: %d, error info: %s", \
			__LINE__, pTrackerServer->ip_addr, \
			pTrackerServer->port, \
			errno, strerror(errno));
		*in_bytes = 0;
		if (bMalloced)
		{
			free(*buff);
			*buff = NULL;
		}
		return errno != 0 ? errno : EPIPE;
	}

	return resp.status;
}

int tracker_quit(TrackerServerInfo *pTrackerServer)
{
	TrackerHeader header;
	int result;

	header.pkg_len[0] = '0';
	header.pkg_len[1] = '\0';
	header.cmd = TRACKER_PROTO_CMD_STORAGE_QUIT;
	header.status = 0;
	result = tcpsenddata(pTrackerServer->sock, &header, sizeof(header), \
				g_network_timeout);
	if(result != 1)
	{
		logError("file: "__FILE__", line: %d, " \
			"tracker server ip: %s, send data fail, " \
			"errno: %d, error info: %s", \
			__LINE__, pTrackerServer->ip_addr, \
			errno, strerror(errno));
		return errno != 0 ? errno : EPIPE;
	}

	return 0;
}

int tracker_validate_group_name(const char *group_name)
{
	const char *p;
	const char *pEnd;
	int len;

	len = strlen(group_name);
	if (len == 0)
	{
		return EINVAL;
	}

	pEnd = group_name + len;
	for (p=group_name; p<pEnd; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || \
			(*p >= '0' && *p <= '9')))
		{
			return EINVAL;
		}
	}

	return 0;
}

int metadata_cmp_by_name(const void *p1, const void *p2)
{
	return strcmp(((FDFSMetaData *)p1)->name, ((FDFSMetaData *)p2)->name);
}

const char *get_storage_status_caption(const int status)
{
	switch (status)
	{
		case FDFS_STORAGE_STATUS_INIT:
			return "INIT";
		case FDFS_STORAGE_STATUS_WAIT_SYNC:
			return "WAIT_SYNC";
		case FDFS_STORAGE_STATUS_SYNCING:
			return "SYNCING";
		case FDFS_STORAGE_STATUS_OFFLINE:
			return "OFFLINE";
		case FDFS_STORAGE_STATUS_ONLINE:
			return "ONLINE";
		case FDFS_STORAGE_STATUS_DEACTIVE:
			return "DEACTIVE";
		case FDFS_STORAGE_STATUS_ACTIVE:
			return "ACTIVE";
		default:
			return "UNKOWN";
	}
}

FDFSMetaData *fdfs_split_metadata_ex(char *meta_buff, \
		const char recordSeperator, const char filedSeperator, \
		int *meta_count, int *err_no)
{
	char **rows;
	char **ppRow;
	char **ppEnd;
	FDFSMetaData *meta_list;
	FDFSMetaData *pMetadata;
	char *pSeperator;
	int nNameLen;
	int nValueLen;

	*meta_count = getOccurCount(meta_buff, recordSeperator) + 1;
	meta_list = (FDFSMetaData *)malloc(sizeof(FDFSMetaData) * \
						(*meta_count));
	if (meta_list == NULL)
	{
		*meta_count = 0;
		*err_no = ENOMEM;
		return NULL;
	}

	rows = (char **)malloc(sizeof(char *) * (*meta_count));
	if (rows == NULL)
	{
		free(meta_list);
		*meta_count = 0;
		*err_no = ENOMEM;
		return NULL;
	}

	*meta_count = splitEx(meta_buff, recordSeperator, \
				rows, *meta_count);
	ppEnd = rows + (*meta_count);
	pMetadata = meta_list;
	for (ppRow=rows; ppRow<ppEnd; ppRow++)
	{
		pSeperator = strchr(*ppRow, filedSeperator);
		if (pSeperator == NULL)
		{
			continue;
		}

		nNameLen = pSeperator - (*ppRow);
		nValueLen = strlen(pSeperator+1);
		if (nNameLen > FDFS_MAX_META_NAME_LEN)
		{
			nNameLen = FDFS_MAX_META_NAME_LEN;
		}
		if (nValueLen > FDFS_MAX_META_VALUE_LEN)
		{
			nValueLen = FDFS_MAX_META_VALUE_LEN;
		}

		memcpy(pMetadata->name, *ppRow, nNameLen);
		memcpy(pMetadata->value, pSeperator+1, nValueLen);
		pMetadata->name[nNameLen] = '\0';
		pMetadata->value[nValueLen] = '\0';

		pMetadata++;
	}

	*meta_count = pMetadata - meta_list;
	free(rows);

	*err_no = 0;
	return meta_list;
}

char *fdfs_pack_metadata(const FDFSMetaData *meta_list, const int meta_count, \
			char *meta_buff, int *buff_bytes)
{
	const FDFSMetaData *pMetaCurr;
	const FDFSMetaData *pMetaEnd;
	char *p;
	int name_len;
	int value_len;

	if (meta_buff == NULL)
	{
		meta_buff = (char *)malloc(sizeof(FDFSMetaData) * meta_count);
		if (meta_buff == NULL)
		{
			*buff_bytes = 0;
			return NULL;
		}
	}

	p = meta_buff;
	pMetaEnd = meta_list + meta_count;
	for (pMetaCurr=meta_list; pMetaCurr<pMetaEnd; pMetaCurr++)
	{
		name_len = strlen(pMetaCurr->name);
		value_len = strlen(pMetaCurr->value);
		memcpy(p, pMetaCurr->name, name_len);
		p += name_len;
		*p++ = FDFS_FIELD_SEPERATOR;
		memcpy(p, pMetaCurr->value, value_len);
		p += value_len;
		*p++ = FDFS_RECORD_SEPERATOR;
	}

	*(--p) = '\0'; //omit the last record seperator
	*buff_bytes = p - meta_buff;
	return meta_buff;
}

