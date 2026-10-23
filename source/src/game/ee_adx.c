/* Native standalone ADX playback. The guest retains AIX demux/voices and SFX.
 * ADX compressed bytes are decoded by the installed FFmpeg library outside the
 * audio lock, then consumed by the SDL mixer with exact sample loop points.
 * No periodic watchdog, stale-buffer muting or automatic track restart. */
#if !defined(__SWITCH__) && !defined(SH2_STOCK)
#include "../runtime/rt.h"
#include "desktop.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <limits.h>
#include <strings.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
/* A bounded full-track buffer keeps producer scheduling out of music playback.
 * 512 MiB is the upper bound per decoded track; ordinary game music is smaller. */
#define PCM_MAX_FRAMES (64u*1024u*1024u)
static int decode_adx(const char *path,float **samples,unsigned *count,unsigned *loop_start,unsigned *loop_end) {
    uint8_t header[64]={0}; FILE *f=fopen(path,"rb");
    if (!f) return 0;
    size_t got=fread(header,1,sizeof header,f); fclose(f);
    if (got<20 || header[0]!=0x80 || header[1]!=0 || header[4]!=3 || header[5]!=18 || header[6]!=4) return 0;
    unsigned rate=be32(header+8),total=be32(header+12),start=0,end=0;
    if (!rate || !total || (uint64_t)total*44100/rate>PCM_MAX_FRAMES) return 0;
    if (got>=56 && header[18]==4 && be32(header+36)) { start=be32(header+40);end=be32(header+48); }
    else if (got>=44 && header[18]==3 && be32(header+24)) { start=be32(header+28);end=be32(header+36); }
    if (start>=end || end>total) start=end=0;
    AVFormatContext *format=NULL;AVCodecContext *codec=NULL;SwrContext *swr=NULL;
    AVPacket *packet=NULL;AVFrame *frame=NULL;float *pcm=NULL;int ok=0;
    if (avformat_open_input(&format,path,NULL,NULL)<0 || avformat_find_stream_info(format,NULL)<0) goto done;
    int stream=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if (stream<0) goto done;
    const AVCodec *decoder=avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
    if (!decoder || decoder->id!=AV_CODEC_ID_ADPCM_ADX) goto done;
    codec=avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec,format->streams[stream]->codecpar)<0 || avcodec_open2(codec,decoder,NULL)<0) goto done;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_FLT,44100,&codec->ch_layout,codec->sample_fmt,codec->sample_rate,0,NULL)<0 || swr_init(swr)<0) goto done;
    unsigned cap=(unsigned)((uint64_t)total*44100/rate)+8192,n=0;
    if (cap>PCM_MAX_FRAMES) goto done;
    pcm=malloc((size_t)cap*2*sizeof(float));packet=av_packet_alloc();frame=av_frame_alloc();
    if (!pcm || !packet || !frame) goto done;
    int eof=0;
    while (!eof) {
        int rc=av_read_frame(format,packet);
        if (rc<0) { eof=1; if (rc!=AVERROR_EOF) goto done; rc=avcodec_send_packet(codec,NULL); }
        else if (packet->stream_index==stream) rc=avcodec_send_packet(codec,packet);
        else { av_packet_unref(packet);continue; }
        av_packet_unref(packet);
        if (rc<0 && rc!=AVERROR_EOF) goto done;
        while ((rc=avcodec_receive_frame(codec,frame))>=0) {
            uint8_t *out=(uint8_t*)(pcm+2*n);
            int produced=swr_convert(swr,&out,cap-n,(const uint8_t**)frame->extended_data,frame->nb_samples);
            av_frame_unref(frame);
            if (produced<0 || (unsigned)produced>=cap-n) goto done;
            n+=(unsigned)produced;
        }
        if (rc!=AVERROR_EOF && rc!=AVERROR(EAGAIN)) goto done;
    }
    for (;;) {
        uint8_t *out=(uint8_t*)(pcm+2*n);
        int produced=swr_convert(swr,&out,cap-n,NULL,0);
        if (produced<0 || (unsigned)produced>=cap-n) goto done;
        n+=(unsigned)produced;if (!produced) break;
    }
    unsigned expected=(unsigned)((uint64_t)total*44100/rate);
    if (n<expected) goto done;
    *samples=pcm;*count=expected;pcm=NULL;
    *loop_start=(unsigned)((uint64_t)start*44100/rate);
    *loop_end=(unsigned)((uint64_t)end*44100/rate);
    ok=1;
done:
    free(pcm);av_frame_free(&frame);av_packet_free(&packet);swr_free(&swr);
    avcodec_free_context(&codec);avformat_close_input(&format);return ok;
}

void sub_0055BF60(void) {
    uint32_t owner=ARG(0),name=ARG(1);
    if (owner && name && !getenv("SH2_LEGACY_ADX")) {
        const char *guest=GSTR(name);size_t n=strlen(guest);
        if (n>4 && !strcasecmp(guest+n-4,".adx")) {
            char path[512];rt_path(guest,path,sizeof path);
            /* Preserve the guest voice/AIX timing APIs until their separate
             * sample-clock contracts are adapted and tested. */
            if (strstr(path,"/voice/") || !strstr(path,"/sound/adx/")) {
                sub_0055BF60_desktop_original();return;
            }
            float *pcm=NULL;unsigned frames=0,start=0,end=0;
            if (decode_adx(path,&pcm,&frames,&start,&end)) {
                /* Dispose any previous guest/native stream through its normal
                 * Stop contract before handing the new PCM to the mixer. */
                uint32_t arg=owner;guest_call(0x0055C6A0u,1,&arg,0);
                if (ds_music_start(owner,pcm,frames,start,end)) {
                    ds_music_volume(owner,(int16_t)MEM16(owner+0x40)*10);
                    rt_log("audio: native ADX %s owner=%08X frames=%u loop=%u..%u",guest,owner,frames,start,end);
                    g_esp+=4;return;
                }
                free(pcm);
            }
            rt_log("audio: native ADX unavailable, keeping guest decoder for %s",guest);
        }
    }
    sub_0055BF60_desktop_original();
}
void sub_0055C6A0(void) {
    if (ds_music_stop(ARG(0))) {g_esp+=4;return;}
    sub_0055C6A0_desktop_original();
}
void sub_0055C720(void) {
    int status=ds_music_status(ARG(0));
    if (status>=0) {g_eax=(uint32_t)status;g_esp+=4;return;}
    sub_0055C720_desktop_original();
}
void sub_0055CC60(void) {
    if (ds_music_volume(ARG(0),(int32_t)ARG(1)*10)) {
        MEM16(ARG(0)+0x40)=(uint16_t)ARG(1);g_esp+=4;return;
    }
    sub_0055CC60_desktop_original();
}
#endif
