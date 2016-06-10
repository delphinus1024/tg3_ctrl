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

#pragma once

#include <fstream>
#include <string>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <queue>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

#define FRAME_BUFFER_SIZE   (0x80000)

// Packet head id
#define PACKETID_FIRST (0x90600000)
#define PACKETID_CONT  (0x80600000)
#define PACKETID_LAST  (0x80e00000)

boost::mutex mtx_frame_buffer;
int idx_writing;// mutex protected
int idx_reading;// mutex protected
std::queue<int> swap_buffer_q;// mutex protected
unsigned char frame_buffer[2][FRAME_BUFFER_SIZE]; 
unsigned char jpg_bufffer[2][FRAME_BUFFER_SIZE];
int frame_buffer_wrptr[2];
int jpg_buffer_wrptr[2]; //
int frame_no;

class analyze_rtp {
public:
    analyze_rtp() {
        init(); 
    }
    
    void init() { 
        idx_writing = 0;
        idx_reading = 1;
        frame_buffer_wrptr[0] = frame_buffer_wrptr[1] = jpg_buffer_wrptr[0] = jpg_buffer_wrptr[1] = 0;
        frame_no = 0;
    }
    
    unsigned long check_packet_id(unsigned char *packet) {
        unsigned long *wordpt;
        
        wordpt = (unsigned long*)packet;
        unsigned long head = __builtin_bswap32 (*wordpt) & 0xffff0000;
        
        switch(head) {
            case PACKETID_FIRST:
            case PACKETID_CONT:
            case PACKETID_LAST:
                return head;
        }
        
        return -1; // wrong packet type
    }
    
    bool check_jpg_available() {
        return check_queue_full();
    }

    bool check_queue_full() {
        bool ret;
        {
            boost::mutex::scoped_lock lock(mtx_frame_buffer);
            ret = (swap_buffer_q.size() > 0 ? true : false);
        }
        return ret;
    }
    
    bool update_queue() {
        {
            boost::mutex::scoped_lock lock(mtx_frame_buffer);
            if(swap_buffer_q.size() == 0) { // if not full move to another plane, otherwise current frame to trash
                swap_buffer_q.push(idx_writing);
                std::swap(idx_writing,idx_reading);
                
            }
            
            frame_buffer_wrptr[idx_writing] = 0;
            jpg_buffer_wrptr[idx_writing] = 0;
        }
        return true;
    }
    
    bool pop_queue() {
        {
            boost::mutex::scoped_lock lock(mtx_frame_buffer);
            if(swap_buffer_q.size() > 0) { // if not full move to another plane, otherwise current frame to trash
                swap_buffer_q.pop();
            }
        }
    }
    
    bool store_first_packet(unsigned char *packet,int len) {
        if(frame_buffer_wrptr[idx_writing] != 0)
            return false;
        
        memcpy(&(frame_buffer[idx_writing][frame_buffer_wrptr[idx_writing]]),packet,len);
        frame_buffer_wrptr[idx_writing] += len;
        
        return true;
    }
    
    bool store_cont_packet(unsigned char *packet,int len) {
        memcpy(&(frame_buffer[idx_writing][frame_buffer_wrptr[idx_writing]]),packet,len);
        frame_buffer_wrptr[idx_writing] += len;
        
        return true;
    }
    
    bool store_last_packet(unsigned char *packet,int len) {
        memcpy(&(frame_buffer[idx_writing][frame_buffer_wrptr[idx_writing]]),packet,len);
        frame_buffer_wrptr[idx_writing] += len;
        
        return true;
    }
    
    int make_jpg_data_first(unsigned char *packet,int len) {
        unsigned int abspos = 0; //current_frame_head_; // byte position from buffer head
        unsigned int pos = 0; // byte position from frame head
        unsigned int xfersize;
        bool is_last = false;
        unsigned long head;
        unsigned long *wordpt;
        
        unsigned int current_jpg_head = 0;
        
        std::cout << std::hex << std::showbase;
        
        // headder analysis
        run_headder_analysis(packet,len);
        
        wordpt = (unsigned long*)&(packet[0]);
        head = __builtin_bswap32 (*wordpt);
        if((head  & 0xffff0000 )!= PACKETID_FIRST) { // not valid packet
            std::cout << "Error: Invalid packet=" << head << std::endl;
            return -2;
        }
        
        int jpg_len = (len - (jpg_head_offset_ * 4));
        
        memcpy(jpg_bufffer[idx_writing],&(packet[jpg_head_offset_ * 4]),jpg_len);
        jpg_buffer_wrptr[idx_writing] += jpg_len;

        if(!check_pos_ov(jpg_buffer_wrptr[idx_writing])) {
            return -4;
        }
    }

    int make_jpg_data_cont(unsigned char *packet,int len) {
        memcpy(&(jpg_bufffer[idx_writing][jpg_buffer_wrptr[idx_writing]]),&(packet[12]),len - 12);
        jpg_buffer_wrptr[idx_writing] += (len - 12);
        
        if(!check_pos_ov(jpg_buffer_wrptr[idx_writing])) {
            return -1;
        }
        
        return 0;
    }

    int save_jpg() {
        char fn[256];
        int idx;
        
        {
            boost::mutex::scoped_lock lock(mtx_frame_buffer);
            idx = idx_reading;
        }
        
        sprintf(fn,"f%d.jpg",frame_no++);
        FILE *fp;
        
        fp = fopen(fn,"wb");
        if(!fp) 
            return -1;
            
        fwrite(&(jpg_bufffer[idx]),jpg_buffer_wrptr[idx],1,fp);
        fclose(fp);
        
        return 0;
    }

    unsigned char *get_jpg_buf(int &len) {
        char fn[256];
        int idx;
        unsigned char *ret = NULL;
        
        {
            boost::mutex::scoped_lock lock(mtx_frame_buffer);
            idx = idx_reading;
        }
        
        ret = jpg_bufffer[idx];
        len = jpg_buffer_wrptr[idx];
        
        return ret;
    }
    
    int run_headder_analysis(unsigned char *packet,int len) {
        unsigned long *ptword,ptword_swap,*ptword_next_packet;
        
        // RTP headder 0
        ptword = (unsigned long*)packet;
        
        unsigned int offset = 0;
        ptword_swap = __builtin_bswap32 (ptword[offset]);
        std::cout << std::hex << std::showbase;
        
        version_ = (ptword_swap & 0xc0000000) >> 30;
        padding_ = (ptword_swap & 0x20000000) >> 29;
        extension_ = (ptword_swap & 0x10000000) >> 28;
        cc_ = (ptword_swap & 0x0f000000) >> 24;
        marker_ = (ptword_swap & 0x00800000) >> 23;
        pt_ = (ptword_swap & 0x007f0000) >> 16;
        seq_num_ = (ptword_swap & 0x0000ffff);
        offset++;
        
        /*{
            std::cout << "version_=" << version_ << std::endl;
            std::cout << "padding_=" << padding_ << std::endl;
            std::cout << "extension_=" << extension_ << std::endl;
            std::cout << "cc_=" << cc_ << std::endl;
            std::cout << "marker_=" << marker_ << std::endl;
            std::cout << "pt_=" << pt_ << std::endl;
            std::cout << "seq_num_=" << seq_num_ << std::endl;
        }*/
        
        // RTP headder 1
        ptword_swap = __builtin_bswap32 (ptword[offset++]);
        timestamp_ = ptword_swap;
        /*{
            std::cout << "timestamp_=" << timestamp_ << std::endl;
        }*/
        
        // RTP headder 1
        ptword_swap = __builtin_bswap32 (ptword[offset++]);
        ssrc_ = ptword_swap;
        
        // RTP extension headder
        if(extension_) {
            ptword_swap = __builtin_bswap32 (ptword[offset++]);
            
            exth_version_ = (ptword_swap & 0xffff0000) >> 16;
            exth_length_ = (ptword_swap & 0x0000ffff);

            jpg_head_offset_ = offset+exth_length_;
            
            /*{
                std::cout << "exth_version_=" << exth_version_ << std::endl;
                std::cout << "exth_length_=" << exth_length_ << std::endl;
            }*/
            
            unsigned int local_offset = 0;
            
            while(local_offset < exth_length_) {
                int ret;
                ret = analyze_ext_headder(&(ptword[offset + local_offset]));
                if(ret > 0)
                    local_offset += ret;
            }
            
        } 
        
        //analysis_done_ = true;
        
        return 0;
    }
    
protected:
    unsigned int version_;
    unsigned int padding_;
    unsigned int extension_;
    unsigned int cc_;
    unsigned int marker_;
    unsigned int pt_;
    unsigned int seq_num_;
    unsigned int timestamp_;
    unsigned int ssrc_;

    unsigned int exth_version_;
    unsigned int exth_length_;
    unsigned int exth_jpgsize_;
    unsigned int exth_frame_color_;
    unsigned int exth_xcoordinate_;
    unsigned int exth_ycoordinate_;
    unsigned int exth_frame_width_;
    unsigned int exth_frame_height_;
    
    unsigned int jpg_head_offset_;
    
    int analyze_ext_headder(unsigned long *ptword) {
        unsigned long ptword_swap;
        unsigned int id,length;
        
        ptword_swap = __builtin_bswap32 (ptword[0]);
        
        id = (ptword_swap & 0xffff0000) >> 16;
        length = (ptword_swap & 0x0000ffff);

        //std::cout << "analyze_ext_headder::id=" << id << std::endl;
        //std::cout << "analyze_ext_headder::length=" << length << std::endl << std::endl;
        
        switch(id) {
            case 1: // frame size
                exth_jpgsize_ = __builtin_bswap32 (ptword[1]);
                //std::cout << "analyze_ext_headder::exth_jpgsize_=" << exth_jpgsize_ << std::endl << std::endl;
                break;
            //
            case 2: // focus info
                exth_frame_color_ = __builtin_bswap32 (ptword[1]);
                exth_xcoordinate_ = __builtin_bswap32 (ptword[2]);
                exth_ycoordinate_ = __builtin_bswap32 (ptword[3]);
                exth_frame_width_ = __builtin_bswap32 (ptword[4]);
                exth_frame_height_ = __builtin_bswap32 (ptword[5]);
 /*
                std::cout << "analyze_ext_headder::exth_frame_color_=" << exth_frame_color_ << std::endl << std::endl;
                std::cout << "analyze_ext_headder::exth_xcoordinate_=" << exth_xcoordinate_ << std::endl << std::endl;
                std::cout << "analyze_ext_headder::exth_ycoordinate_=" << exth_ycoordinate_ << std::endl << std::endl;
                std::cout << "analyze_ext_headder::exth_frame_width_=" << exth_frame_width_ << std::endl << std::endl;
                std::cout << "analyze_ext_headder::exth_frame_height_=" << exth_frame_height_ << std::endl << std::endl;
*/
                break;
            //
                
        }
        
        
        return length + 1;
    }
    
    bool check_pos_ov(int pos) {
        if(pos >= FRAME_BUFFER_SIZE)
            return false;
        return true;
    }
    
};
