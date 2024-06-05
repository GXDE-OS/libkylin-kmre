/*
 * Copyright (c) KylinSoft Co., Ltd. 2016-2024.All rights reserved.
 *
 * Authors:
 *  Kobe Lee    lixiang@kylinos.cn
 *  Alan Xie    xiehuijun@kylinos.cn
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kmre_socket.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/syslog.h>

namespace KmreSocket {

int connect_socket(const char *container_socket_file)
{
    int fd, len, err, rval;
    struct sockaddr_un un;

    if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "[libkylin-kmre][%s] socket error: %s, %d\n", __func__, strerror(errno), errno);
        return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;

    strncpy(un.sun_path, container_socket_file, strlen(container_socket_file));
    len = offsetof(struct sockaddr_un, sun_path) + strlen(un.sun_path);

    if(connect(fd, (struct sockaddr*)&un, len) < 0)
    {
        syslog(LOG_ERR, "[libkylin-kmre][%s] connect error: %s, %d\n", __func__, strerror(errno), errno);
        close(fd);
        return -1;
    }

    return fd;
}

int write_fully(int fd, const void *buffer, size_t size)
{

    size_t res = size;
    int retval = 0;

    while (res > 0) {
        ssize_t stat = send(fd, (const char *)buffer + (size - res), res, MSG_NOSIGNAL);
        if (stat < 0) {
            if (errno != EINTR) {
                retval =  stat;
                break;
            }
        } else {
            res -= stat;
        }
    }

    return retval;
}

ssize_t read_buf(int fd, void *buf, size_t len)
{
    if (!buf) {
        return -1;  // do not allow NULL buf in that implementation
    }

    ssize_t stat = recv(fd, (char *)(buf), len, MSG_WAITALL);
    if (stat > 0) {
        return stat;
    }

    syslog(LOG_ERR, "[libkylin-kmre][%s] read buf failed!\n", __func__);
    return -1;
}

ssize_t set_timeout(int fd, int send_timeout, int rcv_timeout)
{
    struct timeval timeout = {0,0};

    timeout.tv_sec = send_timeout;
    int iRes = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(struct timeval));
    if (iRes != 0) {
        syslog(LOG_ERR, "[libkylin-kmre][%s] Set send timeout failed! iRes=%d,error: %s(errno: %d)\n", 
            __func__, iRes, strerror(errno), errno);
    }

    timeout.tv_sec = rcv_timeout;
    iRes = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    if (iRes != 0) {
        syslog(LOG_ERR, "[libkylin-kmre][%s] Set rcv timeout failed! iRes=%d,error: %s(errno: %d)\n", 
            __func__, iRes, strerror(errno), errno);
    }

    return iRes;
}

ssize_t read_buf_with_timeout(int fd, void *buf, size_t len, int secs)
{
    if (!buf) {
        return -1;  // do not allow NULL buf in that implementation
    }

    //int timeoutsecs = 25;
    int timeoutsecs = secs;
    struct timeval timeout = {0,0};
    timeout.tv_sec = timeoutsecs;
    int iRes = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    if (iRes != 0) {
        syslog(LOG_ERR, "[libkylin-kmre][%s] setsockopt failed! iRes=%d,error: %s(errno: %d)\n", 
            __func__, iRes, strerror(errno), errno);
        ssize_t stat = recv(fd, (char *)(buf), len, 0);
        if (stat > 0) {
            return stat;
        }
    }
    else {
        ssize_t stat = recv(fd, (char *)(buf), len, MSG_WAITALL);
        if (stat > 0) {
            return stat;
        }
    }

    syslog(LOG_ERR, "[libkylin-kmre][%s] read buf failed!\n", __func__);
    return -1;
}

}
