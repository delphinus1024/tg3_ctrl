/*
The MIT License (MIT)

Copyright (c) 2016 delphinus1024

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <utility> // to use "swap" under C++11 or after
#include <windows.h>
#include <time.h>
#include <winsock.h>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <opencv2/opencv.hpp>

#include "analyze_rtp.h"

#define TG_IP ("192.168.0.10") // tg3 IP address
#define BUFFSIZE (16536)

enum {
    FLG_GET,FLG_POST
};

// liveview flag
boost::mutex mtx_liveview;
bool on_liveview = false; // mtx_liveview protected
analyze_rtp an;

// focus flag
boost::mutex mtx_dofocus;
bool dofocus = false; // mtx_dofocus protected
int focusx,focusy;

char szBuff[BUFFSIZE];

int do_transaction(int flag,const char *req,const char *data,char *resp,int resplen) {
    std::string get_template = std::string(
            " HTTP/1.1\r\n"\
            "Host: 192.168.0.10\r\n"\
            "Connection: Keep-Alive\r\n"\
            "User-Agent: OI.Share v2\r\n"\
            "\r\n");

    std::string post_template = std::string(
            " HTTP/1.1\r\n"\
            "Content-Length: ");
            
    std::string post_template2 = std::string(
            "\r\n"\
            "Content-Type: text/plain; charset=ISO-8859-1\r\n"\
            "Host: 192.168.0.10\r\n"\
            "Connection: Keep-Alive\r\n"\
            "User-Agent: OI.Share v2\r\n"\
            "\r\n");
    
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;
    int nRet;
    SOCKADDR_IN sockaddr;    
    unsigned short nPort = 80;

    int offset;
    int sentnum;
    int restnum;
    int len;
    char szBuff[BUFFSIZE];
    std::ostringstream os;
    
    os << strlen(data); 
    
    std::string cmdstr = std::string(flag == FLG_GET ? ("GET ") : ("POST ")) + std::string(req) + 
        (flag == FLG_GET ? get_template : (post_template + os.str() + post_template2)) + 
        std::string(data);
    std::cout << "REQ:" << std::endl << cmdstr << std::flush << std::endl << std::endl;
    
    for(int i = 0;i < BUFFSIZE;++i) szBuff[i] = 0;
    
    WSAStartup(MAKEWORD(1, 1), &wsaData);

    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = inet_addr(TG_IP);
    sockaddr.sin_port = htons(nPort);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Error: invalid socket." << std::endl;
        return -1;
    }
    
    nRet = connect(sock, (const SOCKADDR*)&sockaddr, sizeof(sockaddr));
    if (nRet != 0) {
        std::cerr << "Error: connect failed." << std::endl;
        return -2;
    }
    
    offset = 0;
    const char *cmd = cmdstr.c_str(); 
    restnum = (int)strlen(cmd);
    while (restnum > 0) {
        sentnum = send(sock, &cmd[offset], restnum, 0);
        if (sentnum == SOCKET_ERROR) {
            return -3;
        }
        offset += sentnum;
        restnum -= sentnum;
    }

    resp[0] = 0;
    while ( (len = recv(sock, szBuff, sizeof(szBuff), 0) ) > 0) {
        strcat(resp,szBuff);
    }

    closesocket(sock);
    WSACleanup();

    std::cout << "RESP:" << std::endl << resp << std::flush << std::endl << std::endl;
    
    return 0;
}

void UCP_Liveview(const int port) {
    
    WSAData wsaData;
    fd_set fds, readfds;
    struct timeval tv;
    
    SOCKET sock;
    struct sockaddr_in addr;

    unsigned char buf[16536];

    WSAStartup(MAKEWORD(2,0), &wsaData);

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.S_un.S_addr = INADDR_ANY;

    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds); 

    tv.tv_sec = 2;
    tv.tv_usec = 0;
    
    std::cout << "Liveview Started===========================" << std::flush;
    
    int index = 0;
    bool on_xfer = false;
    
    std::cout << std::hex << std::showbase;
    
    for(int i = 0;;++i) {
        {
            boost::mutex::scoped_lock lock(mtx_liveview);
            if(!on_liveview)
                break;
        }
        
        memcpy(&fds, &readfds, sizeof(fd_set));
        int n = select(0, &fds, NULL, NULL, &tv);

        if (n == 0) {
            std::cout << "timeout" << std::endl;
        } else if (FD_ISSET(sock, &fds)) {
            memset(buf, 0, sizeof(buf));
            int ret = recv(sock, (char*)buf, sizeof(buf), 0);
            if(ret < 0) {
        		 std::cout << "Error: recv :" << WSAGetLastError() << std::endl;
        		 break;
            } else {
                unsigned long head = an.check_packet_id(buf);
                switch(head) {
                    case PACKETID_FIRST:
                        if(!an.check_queue_full()) {
                            if(an.store_first_packet(buf,ret)) {
                                an.make_jpg_data_first(buf,ret);
                                on_xfer = true;
                            } else
                                on_xfer = false;
                        } else {
                            on_xfer = false;
                        }
                        break;
                    case PACKETID_CONT:
                        if(on_xfer) {
                            an.store_cont_packet(buf,ret);
                            an.make_jpg_data_cont(buf,ret);
                        }
                        break;
                    case PACKETID_LAST:
                        if(on_xfer) {
                            an.store_last_packet(buf,ret);
                            an.make_jpg_data_cont(buf,ret);
                            an.update_queue();
                            on_xfer = false;
                        }
                        break;
                    default:
                        std::cout << "Error: Invalid packet:" << head<< std::endl;
                }
            }
        }
    }
    
    std::cout << std::endl;
    closesocket(sock);
    WSACleanup();
    
    std::cout << "Liveview Exit" << std::endl;
}

void on_mouse_callback(int event, int x, int y, int flags, void* param){
    switch (event){
    case cv::EVENT_LBUTTONDOWN:
        {
            focusx = x;
            focusy = y;
            boost::mutex::scoped_lock lock(mtx_dofocus);
            dofocus = true;
        }
        std::cout << std::dec << std::showbase;
        std::cout << "EVENT_LBUTTONDOWN (" << x << "," << y << ")" << std::endl;
        std::cout << std::hex << std::showbase;
        break;
    }
}


int main()
{
    do_transaction(FLG_GET,"/switch_cammode.cgi?mode=rec&lvqty=0640x0480","",szBuff,sizeof(szBuff));
    //do_transaction(FLG_GET,"/switch_cammode.cgi?mode=rec&lvqty=0320x0240","",szBuff,sizeof(szBuff));
    do_transaction(FLG_POST,"/set_camprop.cgi?com=set&propname=takemode","<?xml version=\"1.0\" ?><set><value>A</value></set>\r\n",szBuff,sizeof(szBuff));
    do_transaction(FLG_GET,"/get_camprop.cgi?com=desc&propname=takemode","",szBuff,sizeof(szBuff));
    {
        boost::mutex::scoped_lock lock(mtx_liveview);
        on_liveview = true;
    }
    boost::thread ucp_liveview(boost::bind(&UCP_Liveview,37789));
    do_transaction(FLG_GET,"/exec_takemisc.cgi?com=startliveview&port=37789","",szBuff,sizeof(szBuff));

	cv::namedWindow("liveview", cv::WINDOW_NORMAL);
    cv::setMouseCallback("liveview", on_mouse_callback);
    
    clock_t start = clock();  
    while(1) {
        if(an.check_jpg_available()) { // frame ready
            //an.save_jpg();
            uchar* jpgpt;
            int len;
            jpgpt = an.get_jpg_buf(len);
            if(!jpgpt) {
                std::cerr << "Error: null jpg data" << std::endl;
                continue;
            }
            std::vector<uchar> ibuff;
            
            for(int i = 0;i < len;++i) ibuff.push_back(jpgpt[i]);

            cv::Mat decodedImage  =  cv::imdecode( cv::Mat(ibuff) , CV_LOAD_IMAGE_COLOR);
            
            if((decodedImage.size().width > 0) && (decodedImage.size().height > 0))
                cv::imshow("liveview", decodedImage);
                
            an.pop_queue();
        }
        
        /*
        clock_t end = clock(); 
        if(((double)(end - start) / CLOCKS_PER_SEC) > 4)
            break;
        */
        int key = cv::waitKey(33);
        if(key == 27)
            break;
            
        // shoot
        switch(key) {
            case 0xd: // take picture
                    do_transaction(FLG_GET,"/exec_takemotion.cgi?com=starttake","",szBuff,sizeof(szBuff));
                break;
        }
        
        // focus
        bool dofocus_local = false;
        int focusx_local,focusy_local;
        
        {
            boost::mutex::scoped_lock lock(mtx_dofocus);
            if(dofocus) {
                dofocus_local = true;
                focusx_local = focusx;
                focusy_local = focusy;
                dofocus = false;
            }
        }
        
        if(dofocus_local) {
            char cmd[128];
            sprintf(cmd,"/exec_takemotion.cgi?com=assignafframe&point=%04dx%04d",focusx_local,focusy_local);
            std::cout << "cmd=" << cmd << std::endl << std::flush;
            do_transaction(FLG_GET,cmd,"",szBuff,sizeof(szBuff));
        }
    }
    //Sleep(10000);

/*
    do_transaction(FLG_GET,"/exec_takemotion.cgi?com=assignafframe&point=0064x0139","",szBuff,sizeof(szBuff));
    Sleep(1000);

    do_transaction(FLG_GET,"/exec_takemotion.cgi?com=starttake","",szBuff,sizeof(szBuff));
    Sleep(1000);

    do_transaction(FLG_GET,"/exec_takemotion.cgi?com=assignafframe&point=0233x0039","",szBuff,sizeof(szBuff));
    Sleep(1000);

    do_transaction(FLG_GET,"/exec_takemotion.cgi?com=starttake","",szBuff,sizeof(szBuff));
    Sleep(1000);
*/

    //==============================

    do_transaction(FLG_GET,"/exec_takemisc.cgi?com=stopliveview","",szBuff,sizeof(szBuff));

    {
        boost::mutex::scoped_lock lock(mtx_liveview);
        on_liveview = false;
    }
    
    ucp_liveview.join();

ERROR_EXIT:
    return 0;
}
