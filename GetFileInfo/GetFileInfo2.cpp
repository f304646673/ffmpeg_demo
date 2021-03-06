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

template<typename Src, typename Dst>
class TransAvComponent {
public:
    virtual bool save(Src *s, Dst *d, int retcode) = 0;
};

template<typename Src, typename Dst>
class TransStore :
    public TransAvComponent<Src, Dst>
{
public:
    TransStore(std::function<Dst*(const Dst*)> clone, std::function<void(Dst**)> free) {
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
    virtual void save(Src *s, Dst *d) {
        Dst *p = _clone(d);
        _store.push_back(p);
    }
private:
    std::vector<Dst*> _store;
    std::function<Dst*(const Dst*)> _clone;
    std::function<void(Dst**)> _free;
};


int decode_packet(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *pkt) {
    int ret;
    *got_frame = 0;
    if (pkt) {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        return ret;
    }
    
    if (ret >= 0) {
        *got_frame = 1;
    }

    return 0;
}

long long g_index = 0;
int encode_frame(AVCodecContext *c, AVFrame *frame, AVPacket *pkt) {
    int ret;
    int size = 0;

    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        return ret;
    }

    std::string file_name = std::to_string(g_index++) + ".jpg";
    std::unique_ptr<std::FILE, std::function<int(FILE*)>> file(std::fopen(file_name.c_str(), "wb"), std::fclose);
    do {
        ret = avcodec_receive_packet(c, pkt);
        if (ret >= 0) {
            size += pkt->size;
            fwrite(pkt->data, 1, pkt->size, file.get());
            av_packet_unref(pkt);
        }
        else if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            return ret;
        }
    } while (ret >= 0);

    return size;
}

void get_thumbnail(AVFormatContext *avfmt_ctx, int stream_index, AVCodecContext *avcodec_ctx) {

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

        int got_picture_ptr = 0;
        std::unique_ptr<AVFrame, std::function<void(AVFrame*)>> avframe_src(
            av_frame_alloc(),
            [](AVFrame *frame) {
                if (frame) {
                    av_frame_free(&frame);
                }
            }
        );
        decode_packet(avcodec_ctx, avframe_src.get(), &got_picture_ptr, avpacket_src.get());
        if (got_picture_ptr) {

            /*
            const int &dst_width = avframe_src->width / 2;
            const int &dst_height = avframe_src->height / 2;

            std::unique_ptr<SwsContext, std::function<void(SwsContext*)>> scaler_ctx(
                sws_getContext(avframe_src->width, avframe_src->height, avcodec_ctx->pix_fmt,
                    dst_width, dst_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR,
                    nullptr, nullptr, nullptr),
                sws_freeContext
            );

             std::unique_ptr<AVFrame, std::function<void(AVFrame*)>> avframe_dst(
                av_frame_alloc(), 
                [](AVFrame *frame) {
                    if (frame) {
                        av_frame_free(&frame);
                    }
                }
            );
             std::unique_ptr<uint8_t, std::function<void(void*)>> dst_buffer(
                static_cast<uint8_t*>(
                    av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, dst_width, dst_height, 1))
                    ),
                av_free
            );

            av_image_fill_arrays(avframe_dst->data, avframe_dst->linesize,
                static_cast<uint8_t*>(dst_buffer.get()), AV_PIX_FMT_YUV420P, dst_width, dst_height, 1);

            sws_scale(scaler_ctx.get(), avframe_src->data, avframe_src->linesize, 0, avframe_src->height, avframe_dst->data, avframe_dst->linesize);
            
            */
            AVCodec *avcodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
            std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext*)>> avcodec_ctx_output(
                avcodec_alloc_context3(avcodec), 
                [](AVCodecContext *avctx) {
                    if (avctx) {
                        avcodec_free_context(&avctx);
                    }
                }
            );

            avcodec_ctx_output->width = avframe_src->width;
            avcodec_ctx_output->height = avframe_src->height;
            avcodec_ctx_output->time_base.num = 1;
            avcodec_ctx_output->time_base.den = 16;
            avcodec_ctx_output->pix_fmt = AV_PIX_FMT_YUVJ420P;
            avcodec_ctx_output->codec_id = AV_CODEC_ID_MJPEG;
            avcodec_ctx_output->codec_type = avcodec_ctx->codec_type; //AVMEDIA_TYPE_VIDEO

            if (avcodec_open2(avcodec_ctx_output.get(), avcodec, nullptr) < 0) {
                std::cerr << "Failed to open codec" << std::endl;
                return;
            }

            std::unique_ptr<AVPacket, std::function<void(AVPacket*)>> avpacket_dst(
                av_packet_alloc(), 
                [](AVPacket *pkt) {
                    if (pkt) {
                        av_packet_free(&pkt);
                    }
                }
            );
            av_init_packet(avpacket_dst.get());
            /*
            int gotPacket = -1;
            err = avcodec_encode_video2(avcodec_ctx_output(), avpacket_dst(), avframe_dst(), &gotPacket);
 
            std::unique_ptr<std::FILE, std::function<int(FILE*)>> file(fopen("a.jpg", "wb"), std::fclose); 
            fwrite(avpacket_dst->data, 1, avpacket_dst->size, file.get());
            avframe_dst->height = avframe_src->height;
            avframe_dst->width = avframe_src->width;
            */
            if (encode_frame(avcodec_ctx_output.get(), avframe_src.get(), avpacket_dst.get()) < 0) {
                std::cerr << "encode_frame error" << std::endl;
                continue;
            }
        }
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
        }
        AVCodec *avcodec = avcodec_find_decoder(avcodec_ctx->codec_id);
        if (avcodec->name) {
            std::cout << "name:\t" << avcodec->name << std::endl;
        }

        if (avcodec->long_name) {
            std::cout << "long_name:\t" << avcodec->long_name << std::endl;
        }

        if (avcodec->type == AVMEDIA_TYPE_VIDEO) {
            if (avcodec_open2(avcodec_ctx.get(), avcodec, NULL) < 0) {
                std::cerr << "Failed to open codec" << std::endl;
                return false;
            }
            get_thumbnail(avfmt_ctx, i, avcodec_ctx.get());
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

        std::cout << "**************************************" << std::endl;
    }
    return true;
}

bool get_file_info(const char* file_path) {
 //   AVIOContext *avio_ctx = NULL;
    AVFormatContext *avfmt_ctx = NULL;
    bool err = true;

    do {
//        if (avio_open(&avio_ctx, file_path, AVIO_FLAG_READ)) {
//            std::cerr << "avio_open error";
//            break;
//        }

        if ((avfmt_ctx = avformat_alloc_context()) == NULL) {
            std::cerr << "avformat_alloc_context error";
            break;
        }

 //       avfmt_ctx->pb = avio_ctx;

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

 //   if (avio_ctx) {
 //       avio_close(avio_ctx);
 //   }

    return err;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    avdevice_register_all();
    avformat_network_init();
    std::cout << FLAGS_url << " info is " << get_file_info(FLAGS_url.c_str());
    return 0;
}
