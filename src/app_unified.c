/* Shared, firmware-checked GPIO ownership and recovery from the existing C1 apps. */
#define C1GRAY_RUN "/tmp/c1-video"
#define C1GRAY_CUSTOM_KEYS
/* Same SPI timing as the installed gallery (timing.txt: 0 0). */
#define C1GRAY_SPI_DELAY_NS 0
#define C1GRAY_SPI_NOP_COUNT 0
#define main legacy_unused_main
#include "c1gray_fallprep.c"
#undef main
#undef REG
#include "c1_gray32.h"
#include "media.h"
extern int c1_video_ui(const c1_bus*,const uint8_t*);
extern void c1_video_ui_invalidate(void);
extern void c1_video_reader_cleanup(void);
static const c1_bus *active_bus;
static int ui_mode,back_request;
#include "video_list.h"
static int paused_request;
static int pending_seek;
static uint64_t request_us;
uint64_t video_request_us(void){return request_us;}
static void poll_keys(void){
    for(int i=0;i<2;i++)if(inputfd[i]>=0){struct input_event e;
        while(read(inputfd[i],&e,sizeof(e))==sizeof(e))if(e.type==EV_KEY&&(e.value==1||e.value==2)){
            if(ui_mode==0){
                if(e.value!=1)continue;
                if(e.code==KEY_BACK||e.code==KEY_ESC){quit=1;continue;}
                if(!count)continue;
                if(e.code==KEY_ENTER||e.code==KEY_SPACE){action=selected;continue;}
                if(e.code==KEY_UP||e.code==KEY_VOLUMEUP)selected=(selected+count-1)%count;
                else if(e.code==KEY_DOWN||e.code==KEY_VOLUMEDOWN)selected=(selected+1)%count;
                else if(e.code==KEY_LEFT)selected=selected>=3?selected-3:0;
                else if(e.code==KEY_RIGHT){selected+=3;if(selected>=(int)count)selected=count-1;}
                else continue;dirty=1;continue;
            }
            if(e.code==KEY_BACK||e.code==KEY_ESC){if(e.value==1){back_request=1;media_pause();}continue;}
            if(ui_mode==1)continue;
            if(e.code==KEY_VOLUMEUP||e.code==KEY_VOLUMEDOWN)media_volume_step(e.code==KEY_VOLUMEUP?5:-5);
            else if(e.value==1){
                if(e.code==KEY_BACK||e.code==KEY_ESC){quit=1;media_pause();}
                else if(e.code==KEY_SPACE){paused_request=!paused_request;request_us=mono_ns()/1000;if(paused_request)media_pause();printf("SPACE requested=%s\n",paused_request?"pause":"play");}
                else if(e.code==KEY_LEFT||e.code==KEY_RIGHT){pending_seek+=e.code==KEY_LEFT?-150:150;if(pending_seek>1800)pending_seek=1800;if(pending_seek< -1800)pending_seek=-1800;media_pause();}
            }
        }
    }
}
int video_take_seek(void){poll_keys();int n=pending_seek;pending_seek=0;return n;}
int video_action(void){poll_keys();return (quit||back_request)?2:paused_request?1:0;}
static long long transfer_ns;
static int bus_write(void *ctx,uint8_t c,const uint8_t *p,size_t n){(void)ctx;long long t=mono_ns();if(c==0x22&&n&&p[0]==0xf7)puts("NATIVE_CLEAR initial_entry");command(c);if(n)data(p,n);transfer_ns+=mono_ns()-t;return 0;}
static void bus_reset(void *ctx){(void)ctx;reset();}
static int bus_busy(void *ctx){(void)ctx;return !!(gpio[0]&BUSY);}
static uint64_t bus_time(void *ctx){(void)ctx;return mono_ns()/1000;}
static void bus_sleep(void *ctx,unsigned us){(void)ctx;beat();poll_keys();usleep(us);}
#include "cache_prepare_unified.h"
int main(int argc,char **argv){
    setvbuf(stdout,0,_IONBF,0);
    if(argc!=3)return 2;
    pid_t ui=atoi(argv[1]),hotkey=atoi(argv[2]);
    if(!process_is(ui,"mpenMain")||(hotkey>1&&!process_is(hotkey,"c1hotkey")))return 2;
    int lockfd=open("/tmp/c1term.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lockfd<0||flock(lockfd,LOCK_EX|LOCK_NB)){puts("Display app already active");return 2;}
    memfd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(memfd<0||!kernel_match())return 3;
    gpio=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,memfd,0x10010000);if(gpio==MAP_FAILED)return 3;
    uint32_t all=OUTPUTS|BUSY;if((gpio[0x10/4]&all)||(gpio[0x20/4]&all)!=all||(gpio[0x30/4]&all)!=BUSY)return 3;
    mkdir(RUN,0700);
    Backup *b=mmap(0,sizeof(*b),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);if(b==MAP_FAILED)return 3;
    pid_t guard=guard_start(ui,hotkey,b,lockfd);if(guard<0)return 3;
    signal(SIGTERM,on_signal);signal(SIGINT,on_signal);signal(SIGHUP,on_signal);signal(SIGPIPE,SIG_IGN);
    int result=0;char id[32];snprintf(id,sizeof(id),"%ld\n",(long)getpid());save("pid",id,strlen(id));
    c1_bus bus={0,bus_write,bus_reset,bus_busy,bus_time,bus_sleep};
    b->ui_paused=1;if(kill(ui,SIGSTOP)){result=3;goto done;}
    if(hotkey>1){b->hotkey_paused=1;if(kill(hotkey,SIGSTOP)){result=3;goto done;}}
    idle_ms(150);if(capture(ui,b)||kernel_quiet()){result=3;goto done;}
    for(int i=0;i<2;i++){char path[64];snprintf(path,sizeof(path),"/dev/input/event%d",i);inputfd[i]=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(inputfd[i]<0||ioctl(inputfd[i],EVIOCGRAB,1)){result=3;goto done;}}
    active_bus=&bus;scan_videos();load_font();restore_selection();
    b->touched=1;b->asleep=0;
    while(!quit){
        ui_mode=0;poll_keys();if(quit)break;
        if(dirty){dirty=0;draw_list();if(c1_video_ui(&bus,screen)){result=1;break;}b->asleep=1;save("list-screen.bin",screen,N);marker("LIST_READY\n");}
        if(action>=0){
            action=-1;if(save_selection()){result=1;break;}
            char source[1024],cache[1024];FILE*f=fopen(BASE "/selected-video.txt","r");
            int ok=f&&fgets(source,sizeof(source),f)&&fgets(cache,sizeof(cache),f);if(f)fclose(f);if(!ok){result=1;break;}
            source[strcspn(source,"\r\n")]=0;cache[strcspn(cache,"\r\n")]=0;
            setenv("C1_VIDEO_SOURCE",source,1);setenv("C1_VIDEO_CACHE",cache,1);
            mkdir(BASE "/device-cache/items",0700);mkdir(cache,0700);
            ui_mode=1;back_request=0;paused_request=pending_seek=0;b->asleep=0;
            int prepared=ensure_device_cache();
            if(!prepared&&!quit&&!back_request){
                ui_mode=2;memset(levels,0,sizeof(levels));c1_video_ui_invalidate();
                if(media_init()){result=3;break;}
                int played=c1_gray32_display(&bus,levels);c1_video_reader_cleanup();media_shutdown(beat);
                if(played&&!back_request&&!quit)printf("PLAYBACK_ERROR result=%d\n",played);
                c1_video_ui_invalidate();
            }
            back_request=0;paused_request=pending_seek=0;dirty=1;ui_mode=0;
            puts("RETURN_TO_LIST restore_native=0");
        }
        beat();usleep(20000);
    }

done:
    media_shutdown(beat);
    for(int i=0;i<2;i++)if(inputfd[i]>=0){ioctl(inputfd[i],EVIOCGRAB,0);close(inputfd[i]);inputfd[i]=-1;}
    close(heartbeat);heartbeat=-1;int status=0;while(waitpid(guard,&status,0)<0&&errno==EINTR){}
    close(lockfd);munmap((void*)gpio,4096);close(memfd);
    return result||!WIFEXITED(status)||WEXITSTATUS(status)?1:0;
}
