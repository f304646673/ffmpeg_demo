#pragma once
#include <string>
#include <map>
#include <iostream>

extern "C" {
#include "libavformat/avformat.h"
};

const std::map<AVMediaType, std::string> g_avmedia_type_map = {
    {AVMEDIA_TYPE_UNKNOWN, "AVMEDIA_TYPE_UNKNOWN"},
    {AVMEDIA_TYPE_VIDEO, "AVMEDIA_TYPE_VIDEO"},
    {AVMEDIA_TYPE_AUDIO, "AVMEDIA_TYPE_AUDIO"},
    {AVMEDIA_TYPE_DATA, "AVMEDIA_TYPE_DATA"},
    {AVMEDIA_TYPE_SUBTITLE, "AVMEDIA_TYPE_SUBTITLE"},
    {AVMEDIA_TYPE_ATTACHMENT, "AVMEDIA_TYPE_ATTACHMENT"},
    {AVMEDIA_TYPE_NB, "AVMEDIA_TYPE_NB"}
};

 std::string get_type_str(AVMediaType type) {
    auto it = g_avmedia_type_map.find(type);
    if (it != g_avmedia_type_map.end()) {
        return it->second;
    }
    return std::string("");
}
