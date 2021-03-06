// GetFileSize.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#include <iostream>
#include "gflags/gflags.h"

extern "C" {
#include "libavformat/avio.h"
};

#pragma comment(lib, "avformat.lib")

DEFINE_string(url, "http://192.168.56.1/share/Wildlife.wmv", "file's url");

int64_t get_size(const char* file_path) {
    AVIOContext *avio_ctx = NULL;
    int64_t file_size = 0;
    
    do {
        if (avio_open(&avio_ctx, file_path, AVIO_FLAG_READ)) {
            std::cout << "avio_open error";
            break;
        }
        file_size = avio_size(avio_ctx);
    } while (0);
    
    if (avio_ctx) {
        avio_close(avio_ctx);
    }

    return file_size;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    std::cout << FLAGS_url << " size is "  << get_size(FLAGS_url.c_str());
    system("pause");
    return 0;
}
