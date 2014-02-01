#if defined(__cplusplus)
extern "C" {
#endif

#ifdef _MSC_VER
#define inline __inline
#endif

#include <stdint.h>
#include <inttypes.h>

#include <stdio.h>
#include <stdlib.h>

#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#if defined(__cplusplus)
};
#endif

#define ERRSTR_LEN      4096

void remux(const char *infname, const char *outfname, const char *out_fmt_name){
    int res;
    unsigned int    i=0;
    char error_av[ERRSTR_LEN];
    int *stream_map=NULL, idx;

    AVFormatContext *ic=NULL, *oc=NULL;

    AVStream *stream=NULL;
    AVPacket inpacket, outpacket;
    ic = avformat_alloc_context();
    if(!ic){
        exit(1);
    }
    //ic->flags |= AVFMT_FLAG_NONBLOCK;


    /* 
    	Open input 
    */
    res = avformat_open_input(&ic, infname, NULL, NULL);
    av_log(NULL, AV_LOG_DEBUG, "avformat_open_input: %s for '%s'\n", (res>=0)?"passed":"failed", infname);
    if(res<0){
        av_log(NULL, AV_LOG_ERROR, "Failed to open input file\n");
        memset(error_av, 0, ERRSTR_LEN);
        av_make_error_string(error_av, ERRSTR_LEN, res);
        av_log(NULL, AV_LOG_INFO, "error: %s\n", error_av);

        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "Opening input file... [ DONE ]\n");
    res = avformat_find_stream_info(ic, NULL);
    if(res<0){
        av_log(NULL, AV_LOG_INFO, "Finding input format... [ FAIL ]\n");
        memset(error_av, 0, ERRSTR_LEN);
        av_make_error_string(error_av, ERRSTR_LEN, res);
        av_log(NULL, AV_LOG_INFO, "error: %s\n", error_av);

        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "Guessing input format... [ DONE ]\n");


    /* 
    	Open output 
    */
    oc = avformat_alloc_context();
    if(!oc){
        av_log(NULL, AV_LOG_ERROR, "Allocating output context... [ FAIL ]\n");
        exit(1);
    }
    oc->oformat = av_guess_format(out_fmt_name, NULL, NULL);
    if(oc->oformat == NULL){
        av_log(NULL, AV_LOG_ERROR, "Guessing output format... [ FAIL ]\n");
        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "Guessing output format: %s... [ DONE ]\n", oc->oformat->name);
    memmove(oc->filename, outfname, strlen(outfname));

    res = avio_open(&oc->pb, outfname, AVIO_FLAG_WRITE);
    if(res<0){
        memset(error_av, 0, ERRSTR_LEN);
        av_make_error_string(error_av, ERRSTR_LEN, res);
        av_log(NULL, AV_LOG_INFO, "avio_open error: %s\n", error_av);
        exit(1);
    }


    /* 
    	map input video streams to output streams 
    */
    stream_map = (int *)calloc(ic->nb_streams, sizeof(ic->nb_streams));
    memset(stream_map, 0xff, ic->nb_streams*sizeof(ic->nb_streams));
    for(i=0; i<ic->nb_streams; i++){
        if(ic->streams[i]->codec->codec_type!=AVMEDIA_TYPE_VIDEO){
            continue;
        }
        stream = avformat_new_stream(oc, ic->streams[i]->codec->codec);
        if(!stream){
            av_log(NULL, AV_LOG_INFO, "Adding output stream type %i [ FAIL ]\n", ic->streams[i]->codec->codec_type);
            exit(1);
        }
        res = avcodec_copy_context(stream->codec, ic->streams[i]->codec);
        if(res!=0){
            av_log(NULL, AV_LOG_ERROR, "Copying codec context for stream %i [ FAIL ]\n", i);
            memset(error_av, 0, ERRSTR_LEN);
            av_make_error_string(error_av, ERRSTR_LEN, res);
            av_log(NULL, AV_LOG_INFO, "error: %s\n", error_av);

            exit(1);
        }
        av_log(NULL, AV_LOG_INFO, "Adding output stream type %i [ DONE ]\n", ic->streams[i]->codec->codec_type);
        stream->id = oc->nb_streams-1;
        stream->codec->codec_tag = 0;
        
        stream_map[i] = stream->index;
        stream = NULL;
    }
    av_log(NULL, AV_LOG_INFO, "Mapping all video streams... [ DONE ]\n");
    oc->duration = ic->duration;
    oc->bit_rate = ic->bit_rate;
    
    
    /* 
    	write output header 
    */
    res = avformat_write_header(oc, NULL);
    if(res<0){
        av_log(NULL, AV_LOG_ERROR, "Writing output header... [ FAIL ]\n");
        memset(error_av, 0, ERRSTR_LEN);
        av_make_error_string(error_av, ERRSTR_LEN, res);
        av_log(NULL, AV_LOG_INFO, "error: %s\n", error_av);

        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "Writing output header... [ DONE ]\n");

    /* 
    	Remuxing: copy all input video frames to output w/o decoding
    */
    for(i=0; av_read_frame(ic, &inpacket)>=0; i++, av_free_packet(&inpacket)) {
        idx = inpacket.stream_index;
        if((unsigned)idx>=ic->nb_streams){
            // drop the packet;
            av_log(NULL, AV_LOG_WARNING, "Invalid stream #%u, max expected %u\n", (unsigned)idx, ic->nb_streams);
            continue;
        }
        stream = ic->streams[idx];
        if(stream_map[idx]==-1){
            //av_log(NULL, AV_LOG_WARNING, "Skipping packet from stream #%i\n", idx);
            continue;
        }
        switch(ic->streams[stream_map[idx]]->codec->codec_type){
            case AVMEDIA_TYPE_VIDEO:
                break;
            default:
                continue;
        }
        av_log(NULL, AV_LOG_DEBUG, "Remuxing packet-stream %i->%i\r", idx, stream_map[idx]);
        av_copy_packet(&outpacket, &inpacket);
        outpacket.stream_index = stream_map[idx];
        av_interleaved_write_frame(oc, &outpacket);
        av_free_packet(&outpacket);
    }
    av_log(NULL, AV_LOG_INFO, "Remuxing video streams... [ DONE ]\n");

    
    /* 
    	write output trailer 
    */
    res = av_write_trailer(oc);
    if(res<0){
        av_log(NULL, AV_LOG_INFO, "Writing output trailer... [ FAIL ]\n");
        memset(error_av, 0, ERRSTR_LEN);
        av_make_error_string(error_av, ERRSTR_LEN, res);
        av_log(NULL, AV_LOG_ERROR, "error: %s\n", error_av);

        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "Writing output trailer... [ DONE ]\n");
    
    /*
    	Close input output
    */
    for(idx=0;(unsigned)idx<oc->nb_streams; idx++){
        avcodec_close(oc->streams[idx]->codec);
        av_freep(&oc->streams[idx]);
        av_freep(&oc->streams[idx]);
    }
    if (oc->oformat&&!(oc->oformat->flags & AVFMT_NOFILE))
        avio_close(oc->pb);
    av_log(NULL, AV_LOG_INFO, "Closing output format... [ DONE ]\n");
    
    avformat_close_input(&ic);
    if(stream_map){
        free(stream_map);
        stream_map = NULL;
    }
}

void init_framework(){
    avcodec_register_all();
    avfilter_register_all();
    av_register_all();
}

void    set_log_level(int level){
    av_log_set_level(level);
}

int main(int argc, char* argv[]){
    init_framework();
    set_log_level(AV_LOG_DEBUG);

    if(argc!=3){
        printf("Usage: <inpufile> <outputfile>");
        return -1;
    }
    
    remux(argv[1], argv[2], "avi");
    return 0;
}


