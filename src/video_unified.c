/* MIT; derived from kuslota/InkBoy. */
#include "c1_gray32.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include "frame_reader.h"
#define BYTES (C1_PIXELS/8)
static const uint8_t phase_a[31]={1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,4,5,8,10};
static const uint8_t vsh[31]={56,16,17,17,17,18,19,19,20,21,21,23,24,26,28,30,32,34,37,41,44,49,53,57,61,66,69,72,74,75,76};
#define REG(cmd,...) do { const uint8_t d[]={__VA_ARGS__};     if(b->write(b->ctx,cmd,d,sizeof(d)))return -1; } while(0)
static int wait_idle(const c1_bus *b,unsigned timeout,int edge){
    uint64_t start=b->time_us(b->ctx);int seen=0;
    if(!edge)b->sleep_us(b->ctx,2000);
    for(;;){int high=b->busy(b->ctx);if(high<0)return -1;
        if(high)seen=1;if(!high&&(!edge||seen))return 0;
        uint64_t elapsed=b->time_us(b->ctx)-start;
        if(elapsed>=timeout||(edge&&!seen&&elapsed>=400000))return -1;
        b->sleep_us(b->ctx,edge?50:2000);
    }
}
static int update(const c1_bus *b,uint8_t control,int edge,unsigned timeout){
    if(edge&&b->busy(b->ctx)!=0)return -1;
    REG(0x45,0,0,0x27,1);
    REG(0x22,control);if(b->write(b->ctx,0x20,0,0))return -1;
    return wait_idle(b,timeout,edge);
}
static uint64_t pending_start;
static int finish_pending(const c1_bus *b){
 if(!pending_start)return 0;
 for(;;){int high=b->busy(b->ctx);if(high<0)return -1;if(!high){pending_start=0;return 0;}
  if(b->time_us(b->ctx)-pending_start>5000000)return -1;b->sleep_us(b->ctx,50);}
}
static int start_pending(const c1_bus *b){
 if(pending_start||b->busy(b->ctx)!=0)return -1;
 REG(0x45,0,0,0x27,1);REG(0x22,0x0c);if(b->write(b->ctx,0x20,0,0))return -1;
 uint64_t t=b->time_us(b->ctx);
 for(;;){int high=b->busy(b->ctx);if(high<0)return -1;if(high){pending_start=t;return 0;}
  if(b->time_us(b->ctx)-t>400000)return -1;b->sleep_us(b->ctx,50);}
}
static int window(const c1_bus *b){
    REG(0x44,0x12,0);REG(0x45,0,0,0x27,1);REG(0x4e,0x12);REG(0x4f,0,0);return 0;
}
static uint8_t plane_cache[2][BYTES];static int valid_cache[2];
static int plane(const c1_bus *b,uint8_t command,const uint8_t *data){
 unsigned bank=command==0x26;uint8_t changed[C1_WIDTH]={0},packet[BYTES];
 for(unsigned x=0;x<C1_WIDTH;x++){
  if(!valid_cache[bank]){changed[x]=1;continue;}
  for(unsigned y=0;y<C1_HEIGHT/8;y++)if(data[y*C1_WIDTH+x]!=plane_cache[bank][y*C1_WIDTH+x]){changed[x]=1;break;}
 }
 unsigned x=0;
 while(x<C1_WIDTH){
  while(x<C1_WIDTH&&!changed[x])x++;if(x==C1_WIDTH)break;
  unsigned lo=x,hi=x,last=x;
  for(x++;x<C1_WIDTH;x++){if(changed[x]){last=x;hi=x;}else if(x-last>8)break;}
  unsigned n=0;for(unsigned y=0;y<C1_HEIGHT/8;y++)for(unsigned c=lo;c<=hi;c++)packet[n++]=data[y*C1_WIDTH+c];
  REG(0x45,lo&255,lo>>8,hi&255,hi>>8);REG(0x4e,0x12);REG(0x4f,lo&255,lo>>8);
  if(b->write(b->ctx,command,packet,n))return -1;
 }
 memcpy(plane_cache[bank],data,BYTES);valid_cache[bank]=1;return 0;
}
static int lut(const c1_bus *b,unsigned a){
    uint8_t data[153]={0};data[13]=0xa0;data[25]=0x50;data[37]=a?0x80:0x40;data[67]=1;data[68]=1;
    memset(data+144,0x22,6);data[144]=0x27;
    return b->write(b->ctx,0x32,data,sizeof(data));
}

#include "pixel_response.h"
#include "media.h"
extern int video_take_seek(void);
extern uint64_t video_request_us(void);
extern int video_action(void);

static const unsigned short rankq[32]={0,132,264,396,528,660,792,924,1057,1189,1321,1453,1585,1717,1849,1981,2114,2246,2378,2510,2642,2774,2906,3038,3171,3303,3435,3567,3699,3831,3963,4096};
static int video_power(const c1_bus*b){
    uint8_t zero[BYTES]={0};valid_cache[0]=valid_cache[1]=0;
    REG(0x21,0,0x80);REG(0x74,0x54);REG(0x7e,0x3b);REG(0x2c,0);
    REG(0x03,0x15);REG(0x04,0x41,0x41,0x32);REG(0x3a,0);REG(0x3b,4);
    REG(0x11,6);if(window(b))return -1;
    REG(0x37,0,0,0,0,0,0,0,0,0,0);REG(0x3c,0x80);REG(0x18,0x80);REG(0x3f,2);
    if(lut(b,1)||plane(b,0x24,zero)||plane(b,0x26,zero)||update(b,0xc0,1,5000000))return -1;
    return 0; /* Resume immediately after successful power-on BUSY completion. */
}
static int static_lut(const c1_bus*b,unsigned a){
    uint8_t data[153]={0};data[13]=0x80;data[25]=0x40;data[67]=a;
    memset(data+144,0x22,6);data[144]=0x27;
    return b->write(b->ctx,0x32,data,sizeof(data));
}
static int static_from_white(const c1_bus*b,const uint8_t*ranks,int region_only){
    uint8_t zero[BYTES]={0},mask[BYTES];unsigned last=0;
    for(unsigned i=0;i<C1_PIXELS;i++){if(ranks[i]>31)return -1;if(ranks[i]>last)last=ranks[i];}
    valid_cache[0]=valid_cache[1]=0;
    REG(0x04,vsh[0],0xa8,0x32);REG(0x11,6);if(window(b))return -1;
    if(static_lut(b,1)||plane(b,0x24,zero)||plane(b,0x26,zero))return -1; /* Already powered by video. */
    uint64_t mask_us=0,write_us=0,busy_us=0;
    for(unsigned stage=0;stage<last;stage++){
        uint64_t st=b->time_us(b->ctx);
        if(video_action()==2)return -2;
        REG(0x04,vsh[stage],0xa8,0x32);memset(mask,0,BYTES);
        for(unsigned y=0;y<C1_HEIGHT;y++)for(unsigned x=0;x<C1_WIDTH;x++)
            if((!region_only||(x>=4&&x<86&&y>=134&&y<150))&&ranks[y*C1_WIDTH+x]>stage)mask[(y/8)*C1_WIDTH+x]|=1u<<(7-y%8);
        uint64_t sm=b->time_us(b->ctx);mask_us+=sm-st;
        if(static_lut(b,phase_a[stage])||plane(b,0x26,mask))return -1;
        uint64_t sw=b->time_us(b->ctx);write_us+=sw-sm;
        if(update(b,0x0c,1,5000000))return -1;busy_us+=b->time_us(b->ctx)-sw;
    }
    printf("STATIC_PROFILE stages=%u mask_us=%llu write_us=%llu busy_us=%llu\n",last,(unsigned long long)mask_us,(unsigned long long)write_us,(unsigned long long)busy_us);
    if(plane(b,0x26,zero)||update(b,0x03,1,5000000))return -1;
    return 0;
}

static uint8_t display_frame[C1_PIXELS];
static int clear_video_white(const c1_bus*b,int region_only){
    uint8_t zero[BYTES]={0},white[BYTES]={0};
    if(region_only){for(unsigned y=134;y<150;y++)for(unsigned x=4;x<86;x++)white[(y/8)*C1_WIDTH+x]|=1u<<(7-y%8);}
    else memset(white,255,BYTES);
    if(lut(b,1)||plane(b,0x24,white)||plane(b,0x26,zero))return -1;
    for(unsigned k=0;k<12;k++){if(video_action()==2)return -1;if(update(b,0x0c,1,5000000))return -1;}
    if(plane(b,0x24,zero)||plane(b,0x26,zero)||update(b,0x0c,1,5000000))return -1;
    return 0;
}
static int show_static(const c1_bus*b,const uint8_t*raw,int region_only){
    memcpy(display_frame,raw,C1_PIXELS);media_overlay(display_frame);
    return static_from_white(b,display_frame,region_only);
}
static unsigned seek_target(unsigned current,int delta,unsigned count){int64_t n=(int64_t)current+delta;if(n<0)n=0;if(n>=count)n=count-1;return (unsigned)n;}
int c1_gray32_display(const c1_bus *b,const uint8_t *ranks){
    if(!b||!ranks||!b->write||!b->reset||!b->busy||!b->time_us||!b->sleep_us)return -1;
    int pr=setpriority(PRIO_PROCESS,0,-10);int l1=mlock(pixel_response,sizeof(pixel_response));
    printf("PACING priority=%d response_lock=%d\n",pr,l1);
    unsigned limit=0;
    char source[1024];const char*cache=getenv("C1_VIDEO_CACHE");snprintf(source,sizeof(source),"%s/osmo-device.c1v",cache?cache:"/storage/c1-video/device-cache");
    FILE *vf=fopen(source,"rb");
    uint32_t hdr[5];if(!vf)return -1;setvbuf(vf,0,_IOFBF,65536);
    if(fread(hdr,1,20,vf)!=20||memcmp(hdr,"C1V1",4)||hdr[1]!=296||hdr[2]!=152||hdr[3]!=30||!hdr[4]||hdr[4]>1000000){fclose(vf);return -1;}
    if(fseek(vf,0,SEEK_END)||ftell(vf)!=(long)(20u+hdr[4]*C1_PIXELS)||fseek(vf,20,SEEK_SET)){fclose(vf);puts("VIDEO_SIZE_ERROR");return -1;}
    printf("SOURCE video cache frames=%u size=296x152 fps=30 rotation=0 future=0 threshold=0\n",hdr[4]);
    if(fr_init(source,hdr[4]))return -1;
    uint8_t *video=malloc(C1_PIXELS);if(!video){fclose(vf);return -1;}
    valid_cache[0]=valid_cache[1]=0;
    unsigned last=0;for(unsigned i=0;i<C1_PIXELS;i++){
        if(ranks[i]>31)return -1;if(ranks[i]>last)last=ranks[i];
    }
    uint8_t zero[BYTES]={0},mask[BYTES];
    b->reset(b->ctx);if(b->write(b->ctx,0x12,0,0))return -1;
    b->sleep_us(b->ctx,10000);if(wait_idle(b,5000000,0))return -1;
    REG(0x01,0x27,1,0);REG(0x11,6);if(window(b))return -1;
    REG(0x3c,5);REG(0x21,0x88,0x80);REG(0x18,0x80);REG(0x2c,0);REG(0x3c,5);
    /* One factory white refresh per selected-video start. */
    if(plane(b,0x24,zero)||plane(b,0x26,zero)||update(b,0xf7,1,10000000))return -1;
    puts("PLAY_START_NATIVE_WHITE count=1");
    REG(0x21,0,0x80);REG(0x74,0x54);REG(0x7e,0x3b);REG(0x2c,0);
    REG(0x03,0x15);REG(0x04,0x41,0x41,0x32);REG(0x3a,0);REG(0x3b,4);
    REG(0x11,6);if(window(b))return -1;
    REG(0x37,0,0,0,0,0,0,0,0,0,0);REG(0x3c,0x80);REG(0x18,0x80);
    if(lut(b,1))return -1;REG(0x3f,2);
    if(plane(b,0x24,zero)||plane(b,0x26,zero)||update(b,0xc0,1,5000000))return -1;
    if(clear_video_white(b,0)){fr_close();fclose(vf);free(video);return -1;}
    typedef uint16_t Pixel;
    _Static_assert(sizeof(Pixel)==2,"packed pixel size");
    static Pixel buffers[2][C1_PIXELS];Pixel*current=buffers[0],*next_state=buffers[1];
    memset(buffers,0,sizeof(buffers));
    static uint16_t excitation[C1_PIXELS];memset(excitation,0,sizeof(excitation));
    uint8_t black[BYTES],white[BYTES];
    /* Playback begins after verified fast white completion. */
    unsigned source_index=0,have_frame=0,powered=1,audio_restart=1;
    unsigned frames=0,scans=0,late=0,missed_scan=0;uint64_t last_trigger=0,max_trigger_gap=0;uint64_t last_frame=0,max_gap=0;uint64_t start=b->time_us(b->ctx),next=start,report=start;
    uint64_t psum[4]={0},pmax[4]={0}; unsigned pn=0;
    while(video_action()!=2){
        if(video_action()!=1){int delta=video_take_seek();if(delta){
            unsigned old=have_frame?(source_index+hdr[4]-1)%hdr[4]:0;
            source_index=seek_target(old,delta,hdr[4]);
            if(fseek(vf,20+(long)source_index*C1_PIXELS,SEEK_SET))return -1;
            audio_restart=1;printf("SEEK from=%u to=%u delta=%d paused=0\n",old,source_index,delta);
        }}
        if(have_frame && video_action()==1){
            media_pause();uint64_t pause_begin=b->time_us(b->ctx);if(finish_pending(b))return -1;
            unsigned frozen=(source_index+hdr[4]-1)%hdr[4];
            printf("PAUSE_WHITE frame=%u pulses=12 native_clear=0\n",frozen);
            FILE*raw=fopen("/tmp/c1-video/paused.raw","wb");if(raw){fwrite(video,1,C1_PIXELS,raw);fclose(raw);}
            if(clear_video_white(b,0))return -1;
            uint64_t white_done=b->time_us(b->ctx);
            printf("PAUSE_STATIC frame=%u native_clear=0\n",frozen);
            unsigned rendered_hud=media_hud_token();int rc=show_static(b,video,0);if(rc){if(video_action()==2)goto finish;return rc;}
            printf("PAUSE_TIMING request_to_begin_us=%llu white_us=%llu static_us=%llu total_us=%llu\n",(unsigned long long)(pause_begin-video_request_us()),(unsigned long long)(white_done-pause_begin),(unsigned long long)(b->time_us(b->ctx)-white_done),(unsigned long long)(b->time_us(b->ctx)-video_request_us()));
            powered=0;printf("PAUSED frame=%u power=off\n",frozen);
            unsigned hud_seen=rendered_hud;
            while(video_action()==1){
                int delta=video_take_seek();unsigned token=media_hud_token();
                if(delta){
                    unsigned old=(source_index+hdr[4]-1)%hdr[4],target=seek_target(old,delta,hdr[4]);
                    if(fseek(vf,20+(long)target*C1_PIXELS,SEEK_SET)||fr_get(target,video))return -1;
                    source_index=target+1;printf("SEEK from=%u to=%u delta=%d paused=1\n",old,target,delta);
                    if(video_power(b)||clear_video_white(b,0)||show_static(b,video,0))return -1;
                    hud_seen=token;printf("PAUSED_SEEK frame=%u power=off\n",target);
                }else if(token!=hud_seen){
                    if(video_power(b)||clear_video_white(b,1)||show_static(b,video,1))return -1;
                    hud_seen=token;printf("VOLUME_HUD value=%d active=%u paused=1\n",media_volume(),token&1);
                }
                b->sleep_us(b->ctx,20000);
            }
            if(video_action()==2)goto finish;
            uint64_t resume_begin=b->time_us(b->ctx);
            if(video_power(b))return -1;powered=1;
            printf("RESUME_WHITE pulses=12 native_clear=0\n");
            if(clear_video_white(b,0))return -1;
            memset(current,0,sizeof(buffers[0]));audio_restart=1;
            memset(next_state,0,sizeof(buffers[0]));memset(excitation,0,sizeof(excitation));
            printf("RESUME_TIMING request_to_ready_us=%llu power_us=%llu\n",(unsigned long long)(b->time_us(b->ctx)-video_request_us()),(unsigned long long)(b->time_us(b->ctx)-resume_begin));
            printf("RESUME next_frame=%u native_clear=0\n",source_index%hdr[4]);
            frames=scans=late=missed_scan=0;last_trigger=last_frame=max_trigger_gap=max_gap=0;
            memset(psum,0,sizeof(psum));memset(pmax,0,sizeof(pmax));pn=0;
            start=report=b->time_us(b->ctx);
        }
        uint64_t now=b->time_us(b->ctx);
        uint64_t p0=b->time_us(b->ctx),p1,p2,p3,p4;
        frames++;
        if(frames==1||frames==151||frames==601){struct timeval tv;gettimeofday(&tv,0);printf("VIDEO_PHASE frame=%u wall_us=%llu\n",frames,(unsigned long long)tv.tv_sec*1000000+tv.tv_usec);}
        if(source_index==hdr[4]){source_index=0;audio_restart=1;if(fseek(vf,20,SEEK_SET))return -1;}
        if(fr_get(source_index,video)){puts("VIDEO_READ_ERROR");return -1;}
        source_index++;
        p1=b->time_us(b->ctx);
        const uint8_t *input=video;
        if(media_hud_token()&1){memcpy(display_frame,video,C1_PIXELS);media_overlay(display_frame);input=display_frame;}
        const unsigned history_word=0x64;
        memset(black,0,BYTES);memset(white,0,BYTES);
        for(unsigned y=0;y<C1_HEIGHT;y++)for(unsigned x=0;x<C1_WIDTH;x++){
            unsigned i=y*C1_WIDTH+x;unsigned src=i;
            if(input[src]>31)return -1;int target=rankq[input[src]];
            unsigned packed=current[i],q=packed&8191,reserve=packed>>13;
            unsigned ex=excitation[i]*219u>>8,eh=ex>>6;if(eh>31)eh=31;
            unsigned entry=q&~3u;
            if(target!=(int)q)entry=pixel_response[eh][(q>>2)*32+input[src]];
            unsigned code=(entry>>13)&3,pred=(entry&8191)+(q&3);if(pred>4096)pred=4096;
            if(input[src]==31 && code==2 && pred>=4010)pred=4096;
            excitation[i]=code==2?(ex+256>2047?2047:ex+256):(code?0:ex);
            unsigned history_code=(history_word>>(code*2))&3;
            if(history_code)reserve=history_code==1?2:1;
            next_state[i]=(Pixel)(pred|(reserve<<13));
            if(code){unsigned j=(y/8)*C1_WIDTH+x,bit=1u<<(7-y%8);
                if(code&2)black[j]|=bit;
                if(code&1)white[j]|=bit;
            }


        }
        p2=b->time_us(b->ctx);
        if(finish_pending(b))return -1; /* LUT set on entry and restored by video_power on resume. */
        if(plane(b,0x24,white)||plane(b,0x26,black))return -1;
        p3=b->time_us(b->ctx);
        uint64_t due=start+((uint64_t)scans*1000000)/30+40000;
        now=b->time_us(b->ctx);if(now>due+1000)missed_scan++;
        while((now=b->time_us(b->ctx))<due){if(due-now>200)b->sleep_us(b->ctx,(unsigned)(due-now-100));}
        if(last_trigger&&now-last_trigger>max_trigger_gap)max_trigger_gap=now-last_trigger;last_trigger=now;
        if(1){if(last_frame){uint64_t gap=now-last_frame;if(gap>max_gap)max_gap=gap;if(gap>40000)late++;}last_frame=now;}
        uint64_t pb=b->time_us(b->ctx);
        if(audio_restart){media_play(source_index-1);audio_restart=0;}
        if(start_pending(b))return -1;
        p4=b->time_us(b->ctx);
        uint64_t dt[4]={p1-p0,p2-p1,p3-p2,p4-pb};pn++;for(unsigned z=0;z<4;z++){psum[z]+=dt[z];if(dt[z]>pmax[z])pmax[z]=dt[z];}
        Pixel*swap=current;current=next_state;next_state=swap;
        have_frame=1;scans++;now=b->time_us(b->ctx);
        if(now-report>2000000){printf("PROFILE n=%u read=%llu/%llu model=%llu/%llu spi=%llu/%llu busy=%llu/%llu us_mean/max\n",pn,(unsigned long long)(psum[0]/pn),(unsigned long long)pmax[0],(unsigned long long)(psum[1]/pn),(unsigned long long)pmax[1],(unsigned long long)(psum[2]/pn),(unsigned long long)pmax[2],(unsigned long long)(psum[3]/pn),(unsigned long long)pmax[3]);memset(psum,0,sizeof(psum));memset(pmax,0,sizeof(pmax));pn=0;printf("DYNAMIC fps=%.2f scans=%.2f frames=%u\n",frames*1000000.0/(now-start),scans*1000000.0/(now-start),frames);printf("TIMING late40ms=%u max_gap_us=%llu phase=%s\n",late,(unsigned long long)max_gap,now-start<5000000?"reference":now-start<25000000?"moving":"settling");printf("SCAN missed_deadline=%u max_trigger_gap_us=%llu\n",missed_scan,(unsigned long long)max_trigger_gap);printf("FRAME_BUFFER waits=%u seeks=%u max_get_us=%llu\n",fr.waits,fr.seeks,(unsigned long long)fr.max_wait);report=now;}
        if(limit && scans>=limit)break;
        /* Only current frame read; no refresh inside playback. */
    }
finish:
    media_pause();
    if(finish_pending(b))return -1;
    puts("VIDEO_EXIT");
    fr_close();fclose(vf);free(video);if(plane(b,0x26,zero))return -1;REG(0x3c,0x80);
    if(powered && update(b,0x03,1,5000000))return -1;REG(0x10,1);return 0;
}






static uint8_t ui_last[BYTES];static int ui_valid;
void c1_video_ui_invalidate(void){ui_valid=0;}
void c1_video_reader_cleanup(void){fr_close();munlock(pixel_response,sizeof(pixel_response));}
int c1_video_ui(const c1_bus*b,const uint8_t*frame){
    if(ui_valid&&!memcmp(ui_last,frame,BYTES)){puts("UI_FAST unchanged=1 native_clear=0");return 0;}
    uint8_t zero[BYTES]={0},black[BYTES],white[BYTES];uint64_t began=b->time_us(b->ctx);
    if(finish_pending(b))return -1;b->reset(b->ctx);if(b->write(b->ctx,0x12,0,0))return -1;
    b->sleep_us(b->ctx,10000);if(wait_idle(b,5000000,0))return -1;
    REG(0x01,0x27,1,0);REG(0x11,6);REG(0x3c,0x80);REG(0x18,0x80);
    if(video_power(b))return -1;
    int initial=!ui_valid;
    if(initial){
        memset(white,255,BYTES);if(plane(b,0x24,white)||plane(b,0x26,zero))return -1;
        for(int k=0;k<12;k++)if(update(b,0x0c,1,5000000))return -1;
        memset(ui_last,0,sizeof(ui_last));
    }
    for(unsigned i=0;i<BYTES;i++){black[i]=frame[i]&~ui_last[i];white[i]=ui_last[i]&~frame[i];}
    if(plane(b,0x24,white)||plane(b,0x26,black))return -1;
    for(int k=0;k<20;k++){if(k==12&&plane(b,0x24,zero))return -1;if(update(b,0x0c,1,5000000))return -1;}
    if(plane(b,0x24,zero)||plane(b,0x26,zero)||update(b,0x0c,1,5000000)||update(b,0x03,1,5000000))return -1;
    REG(0x10,1);memcpy(ui_last,frame,BYTES);ui_valid=1;
    printf("UI_FAST initial_white=%d native_clear=0 elapsed_us=%llu\n",initial,(unsigned long long)(b->time_us(b->ctx)-began));return 0;
}

