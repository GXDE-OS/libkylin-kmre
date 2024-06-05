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

#ifndef __KMRE_SOCKET_H__
#define __KMRE_SOCKET_H__
//#pragma once

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <linux/limits.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>

namespace KmreSocket {

int connect_socket(const char *container_socket_file);
int write_fully(int fd, const void *buffer, size_t size);
ssize_t set_timeout(int fd, int send_timeout, int rcv_timeout);
ssize_t read_buf(int fd, void *buf, size_t len);
//ssize_t read_buf_with_timeout(int fd, void *buf, size_t len, int secs);

}

#endif // __KMRE_SOCKET_H__
