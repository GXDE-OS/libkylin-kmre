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

#include <pthread.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <string.h>
#include <pwd.h>
#include <sys/syslog.h>

#include "KmreCore.pb.h"
#include "kmre_socket.h"

using namespace std;
using namespace KmreSocket;

namespace KmreSocket {

//去掉字符串中间的空格
static char *remove_space(char *str)
{
    char *tail = str;
    char *next = str;
    while(*next) {
        if (*next != ' ') {
            if (tail < next) {
                *tail = *next;
            }
            tail ++;
        }
        next ++;
    }
    *tail = '\0';

    return str;
}

static bool file_is_exists(const char *filepath)
{
    struct stat statbuf;
    return stat(filepath, &statbuf) == 0;
}

static bool delete_local_file(const char *filepath)
{
    if (file_is_exists(filepath)) {
        remove(filepath);
    }
    return true;
}

//删除软件的desktoop文件和icon
static bool delete_desktop_and_icon(const char *pkgname)
{
    char *homedir = getenv("HOME");
    int r;
    if (homedir) {
        char desktop_file[512] = {0};
        r = sprintf(desktop_file, "%s/.local/share/applications/%s.desktop", homedir, pkgname);
        if (r >= 0) {
            delete_local_file(desktop_file);
        }

        char svg_icon_file[512] = {0};
        r = sprintf(svg_icon_file, "%s/.local/share/icons/%s.svg", homedir, pkgname);
        if (r >= 0) {
            delete_local_file(svg_icon_file);
        }

        char png_icon_file[512] = {0};
        r = sprintf(png_icon_file, "%s/.local/share/icons/%s.png", homedir, pkgname);
        if (r >= 0) {
            delete_local_file(png_icon_file);
        }
    }
    return true;
}


static int isInCpuinfo(const char *fmt, const char *str)
{
    FILE *cpuinfo;
    char field[256];
    char format[256];
    int found = 0;

    sprintf(format, "%s : %s", fmt, "%255s");

    cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo) {
        do {
            if (fscanf(cpuinfo, format, field) == 1) {
                if (strncmp(field, str, strlen(str)) == 0)
                    found = 1;
                break;
            }
        } while (fgets(field, 256, cpuinfo));
        fclose(cpuinfo);
    }

    return found;
}

static int strInCpuinfo(const char *str)
{
    FILE *fp;
    const char *filename;
    char linebuf[128];
    int found = 0;

    if ((fp = fopen(filename = "/proc/cpuinfo", "r")) == NULL) {
        return found;
    }

    while ((fgets(linebuf, sizeof(linebuf) - 1, fp)) != NULL) {
        if (strstr(linebuf, str)) {
            found = 1;
            break;
        }
    }
    if (fclose(fp) != 0) {
        perror(filename);
    }

    return found;
}

static std::string get_user_name()
{
    std::string user_name = "";
    struct passwd  pwd;
    struct passwd *result = nullptr;
    char *buf = nullptr;

    int bufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (bufSize == -1) {
        bufSize = 16384;
    }
    buf = new char[bufSize]();

    getpwuid_r(getuid(), &pwd, buf, bufSize, &result);
    if (result && pwd.pw_name) {
        user_name = pwd.pw_name;
    }
    else {
        syslog(LOG_ERR, "[libkylin-kmre][%s] getpwuid_r error!", __func__);

        user_name = std::getenv("USER");
        if (user_name.empty()) {
            user_name = std::getenv("USERNAME");
        }
        if (user_name.empty()) {
            char name[16];
            snprintf(name, sizeof(name), "%u", getuid());
            user_name = std::string(name);
        }
    }

    if (buf) {
        delete[] buf;
    }

    return user_name;
}

static std::string get_uid()
{
    uid_t uid = getuid();
    static char uidstr[12] = {0};
    snprintf(uidstr, sizeof(uidstr), "%d", uid);

    return std::string(uidstr);
}

typedef enum {
    eLink_Launcher = 0,
    eLink_Manager,
}SocketLink;

#define BUF_SIZE 2048
#define LAUNCHER_SOCKET_LOCK_FILE "/tmp/.kmre_launcher_socket.lock"
#define MANAGER_SOCKET_LOCK_FILE "/tmp/.kmre_manager_socket.lock"

static std::string convertUserNameToPath(const std::string& userName)
{
    char buffer[BUF_SIZE] = {0};
    std::string path = userName;
    unsigned int i = 0;
    const char* str = nullptr;

    str = userName.c_str();
    if (str && strstr(str, "\\")) {
        snprintf(buffer, sizeof(buffer), "%s", str);
        for (i = 0; i < sizeof(buffer); ++i) {
            if ('\0' == buffer[i]) {
                break;
            }

            if ('\\' == buffer[i]) {
                buffer[i] = '_';
            }
        }

        path = buffer;
    }

    return path;
}

template <typename T, typename R = cn::kylinos::kmre::kmrecore::ActionResult>
class ConnectSocket
{
public:
    ConnectSocket(SocketLink link) {
        switch (link) {
        case eLink_Launcher: {
            mSocketPath = "/var/lib/kmre/kmre-" + get_uid() + "-" + convertUserNameToPath(get_user_name()) + "/sockets/kmre_launcher";
        }break;
        case eLink_Manager: {
            mSocketPath = "/var/lib/kmre/kmre-" + get_uid() + "-" + convertUserNameToPath(get_user_name()) + "/sockets/kmre_manager";
        }break;
        default:break;
        }
    }

    ~ConnectSocket() {
        if (mSocketFd > 0) {
            close(mSocketFd);
        }
        mSocketFd = -1;
    }

    bool connect() {
        if (!file_is_exists(mSocketPath.c_str())) {
            syslog(LOG_ERR, "[%s] Can't find socket file:'%s'!", __func__, mSocketPath.c_str());
            return false;
        }

        mSocketFd = connect_socket(mSocketPath.c_str());
        if (mSocketFd < 0) {
            syslog(LOG_ERR, "[%s] Create socket:'%s' or connect server failed!", __func__, mSocketPath.c_str());
            return false;
        }
        return true;
    }

    bool setTimeout(int sendTimeout = 2, int rcvTimeout = 2) {// default timeout: 2s
        if (mSocketFd < 0) {
            syslog(LOG_ERR, "[%s] Invalid socket fd!", __func__); 
            return false;
        }
        
        if (set_timeout(mSocketFd, sendTimeout, rcvTimeout) != 0) {
            syslog(LOG_ERR, "[%s] Set socket timeout failed!", __func__);
            return false;
        }
        return true;
    }

    bool sendData(T &&data, const int &&index) {
        if (mSocketFd < 0) {
            syslog(LOG_ERR, "[%s] Invalid socket fd!", __func__); 
            return false;
        }

        const size_t content_size = data.ByteSize();
        const unsigned char header_bytes[4] = {
            static_cast<unsigned char>((index / 1000) % 10),
            static_cast<unsigned char>((index / 100) % 10),
            static_cast<unsigned char>((index / 10) % 10),
            static_cast<unsigned char>(index % 10),
        };
        std::vector<std::uint8_t> send_buffer(sizeof(header_bytes) + content_size);
        std::copy(header_bytes, header_bytes + sizeof(header_bytes), send_buffer.begin());
        data.SerializeToArray(send_buffer.data() + sizeof(header_bytes), content_size);

        int ret = write_fully(mSocketFd, reinterpret_cast<const char *>(send_buffer.data()), send_buffer.size());
        if (ret < 0) {
            syslog(LOG_ERR, "[%s] Write data to server failed!", __func__);            
            return false;
        }
        return true;
    }

    bool readData(R &data) {
        if (mSocketFd < 0) {
            syslog(LOG_ERR, "[%s] Invalid socket fd!", __func__); 
            return false;
        }

        char* buf = (char*)malloc(BUF_SIZE);
        memset(buf, 0, BUF_SIZE);
        ssize_t readSize = 0, totalSize = 0, readCount = 0;

        readSize = read_buf(mSocketFd, buf, BUF_SIZE);
        ++readCount;

        while (readSize == BUF_SIZE) {
            totalSize += readSize;

            char* newBuf = (char*)realloc(buf, (readCount + 1) * BUF_SIZE);
            if (!newBuf) {
                syslog(LOG_ERR , "[%s] Realloc failed !", __func__);
                free(buf);
                return false;
            }

            buf = newBuf;
            char* dataStartAddr = buf + (readCount * BUF_SIZE);
            memset(dataStartAddr, 0, BUF_SIZE);

            readSize = read_buf(mSocketFd, dataStartAddr, BUF_SIZE);
            ++readCount;
        }
        if (readSize > 0) {
            totalSize += readSize;
        }

        string str(buf, totalSize);
        data.ParseFromString(str);
        free(buf);

        return true;
    }

private:
    std::string mSocketPath = "";
    int mSocketFd = -1;
};

}

extern "C" {

/***********************************************************
   Function:       install_app
   Description:    安装app
   Calls:
   Called By:
   Input:
        filename:apk名称，如 com.tencent.mm_8.0.0.apk
        appname:应用名,如 微信
        pkgname:包名，如 com.tencent.mm
   Output:
        true: 执行成功
        false: 执行失败
   Return:
   Others:  head: 0001
 ************************************************************/
bool install_app(char *filename, char *appname, char *pkgname)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::InstallApp> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::InstallApp obj;
        obj.set_file_name(filename);
        obj.set_app_name(appname);
        obj.set_package_name(pkgname);
        if (connectSocket.sendData(std::move(obj), 1)) {
            cn::kylinos::kmre::kmrecore::ActionResult reply;
            if (connectSocket.readData(reply)) {
                return reply.result();
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return false;
        }
    }

    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       uninstall_app
   Description:    卸载app
   Calls:
   Called By:
   Input:
        pkgname:包名，如 com.tencent.mm
   Output:
        true: 执行成功
        false: 执行失败
   Return:
   Others:  head: 0002
 ************************************************************/
int uninstall_app(char* pkgname)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::UninstallApp> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::UninstallApp obj;
        obj.set_package_name(pkgname);
        if (connectSocket.sendData(std::move(obj), 2)) {
            cn::kylinos::kmre::kmrecore::ActionResult reply;
            if (connectSocket.readData(reply)) {
                std::string cmdInfo = reply.org_cmd();//UninstallApp or InstallApp
                std::string errInfo = reply.has_err_info() ? reply.err_info() : "";

                syslog(LOG_DEBUG, "[%s] Reply:result = %d, cmd_info:'%s', err_info:'%s'", 
                    __func__, reply.result(), cmdInfo.c_str(), errInfo.c_str());

                if (reply.result()) {
                    delete_desktop_and_icon(pkgname);// remove desktop file
                    return 1;
                }
                else {
                    if (cmdInfo == "DELETE_SUCCEEDED") {
                        delete_desktop_and_icon(pkgname);// remove desktop file
                        return 1;
                    }
                    else if (cmdInfo == "DELETE_FAILED_INTERNAL_ERROR") {//未指明的原因
                        return -1;
                    }
                    else if (cmdInfo == "DELETE_FAILED_DEVICE_POLICY_MANAGER") {//设备管理器
                        return -2;
                    }
                    else if (cmdInfo == "DELETE_FAILED_USER_RESTRICTED") {//用户受到限制
                        return -3;
                    }
                    else if (cmdInfo == "DELETE_FAILED_OWNER_BLOCKED") {//因为配置文件或设备所有者已将包标记为可卸载
                        return -4;
                    }
                    else if (cmdInfo == "DELETE_FAILED_ABORTED") {//中止
                        return -5;
                    }
                    else if (cmdInfo == "DELETE_FAILED_USED_SHARED_LIBRARY") {//因为packge是一个由其他已安装的包使用的共享库
                        return -6;
                    }
                }
                return -1;
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return -7;
        }
    }


    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return -8;
}

/***********************************************************
   Function:       launch_app
   Description:    启动app
   Calls:
   Called By:
   Input:
        pkgname:包名，如 com.tencent.mm
        fullscreen:是否以全屏方式启动应用， true：全屏，false：非全屏
        width: 安卓内启动的分辨率的宽度，如720
        height: 安卓内启动的分辨率的高度，如1280
        density: 安卓内启动的密度，如160或者320
   Output:
        true: 执行成功
        false: 执行失败
   Return:
   Others:  head: 0003
 ************************************************************/
bool launch_app(char* pkgname, bool fullscreen, int width, int height, int density)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::LaunchApp> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::LaunchApp obj;
        obj.set_package_name(pkgname);
        obj.set_fullscreen(fullscreen);
        obj.set_width((width > 0) ? width : 0);
        obj.set_height((height > 0) ? height : 0);
        obj.set_density((density > 0) ? density : 240);
        if (connectSocket.sendData(std::move(obj), 3)) {
            cn::kylinos::kmre::kmrecore::ActionResult reply;
            if (connectSocket.readData(reply)) {
                return reply.result();
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return false;
        }
    }

    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       close_app
   Description:    关闭app
   Calls:
   Called By:
   Input:
        appname:应用名, 如 微信
        pkgname:包名，如 com.tencent.mm
   Output:
        true: 执行成功
        false: 执行失败
   Return:
   Others:  head: 0004
 ************************************************************/
bool close_app(char* appname, char* pkgname)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::CloseApp> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::CloseApp obj;
        obj.set_app_name(appname);
        obj.set_package_name(pkgname);
        if (connectSocket.sendData(std::move(obj), 4)) {
            cn::kylinos::kmre::kmrecore::ActionResult reply;
            if (connectSocket.readData(reply)) {
                return reply.result();
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return false;
        }
    }

    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       get_installed_applist
   Description:    获取已安装的应用列表
   Calls:
   Called By:
   Input:
   Output:  返回json格式的字符串
   Return:
   Others:          head: 0005   （保证返回值的格式为[],方便管理端程序对其转换为json进行解析）
 ************************************************************/
char* get_installed_applist()
{
    static std::string list = "[]";
    ConnectSocket<cn::kylinos::kmre::kmrecore::GetInstalledAppList, \
                cn::kylinos::kmre::kmrecore::InstalledAppList> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::GetInstalledAppList obj;
        obj.set_include_hide_app(true);
        if (connectSocket.sendData(std::move(obj), 5)) {
            cn::kylinos::kmre::kmrecore::InstalledAppList data;
            if (connectSocket.readData(data)) {
                if (data.size() > 1) {//size is a member variable of InstalledAppList
                    list = "[";
                    for (int n = 0; n < data.item_size(); n++) {
                        auto app = data.item(n);//InstalledAppItem
                        if(n > 0){
                            list += ",";
                        }
                        list += "{\"app_name\":\"";
                        list += app.app_name();

                        list += "\",\"package_name\":\"";
                        list += app.package_name();

                        list += "\",\"version_name\":\"";
                        list += app.version_name();
                        list += "\"}";
                    }
                    list += "]";

                    return const_cast<char *>(list.c_str());
                }
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return const_cast<char *>(list.c_str());
        }
    }

    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return const_cast<char *>(list.c_str());
}

/***********************************************************
   Function:       get_running_applist
   Description:    获取正在运行的应用列表
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:          head: 0006   （保证返回值的格式为[],方便管理端程序对其转换为json进行解析      strdup）
 ************************************************************/
char* get_running_applist()
{
    static std::string list = "[]";
    ConnectSocket<cn::kylinos::kmre::kmrecore::GetRunningAppList, \
                cn::kylinos::kmre::kmrecore::RunningAppList> connectSocket(eLink_Launcher);
    
    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::GetRunningAppList obj;
        obj.set_with_thumbnail(true);
        if (connectSocket.sendData(std::move(obj), 6)) {
            cn::kylinos::kmre::kmrecore::RunningAppList data;
            if (connectSocket.readData(data)) {
                if (data.size() > 0) {//size is a member variable of RunningAppList
                    list = "[";
                    for (int n = 0; n < data.item_size(); n++) {
                        auto app = data.item(n);//RunningAppItem
                        if(n > 0){
                            list += ",";
                        }
                        list += "{\"app_name\":\"";
                        list += app.app_name();

                        list += "\",\"package_name\":\"";
                        list += app.package_name();
                        list += "\"}";
                    }
                    list += "]";

                    return const_cast<char *>(list.c_str());
                }
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return const_cast<char *>(list.c_str());
        }
    }

    syslog(LOG_ERR, "[%s] Send data failed!", __func__);
    return const_cast<char *>(list.c_str());
}

/***********************************************************
   Function:       send_clipboard
   Description:    将kylin桌面的剪切板数据发送给android
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:          head: 0007
 ************************************************************/
bool send_clipboard(char *content)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::SetClipboard> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::SetClipboard obj;
        obj.set_content(content);
        if (connectSocket.sendData(std::move(obj), 7)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       focus_win_id
   Description:    根据id激活对应的app
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:          head: 0008
 ************************************************************/
bool focus_win_id(int display_id)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::FocusWin> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::FocusWin obj;
        obj.set_focus_win(display_id);
        if (connectSocket.sendData(std::move(obj), 8)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       control_app
   Description:    控制安卓
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:          head: 0009
        event_type=0 : 点击返回按钮
        event_type=1 : 点击拍照按钮
        event_type=2 : 音量增大
        event_type=3 : 音量减小
        event_type=4 : 亮度增大
        event_type=5 : 亮度减小
        event_type ......
 ************************************************************/
bool control_app(int display_id, char *pkgname, int event_type, int event_value)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::ControlApp> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::ControlApp obj;
        obj.set_display_id(display_id);
        obj.set_package_name(pkgname);
        obj.set_event_type(event_type);
        if (event_value > 0) {
            obj.set_event_value(event_value);
        }
        if (connectSocket.sendData(std::move(obj), 9)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       insert_file
   Description:    向安卓数据库添加一条记录
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:  head: 0010
 ************************************************************/
bool insert_file(char *path, char *mime_type)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::InsertFile> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::InsertFile obj;
        obj.set_data(path);
        obj.set_mime_type(mime_type);
        if (connectSocket.sendData(std::move(obj), 10)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       remove_file
   Description:    从安卓数据库删除一条记录
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:  head: 0011
 ************************************************************/
bool remove_file(char *path, char *mime_type)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::RemoveFile> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::RemoveFile obj;
        obj.set_data(path);
        obj.set_mime_type(mime_type);
        if (connectSocket.sendData(std::move(obj), 11)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       request_media_files
   Description:    从安卓请求所有文件的数据
   Calls:
   Called By:
   Input:
       type 0: dump all mediafile info
            1: image files
            2: video files
            3: audio files
            4: document files
   Output:
   Return:
   Others:  head: 0012
 ************************************************************/
bool request_media_files(int type)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::RequestMediaFiles> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::RequestMediaFiles obj;
        obj.set_type(type);
        if (connectSocket.sendData(std::move(obj), 12)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       request_drag_file
   Description:    拖动文件到安卓应用里
   Calls:
   Called By:
   Input:
   Output:
   Return:
   Others:  head: 0013
 ************************************************************/
bool request_drag_file(const char *path, const char *pkg, int display_id, bool has_double_display)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::DragFile> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::DragFile obj;
        obj.set_file_path(path);
        obj.set_package_name(pkg);
        obj.set_display_id(display_id);
        obj.set_has_double_display(has_double_display);
        if (connectSocket.sendData(std::move(obj), 13)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       rotation_changed
   Description:    窗口方向发生了变化(横、竖、方)
   Calls:
   Called By:
   Input:
        display_id:ID, 如 5001
        pkgname:包名, 如 com.tencent.mm
        width:窗口宽度, 如 450
        height:窗口高度, 如 800
        rotation:包名，如 0表示竖向   1表示横向     2表示方型
   Output:
   Return:
   Others:  head: 0014
 ************************************************************/
bool rotation_changed(int display_id, char *pkgname, int width, int height, int rotation)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::RotationChanged> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::RotationChanged obj;
        obj.set_display_id(display_id);
        obj.set_package_name(pkgname);
        obj.set_width(width);
        obj.set_height(height);
        obj.set_rotation(rotation);
        if (connectSocket.sendData(std::move(obj), 14)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       set_system_prop
   Description:    设置Android属性
   Calls:
   Called By:
   Input:
        event_type:属性类型, 0：prop属性，1：setting属性
        prop_name:属性名称
        prop_value:属性值
   Output:
   Return:
   Others:  head: 0015
 ************************************************************/
bool set_system_prop(int event_type, char *prop_name, char *prop_value)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::SetSystemProp> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::SetSystemProp obj;
        obj.set_event_type(event_type);
        obj.set_value_field(prop_name);
        obj.set_value(prop_value);
        if (connectSocket.sendData(std::move(obj), 15)) {
            return true;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return false;
}

/***********************************************************
   Function:       get_system_prop
   Description:    获取Android属性
   Calls:
   Called By:
   Input:
        event_type:属性类型, 0：prop属性，1：setting属性
        prop_name:属性名称
   Output:
   Return:
   Others:  head: 0016
 ************************************************************/
char *get_system_prop(int event_type, char *prop_name)
{
    static std::string value;
    ConnectSocket<cn::kylinos::kmre::kmrecore::GetSystemProp, \
                    cn::kylinos::kmre::kmrecore::SendSystemProp> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::GetSystemProp obj;
        obj.set_event_type(event_type);
        obj.set_value_field(prop_name);
        connectSocket.setTimeout();
        if (connectSocket.sendData(std::move(obj), 16)) {
            cn::kylinos::kmre::kmrecore::SendSystemProp data;
            if (connectSocket.readData(data)) {
                if ((data.event_type() == event_type) && (data.value_field() == prop_name)) {
                    value = data.value();
                    return (char*)(value.c_str());
                }
            }
            syslog(LOG_ERR, "[%s] Read data failed!", __func__);
            return nullptr;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return nullptr;
}

/***********************************************************
   Function:       update_app_window_size
   Description:    更新App窗口大小
   Calls:
   Called By:
   Input:
        pkgName: app包名
        display_id: display id
        width: 宽度值
        height: 高度值
   Output:
   Return:
   Others:  head: 0017
 ************************************************************/
int update_app_window_size(const char* pkg_name, int display_id, int width, int height)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::UpdateAppWindowSize> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::UpdateAppWindowSize obj;
        obj.set_package_name(pkg_name);
        obj.set_display_id(display_id);
        obj.set_width(width);
        obj.set_height(height);
        if (connectSocket.sendData(std::move(obj), 17)) {
            return 0;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return -1;
}

/***********************************************************
   Function:       update_network_proxy
   Description:    更新网络代理
   Calls:
   Called By:
   Input:
        enable: 打开/关闭
        protocal: 协议类型，包括（http，https，ftp，socks）
        host: 主机域名/ip
        port: 端口号
   Output:
   Return:
   Others:  head: 0018
 ************************************************************/
int update_network_proxy(bool enable, const char* protocal, const char* host, int port)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::SetProxy> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::SetProxy obj;
        obj.set_open(enable);
        obj.set_host(host);
        obj.set_port(port);
        obj.set_type(protocal);
        if (connectSocket.sendData(std::move(obj), 18)) {
            return 0;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return -1;
}

/***********************************************************
   Function:       update_display_size
   Description:    动态更新display大小,用于平板模式切换或屏幕分辨率改变时
   Calls:
   Called By:
   Input:
        display_id: display id
        width: 宽度值
        height: 高度值
   Output:
   Return:
   Others:  head: 0019
 ************************************************************/
int update_display_size(int display_id, int width, int height)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::UpdateDisplaySize> connectSocket(eLink_Launcher);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::UpdateDisplaySize obj;
        obj.set_display_id(display_id);
        obj.set_width(width);
        obj.set_height(height);
        if (connectSocket.sendData(std::move(obj), 19)) {
            return 0;
        }
    }
    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return -1;
}

/***********************************************************
   Function:       answer_call
   Description:    消息通知接听/拒收
   Calls:
   Called By:
   Input:
        answer: 接听
   Output:
   Return:
   Others:  head: 0020
 ************************************************************/
int answer_call(bool answer)
{
    ConnectSocket<cn::kylinos::kmre::kmrecore::AnswerCall> connectSocket(eLink_Manager);

    if (connectSocket.connect()) {
        cn::kylinos::kmre::kmrecore::AnswerCall obj;
        obj.set_answer(answer);
        if (connectSocket.sendData(std::move(obj), 20)) {
            return 0;
        }
    }

    syslog(LOG_ERR, "[%s] Send cmd data failed!", __func__);
    return -1;
}

/***********************************************************
   Function:       is_debian_package_installed
   Description:    deb包是否安装
   Calls:
   Called By:
   Input:
   Output:
        true: 已经安装
        false: 未安装
   Return:
   Others:
 ************************************************************/
bool is_deb_package_installed(const char *pkg)
{
    // dpkg-query -W -f='${Version}\n' libkylin-kmre
    // dpkg-query -W -f='${Status}\n' libkylin-kmre 2>/dev/null |grep -c "ok installed"

    if (!pkg) {
        return false;
    }

    FILE *fp;
    char buf[256] = {0};
    char cmd[256] = {0};
    //char cmd[] = "dpkg-query -W -f='${Status}\n' libkylin-kmre 2>/dev/null |grep -c \"ok installed\"";
    snprintf(cmd, sizeof(cmd), "dpkg-query -W -f='${Status}\n' %s 2>/dev/null |grep -c \"ok installed\"", pkg);

    fp = popen(cmd, "r");
    if (!fp)  {
        return false;
    }

    (void)fgets(buf, sizeof(buf), fp);
    pclose(fp);

    buf[strcspn(buf, "\n")] = '\0';
    int ret = atoi(buf);
    if (ret > 0) {
        return true;
    }

    return false;
}

/***********************************************************
   Function:       is_android_env_installed
   Description:    安卓兼容环境是否安装
   Calls:
   Called By:
   Input:
   Output:
        true: 已经安装
        false: 未安装
   Return:
   Others:
 ************************************************************/
bool is_android_env_installed()
{
    //__mips__  __sw_64__
#ifdef __x86_64__
    if (is_deb_package_installed("docker.io") && 
        is_deb_package_installed("kylin-kmre-daemon") && 
        is_deb_package_installed("kylin-kmre-window") && 
        is_deb_package_installed("kylin-kmre-manager") && 
        is_deb_package_installed("kylin-kmre-display-control") && 
        is_deb_package_installed("libkylin-kmre-emugl") && 
        is_deb_package_installed("kylin-kmre-image-data-x64")) {
        return true;
    }
#elif __aarch64__
    if (isInCpuinfo("Hardware", "Kirin") || isInCpuinfo("Hardware", "PANGU")) {
        if (is_deb_package_installed("docker.io") && 
            is_deb_package_installed("kylin-kmre-daemon") && 
            is_deb_package_installed("kylin-kmre-window") && 
            is_deb_package_installed("kylin-kmre-manager") && 
            is_deb_package_installed("kylin-kmre-display-control") && 
            is_deb_package_installed("libkylin-kmre-emugl-wayland") && 
            is_deb_package_installed("kylin-kmre-image-data")) {
            return true;
        }
    }
    else {
        if (strInCpuinfo("Kirin") || strInCpuinfo("PANGU")) {
            if (is_deb_package_installed("docker.io") && 
                is_deb_package_installed("kylin-kmre-daemon") && 
                is_deb_package_installed("kylin-kmre-window") && 
                is_deb_package_installed("kylin-kmre-manager") && 
                is_deb_package_installed("kylin-kmre-display-control") && 
                is_deb_package_installed("libkylin-kmre-emugl-wayland") && 
                is_deb_package_installed("kylin-kmre-image-data")) {
                return true;
            }
        }
        else {
            if (is_deb_package_installed("docker.io") && 
                is_deb_package_installed("kylin-kmre-daemon") && 
                is_deb_package_installed("kylin-kmre-window") && 
                is_deb_package_installed("kylin-kmre-manager") && 
                is_deb_package_installed("kylin-kmre-display-control") && 
                is_deb_package_installed("libkylin-kmre-emugl") && 
                is_deb_package_installed("kylin-kmre-image-data")) {
                return true;
            }
        }
    }
#endif

    return false;
}

}
