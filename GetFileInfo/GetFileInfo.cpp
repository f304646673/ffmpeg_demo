// GetFileInfo.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "pch.h"
#define  _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <functional>
#include <sstream>
#include <vector>
#include "gflags/gflags.h"

extern "C" {
#include "libavformat/avio.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
};

#include "av_media_type.h"
#include "av_codec_id.h"

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "swscale.lib")

DEFINE_string(url, "E:/share/Wildlife.wmv", "file's url");

void print_dict(AVDictionary *metadata) {
    AVDictionaryEntry *t = NULL;
    std::cout << "Meta:" << std::endl;
    while ((t = av_dict_get(metadata, "", t, AV_DICT_IGNORE_SUFFIX))) {
        std::cout << "key:" << t->key << "\tvalue:" << t->value << std::endl;
    }
}

std::string get_time(int64_t ts, const AVRational *time_base, int is_duration) {
    std::string time_str;

    if ((!is_duration && ts == AV_NOPTS_VALUE) || (is_duration && ts == 0)) {
        time_str = "N/A";
    }
    else {
        double d = ts * av_q2d(*time_base);
        std::stringstream ss;
        ss << d;
        ss >> time_str;
    }
    
    return time_str;
}

template<typename Component>
class AvComponentStore {
public:
    virtual void save(Component *d) = 0;
};

template<typename Component>
class TransStore :
    public AvComponentStore<Component>
{
public:
    TransStore(std::function<Component*(const Component*)> clone, std::function<void(Component**)> free) {
        _clone = clone;
        _free = free;
    }

    ~TransStore() {
        for (auto it = _store.begin(); it != _store.end(); it++) {
            if (*it) {
                _free(&*it);
            }
        }
    }
public:
    void traverse(std::function<void(Component*)> t) {
        if (!t) {
            return;
        }
        for (auto it = _store.begin(); it != _store.end(); it++) {
            if (*it) {
                t(*it);
            }
        }
    }
public:
    virtual void save(Component *d) {
        Component *p = _clone(d);
        _store.push_back(p);
    }
private:
    std::vector<Component*> _store;
    std::function<Component*(const Component*)> _clone;
    std::function<void(Component**)> _free;
};

using PacketsStore = TransStore<AVPacket>;
using FramesStore = TransStore<AVFrame>;

int decode_packet(AVCodecContext *avctx, AVPacket *pkt, std::shared_ptr<FramesStore> store) {
    int ret = avcodec_send_packet(avctx, pkt);
    if (ret < 0 && ret != AVERROR_EOF) {
        return ret;
    }
    
    std::unique_ptr<AVFrame, std::function<void(AVFrame*)>> frame(
        av_frame_alloc(),
        [](AVFrame *frame) {
            if (frame) {
                av_frame_free(&frame);
            }
        }
    );

    ret = avcodec_receive_frame(avctx, frame.get());
    if (ret >= 0) {
        store->save(frame.get());
    }
    else if (ret < 0 && ret != AVERROR(EAGAIN)) {
        return ret;
    }

    return 0;
}

int encode_frame(AVCodecContext *c, AVFrame *frame, std::shared_ptr<PacketsStore> store) {
    int ret;
    int size = 0;

    std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> pkt(
        av_packet_alloc(),
        [](AVPacket *pkt) {
            if (pkt) {
                av_packet_free(&pkt);
            }
        }
    );
    av_init_packet(pkt.get());

    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        return ret;
    }

    do {
        ret = avcodec_receive_packet(c, pkt.get());
        if (ret >= 0) {
            store->save(pkt.get());
            size += pkt->size;
            av_packet_unref(pkt.get());
        }
        else if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            return ret;
        }
    } while (ret >= 0);

    return size;
}

long long g_index = 0;
std::string gen_pic_name(AVFrame *avframe) {
    std::string type;
    switch (avframe->pict_type) {
    case AV_PICTURE_TYPE_NONE: {  ///< Undefined
        type = "NONE";
    }break;
    case AV_PICTURE_TYPE_I: {     ///< Intra
        type = "I";
    }break;
    case AV_PICTURE_TYPE_P: {     ///< Predicted
        type = "P";
    }break;
    case AV_PICTURE_TYPE_B: {     ///< Bi-dir predicted
        type = "B";
    }break;
    case AV_PICTURE_TYPE_S: {      ///< S(GMC)-VOP MPEG-4
        type = "S";
    }break;
    case AV_PICTURE_TYPE_SI: {     ///< Switching Intra
        type = "SI";
    }break;
    case AV_PICTURE_TYPE_SP: {     ///< Switching Predicted
        type = "SP";
    }break;
    case AV_PICTURE_TYPE_BI: {     ///< BI type
        type = "BI";
    }break;
    default:
        break;
    }
    std::string file_name = std::to_string(g_index++) 
        + "_(type)" + type  
        + "_(key_frame)" + std::to_string(avframe->key_frame)
        + "_(dts)" + std::to_string(avframe->pkt_dts)
        + "_(pts)" + std::to_string(avframe->pts)
        + ".jpg";
    return file_name;
}

void traverse_frame(AVFrame* avframe) {
    AVCodec *avcodec = avcodec_find_encoder(AV_CODEC_ID_BMP);
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext*)>> avcodec_ctx_output(
        avcodec_alloc_context3(avcodec),
        [](AVCodecContext *avctx) {
            if (avctx) {
                avcodec_free_context(&avctx);
            }
        }
    );

    avcodec_ctx_output->width = avframe->width;
    avcodec_ctx_output->height = avframe->height;
    avcodec_ctx_output->time_base.num = 1;
    avcodec_ctx_output->time_base.den = 1000;
    avcodec_ctx_output->pix_fmt = AV_PIX_FMT_BGR24;
    avcodec_ctx_output->codec_id = avcodec->id;
    avcodec_ctx_output->codec_type = AVMEDIA_TYPE_VIDEO;

    if (avcodec_open2(avcodec_ctx_output.get(), avcodec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        return;
    }

    std::shared_ptr<PacketsStore> packets_store = std::make_shared<PacketsStore>(av_packet_clone, av_packet_free);
    if (encode_frame(avcodec_ctx_output.get(), avframe, packets_store) < 0) {
        std::cerr << "encode_frame error" << std::endl;
        return;
    }

    std::string&& file_name = gen_pic_name(avframe);
    std::unique_ptr<std::FILE, std::function<int(FILE*)>> file(std::fopen(file_name.c_str(), "wb"), std::fclose);
    packets_store->traverse(
        [&file](AVPacket* packet){
            fwrite(packet->data, 1, packet->size, file.get());
        }
    );
}

void save_video_pic(AVFormatContext *avfmt_ctx, int stream_index, AVCodecContext *avcodec_ctx) {
    int err = av_seek_frame(avfmt_ctx, -1, avfmt_ctx->start_time, 0);
    do {
        std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> avpacket_src(
            av_packet_alloc(), 
            [](AVPacket *pkt) {
                if (pkt) {
                    av_packet_free(&pkt);
                }
            }
        );
        av_init_packet(avpacket_src.get());
        if (av_read_frame(avfmt_ctx, avpacket_src.get()) < 0) {
            break;
        }
        if (avpacket_src->stream_index != stream_index) {
            continue;
        }

        std::shared_ptr<FramesStore> frames_store = std::make_shared<FramesStore>(av_frame_clone, av_frame_free);
        decode_packet(avcodec_ctx, avpacket_src.get(), frames_store);
        frames_store->traverse(traverse_frame);
 
    } while (true);
}

bool analyze_av_format_context(AVFormatContext *avfmt_ctx) {
    for (decltype(avfmt_ctx->nb_streams) i = 0; i < avfmt_ctx->nb_streams; i++) {
        std::cout << "**************************************" << std::endl;
        std::cout << "Stream[" << i << "]:" << std::endl;
        AVStream *st = avfmt_ctx->streams[i];

        std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext*)>> avcodec_ctx(
            avcodec_alloc_context3(NULL), 
            [](AVCodecContext *avctx) {
                if (avctx) {
                    avcodec_free_context(&avctx);
                }
            }
        );
        if (0 > avcodec_parameters_to_context(avcodec_ctx.get(), st->codecpar)) {
            std::cerr << "avcodec_parameters_to_context error.stream " << i;
            continue;
        }
        AVCodec *avcodec = avcodec_find_decoder(avcodec_ctx->codec_id);
        if (avcodec->name) {
            std::cout << "name:\t" << avcodec->name << std::endl;
        }

        if (avcodec->long_name) {
            std::cout << "long_name:\t" << avcodec->long_name << std::endl;
        }

        std::cout << "type:\t" << get_type_str(avcodec->type) << std::endl;
        std::cout << "id:\t" << get_id_str(avcodec->id) << std::endl;

        std::cout << "bitrate:\t" << avcodec_ctx->bit_rate / 1000 << "kb/s" << std::endl;
        std::cout << "width:\t" << avcodec_ctx->width << std::endl;
        std::cout << "height:\t" << avcodec_ctx->height << std::endl;
        std::cout << "sample rate:\t" << avcodec_ctx->sample_rate << std::endl;
        std::cout << "channel:\t" << avcodec_ctx->channels << std::endl;
        
        std::cout << "start time:\t" << get_time(st->start_time, &st->time_base, 0) << "s" << std::endl;
        std::cout << "duration:\t" << get_time(st->duration, &st->time_base, 1) << "s" << std::endl;
        std::cout << "framerate:\t" << av_q2d(st->avg_frame_rate) << " fps" << std::endl;
        print_dict(st->metadata);

        if (avcodec->type == AVMEDIA_TYPE_VIDEO) {
            if (avcodec_open2(avcodec_ctx.get(), avcodec, NULL) < 0) {
                std::cerr << "Failed to open codec" << std::endl;
                return false;
            }
            save_video_pic(avfmt_ctx, i, avcodec_ctx.get());
        }

        std::cout << "**************************************" << std::endl;
    }
    return true;
}

void get_video_pictures(const char* file_path) {
    std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext*)>> avfmt_ctx_t(
        avformat_alloc_context(),
        [](AVFormatContext *s) {
            if (s) {
                avformat_close_input(&s);
            }
        }
    );
    
    AVFormatContext* && avfmt_ctx = avfmt_ctx_t.get();

    if (avformat_open_input(&avfmt_ctx, file_path, NULL, NULL)) {
        std::cerr << "avformat_open_input error";
        return;
    }

    for (unsigned int i = 0; i < avfmt_ctx->nb_streams; i++) {
        AVStream *st = avfmt_ctx->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

            std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext*)>> avcodec_ctx(
                avcodec_alloc_context3(NULL),
                [](AVCodecContext *avctx) {
                if (avctx) {
                    avcodec_free_context(&avctx);
                }
            }
            );
            if (0 > avcodec_parameters_to_context(avcodec_ctx.get(), st->codecpar)) {
                std::cerr << "avcodec_parameters_to_context error.stream " << i;
                continue;
            }
            AVCodec *avcodec = avcodec_find_decoder(avcodec_ctx->codec_id);
            if (avcodec_open2(avcodec_ctx.get(), avcodec, NULL) < 0) {
                std::cerr << "Failed to open codec" << std::endl;
                continue;
            }
            save_video_pic(avfmt_ctx, i, avcodec_ctx.get());
        }
    }
}


bool get_file_info(const char* file_path) {
    AVFormatContext *avfmt_ctx = NULL;
    bool err = true;

    do {

        if ((avfmt_ctx = avformat_alloc_context()) == NULL) {
            std::cerr << "avformat_alloc_context error";
            break;
        }

        if (avformat_open_input(&avfmt_ctx, file_path, NULL, NULL)) {
            std::cerr << "avformat_open_input error";
            break;
        }

        err = analyze_av_format_context(avfmt_ctx);

        print_dict(avfmt_ctx->metadata);

    } while (0);

    if (avfmt_ctx) {
        avformat_close_input(&avfmt_ctx);
    }

    return err;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    avdevice_register_all();
    avformat_network_init();
    get_video_pictures(FLAGS_url.c_str());
    //std::cout << FLAGS_url << " info is " << get_file_info(FLAGS_url.c_str());
    return 0;
}
