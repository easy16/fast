/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "fdfs_client.h"
#include "fdfs_global.h"
#include "fdfs_base64.h"

int main(int argc, char *argv[])
{
	char *conf_filename;
	char *local_filename;
	TrackerServerInfo *pTrackerServer;
	int result;
	TrackerServerInfo storageServer;
	char group_name[FDFS_GROUP_NAME_MAX_LEN + 1];
	char remote_filename[256];
	FDFSMetaData meta_list[32];
	int meta_count;
	int i;
	FDFSMetaData *pMetaList;
	char buff[13];
	int len;
        char *file_buff;
	int file_size;
	char *operation;
	char *meta_buff;

	base64_init_ex(0, '.', '_', '-');
	printf("This is FastDFS client test program v%d.%d\n" \
"\nCopyright (C) 2008, Happy Fish / YuQing\n" \
"\nFastDFS may be copied only under the terms of the GNU General\n" \
"Public License V3, which may be found in the FastDFS source kit.\n" \
"Please visit the FastDFS Home Page http://www.csource.org/ \n" \
"for more detail.\n\n" \
, g_version.major, g_version.minor);

	if (argc < 3)
	{
		printf("Usage: %s <config_file> <operation>\n" \
			"\toperation: upload, download, getmeta, setmeta " \
			"and delete\n", argv[0]);
		return 1;
	}

	conf_filename = argv[1];
	operation = argv[2];
	if ((result=fdfs_client_init(conf_filename)) != 0)
	{
		return result;
	}

	pTrackerServer = tracker_get_connection();
	if (pTrackerServer == NULL)
	{
		fdfs_client_destroy();
		return errno != 0 ? errno : ECONNREFUSED;
	}

	if (strcmp(operation, "upload") == 0)
	{
		if (argc < 4)
		{
			printf("Usage: %s <config_file> upload " \
				"<local_filename>\n", argv[0]);
			fdfs_client_destroy();
			return EINVAL;
		}

		local_filename = argv[3];
		if ((result=tracker_query_storage_store(pTrackerServer, \
		                &storageServer)) != 0)
		{
			fdfs_client_destroy();
			printf("tracker_query_storage fail, " \
				"error no: %d, error info: %s\n", \
				result, strerror(result));
			return result;
		}

		printf("group_name=%s, ip_addr=%s, port=%d\n", \
			storageServer.group_name, \
			storageServer.ip_addr, \
			storageServer.port);

		if ((result=tracker_connect_server(&storageServer)) != 0)
		{
			fdfs_client_destroy();
			return result;
		}

		memset(&meta_list, 0, sizeof(meta_list));
		meta_count = 0;
		strcpy(meta_list[meta_count].name, "ext_name");
		strcpy(meta_list[meta_count].value, "jpg");
		meta_count++;
		strcpy(meta_list[meta_count].name, "width");
		strcpy(meta_list[meta_count].value, "160");
		meta_count++;
		strcpy(meta_list[meta_count].name, "height");
		strcpy(meta_list[meta_count].value, "80");
		meta_count++;
		strcpy(meta_list[meta_count].name, "file_size");
		strcpy(meta_list[meta_count].value, "115120");
		meta_count++;
		result = storage_upload_by_filename(pTrackerServer, \
				&storageServer, local_filename, \
				meta_list, meta_count, \
				group_name, remote_filename);
		if (result != 0)
		{
			printf("storage_upload_by_filename fail, " \
				"error no: %d, error info: %s\n", \
				result, strerror(result));
			tracker_quit(&storageServer);
			tracker_disconnect_server(&storageServer);
			fdfs_client_destroy();
			return result;
		}

		memset(buff, 0, sizeof(buff));
		base64_decode(remote_filename + 6, strlen(remote_filename) - 6, buff, &len);
		printf("group_name=%s, remote_filename=%s\n", \
			group_name, remote_filename);
		printf("file timestamp=%d\n", buff2int(buff));
		printf("file size=%d\n", buff2int(buff+4));

	}
	else if (strcmp(operation, "download") == 0 || 
		strcmp(operation, "getmeta") == 0 ||
		strcmp(operation, "setmeta") == 0 ||
		strcmp(operation, "delete") == 0)
	{
		if (argc < 5)
		{
			printf("Usage: %s <config_file> %s " \
				"<group_name> <remote_filename>\n", \
				argv[0], operation);
			fdfs_client_destroy();
			return EINVAL;
		}

		snprintf(group_name, sizeof(group_name), "%s", argv[3]);
		snprintf(remote_filename, sizeof(remote_filename), \
				"%s", argv[4]);
		if ((result=tracker_query_storage_fetch(pTrackerServer, \
       	       		&storageServer, group_name, remote_filename)) != 0)
		{
			fdfs_client_destroy();
			printf("tracker_query_storage_fetch fail, " \
				"group_name=%s, filename=%s, " \
				"error no: %d, error info: %s\n", \
				group_name, remote_filename, \
				result, strerror(result));
			return result;
		}

		printf("storage=%s:%d\n", storageServer.ip_addr, \
			storageServer.port);

		if ((result=tracker_connect_server(&storageServer)) != 0)
		{
			fdfs_client_destroy();
			return result;
		}

		if (strcmp(operation, "download") == 0)
		{
			if ((result=storage_download_file(pTrackerServer, \
				&storageServer, group_name, remote_filename, \
				&file_buff, &file_size)) == 0)
			{
				if (argc >= 6)
				{
					local_filename = argv[5];
				}
				else
				{
					local_filename = strrchr( \
							remote_filename, '/');
					if (local_filename != NULL)
					{
						local_filename++;  //skip /
					}
					else
					{
						local_filename=remote_filename;
					}
				}

				if (writeToFile(local_filename, file_buff, \
						file_size) == 0)
				{
					printf("download file success, " \
						"file size=%d, " \
						"file save to %s\n", \
						 file_size, local_filename);
				}
				free(file_buff);
			}
			else
			{
				printf("download file fail, " \
					"error no: %d, error info: %s\n", \
					result, strerror(result));
			}
		}
		else if (strcmp(operation, "getmeta") == 0)
		{
			if ((result=storage_get_metadata(pTrackerServer, \
				&storageServer, group_name, remote_filename, \
				&pMetaList, &meta_count)) == 0)
			{
				printf("get meta data success, " \
					"meta count=%d\n", meta_count);
				for (i=0; i<meta_count; i++)
				{
					printf("%s=%s\n", \
						pMetaList[i].name, \
						pMetaList[i].value);
				}

				free(pMetaList);
			}
			else
			{
				printf("getmeta fail, " \
					"error no: %d, error info: %s\n", \
					result, strerror(result));
			}
		}
		else if (strcmp(operation, "setmeta") == 0)
		{
			if (argc < 7)
			{
				printf("Usage: %s <config_file> %s " \
					"<group_name> <remote_filename> " \
					"<op_flag> <metadata_list>\n" \
					"\top_flag: %c for overwrite, " \
					"%c for merge\n" \
					"\tmetadata_list: name1=value1," \
					"name2=value2,...\n", \
					argv[0], operation, \
					STORAGE_SET_METADATA_FLAG_OVERWRITE, \
					STORAGE_SET_METADATA_FLAG_MERGE);
				fdfs_client_destroy();
				return EINVAL;
			}

			meta_buff = strdup(argv[6]);
			if (meta_buff == NULL)
			{
				printf("Out of memory!\n");
				return ENOMEM;
			}

			pMetaList = fdfs_split_metadata_ex(meta_buff, \
					',', '=', &meta_count, &result);
			if (pMetaList == NULL)
			{
				printf("Out of memory!\n");
				free(meta_buff);
				return ENOMEM;
			}

			if ((result=storage_set_metadata(pTrackerServer, \
				&storageServer, group_name, remote_filename, \
				pMetaList, meta_count, *argv[5])) == 0)
			{
				printf("set meta data success\n");
			}
			else
			{
				printf("setmeta fail, " \
					"error no: %d, error info: %s\n", \
					result, strerror(result));
			}

			free(meta_buff);
			free(pMetaList);
		}
		else if(strcmp(operation, "delete") == 0)
		{
			if ((result=storage_delete_file(pTrackerServer, \
			&storageServer, group_name, remote_filename)) == 0)
			{
				printf("delete file success\n");
			}
			else
			{
				printf("delete file fail, " \
					"error no: %d, error info: %s\n", \
					result, strerror(result));
			}
		}
	}
	else
	{
		fdfs_client_destroy();
		printf("invalid operation: %s\n", operation);
		return EINVAL;
	}

	tracker_quit(&storageServer);
	tracker_disconnect_server(&storageServer);

	tracker_quit(pTrackerServer);

	tracker_close_all_connections();
	fdfs_client_destroy();

	return result;
}

