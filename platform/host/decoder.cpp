#include "media.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <vector>
#include <cstring>
#ifndef TABNAMP_DECODER_FACTORY
#define TABNAMP_DECODER_FACTORY make_compressed_decoder
#endif
namespace tabnamp {
class AvDecoder final:public Decoder {
    AVFormatContext* format_=nullptr;AVCodecContext* codec_=nullptr;SwrContext* swr_=nullptr;
    AVPacket* packet_=nullptr;AVFrame* frame_=nullptr;int stream_=-1;AVIOContext* custom_=nullptr;bool eof_=false,drained_=false;
    std::vector<float> pending_;size_t at_=0;int64_t discard_before_=-1;unsigned decoded_frames_=0;
    void tags(){
        auto read=[&](const char* key){AVDictionaryEntry* t=av_dict_get(format_->metadata,key,nullptr,0);if(!t)t=av_dict_get(format_->streams[stream_]->metadata,key,nullptr,0);return t?std::string(t->value).substr(0,1024):std::string();};
        auto title=read("title");if(!title.empty())info.title=title;info.artist=read("artist");info.album=read("album");info.album_artist=read("album_artist");info.track=read("track");info.disc=read("disc");info.date=read("date");info.genre=read("genre");
    }
public:
    ~AvDecoder()override{av_packet_free(&packet_);av_frame_free(&frame_);swr_free(&swr_);avcodec_free_context(&codec_);avformat_close_input(&format_);if(custom_){av_freep(&custom_->buffer);avio_context_free(&custom_);}}
    bool open(const std::string& uri,std::string& error)override{
#ifdef TABNAMP_LEGACY_ONLY
        if(remote(uri)){error="WMA/ATRAC pela rede ainda nao suportados";return false;}
#endif
        av_max_alloc(4*1024*1024);
        format_=avformat_alloc_context();if(!format_){error="Sem memoria demuxer";return false;}
        format_->probesize=256*1024;format_->max_analyze_duration=3*AV_TIME_BASE;format_->max_probe_packets=256;format_->max_streams=8;
        format_->interrupt_callback.callback=[](void* p){auto* self=static_cast<AvDecoder*>(p);return int(self->cancelled&&self->cancelled());};format_->interrupt_callback.opaque=this;
        if(byte_stream){auto* buffer=static_cast<unsigned char*>(av_malloc(32768));if(!buffer){error="Sem memoria AVIO";return false;}custom_=avio_alloc_context(buffer,32768,0,this,[](void* ptr,uint8_t* out,int n){auto* self=static_cast<AvDecoder*>(ptr);std::string error;int ret=self->byte_stream(out,n,error);return ret>0?ret:ret==0?AVERROR_EOF:ret==-2?AVERROR(EAGAIN):AVERROR(EIO);},nullptr,nullptr);if(!custom_){av_free(buffer);return false;}format_->pb=custom_;format_->flags|=AVFMT_FLAG_CUSTOM_IO;}

        AVDictionary* opts=nullptr;av_dict_set(&opts,"rw_timeout","3000000",0);av_dict_set(&opts,"tls_verify","1",0);av_dict_set(&opts,"protocol_whitelist","file,http,https,tcp,tls",0);
        int ret=avformat_open_input(&format_,byte_stream?nullptr:uri.c_str(),nullptr,&opts);av_dict_free(&opts);
        if(ret<0){error="Nao foi possivel abrir fonte de audio";return false;}
        if(std::string(format_->iformat->name).find("hls")!=std::string::npos){error="HLS ainda nao suportado";return false;}
        if(avformat_find_stream_info(format_,nullptr)<0||(stream_=av_find_best_stream(format_,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0))<0){error="Audio nao encontrado";return false;}
        auto* params=format_->streams[stream_]->codecpar;const AVCodec* decoder=avcodec_find_decoder(params->codec_id);
        if(!decoder){error="Perfil de codec indisponivel neste build";return false;}
        codec_=avcodec_alloc_context3(decoder);
        if(!codec_||avcodec_parameters_to_context(codec_,params)<0||avcodec_open2(codec_,decoder,nullptr)<0){error="Falha ao abrir decoder";return false;}
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if(swr_alloc_set_opts2(&swr_,&stereo,AV_SAMPLE_FMT_FLT,codec_->sample_rate,&codec_->ch_layout,codec_->sample_fmt,codec_->sample_rate,0,nullptr)<0||swr_init(swr_)<0){error="Falha no conversor PCM";return false;}
        packet_=av_packet_alloc();frame_=av_frame_alloc();if(!packet_||!frame_){error="Sem memoria decoder";return false;}
        info.rate=codec_->sample_rate;info.channels=codec_->ch_layout.nb_channels;info.bitrate=codec_->bit_rate;info.codec=decoder->name;info.live=remote(uri);
        info.seekable=!info.live&&!byte_stream;info.duration_ms=info.live?-1:(format_->duration==AV_NOPTS_VALUE?-1:format_->duration/1000);
        info.title=uri.substr(uri.find_last_of('/')+1);tags();return true;
    }
    int read(float* out,size_t capacity,std::string& error)override{
        for(int attempts=0;attempts<200;attempts++){
            if(at_<pending_.size()) {size_t frames=std::min(capacity,(pending_.size()-at_)/2);std::copy_n(pending_.data()+at_,frames*2,out);at_+=frames*2;return int(frames);}
            int ret=avcodec_receive_frame(codec_,frame_);
            if(ret==0){
                if(frame_->nb_samples>65536){error="Frame excede limite";return -1;}
                pending_.resize((frame_->nb_samples+512)*2);uint8_t* dst=reinterpret_cast<uint8_t*>(pending_.data());
                int n=swr_convert(swr_,&dst,int(pending_.size()/2),const_cast<const uint8_t**>(frame_->extended_data),frame_->nb_samples);
                if(n<0){error="Falha de conversao PCM";return -1;}pending_.resize(n*2);at_=0;
                if(discard_before_>=0&&frame_->best_effort_timestamp!=AV_NOPTS_VALUE){int64_t ms=av_rescale_q(frame_->best_effort_timestamp,format_->streams[stream_]->time_base,AVRational{1,1000});if(ms<discard_before_)at_=std::min<size_t>(pending_.size(),(discard_before_-ms)*info.rate/1000*2);else discard_before_=-1;}
                if(info.live&&format_->pb&&decoded_frames_++%64==0){uint8_t* packet=nullptr;if(av_opt_get(format_->pb,"icy_metadata_packet",AV_OPT_SEARCH_CHILDREN,&packet)>=0&&packet){std::string metadata(reinterpret_cast<char*>(packet));auto at=metadata.find("StreamTitle='");if(at!=std::string::npos){at+=13;auto end=metadata.find("';",at);if(end!=std::string::npos)info.title=metadata.substr(at,std::min<size_t>(1024,end-at));}}av_free(packet);}
                av_frame_unref(frame_);continue;
            }
            if(ret==AVERROR_EOF||drained_)return 0;
            if(ret!=AVERROR(EAGAIN)){error="Erro de decodificacao";return -1;}
            if(eof_){avcodec_send_packet(codec_,nullptr);drained_=true;continue;}
            ret=av_read_frame(format_,packet_);
            if(ret==AVERROR_EOF){if(info.live&&format_->pb&&format_->pb->error<0&&format_->pb->error!=AVERROR_EOF){error="Falha de leitura da rede";return -1;}eof_=true;continue;}if(ret<0){error="Falha na fonte/rede";return -1;}
            if(packet_->stream_index==stream_){ret=avcodec_send_packet(codec_,packet_);av_packet_unref(packet_);if(ret<0){error="Pacote de audio invalido";return -1;}}
            else av_packet_unref(packet_);
        }
        return -2;
    }
    bool seek(int64_t ms,std::string& error)override{
        if(!info.seekable){error="Stream ao vivo nao permite seek";return false;}
        int64_t timestamp=av_rescale_q(ms,AVRational{1,1000},format_->streams[stream_]->time_base);
        if(av_seek_frame(format_,stream_,timestamp,AVSEEK_FLAG_BACKWARD)<0){error="Seek falhou";return false;}
        avcodec_flush_buffers(codec_);swr_close(swr_);swr_init(swr_);pending_.clear();at_=0;eof_=drained_=false;discard_before_=ms;return true;
    }
};
std::unique_ptr<Decoder> TABNAMP_DECODER_FACTORY(){return std::unique_ptr<Decoder>(new AvDecoder);}
}
