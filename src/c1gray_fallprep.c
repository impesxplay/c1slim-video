#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/input.h>
#include "font8x8_basic.h"
#include "gray_config.h"

/* Only for the verified C1 Slim e0266a128 / X1600 firmware. No kernel writes.
 * GPIO direction/mux is left intact. Only the five display outputs are written.
 * 144-panel waveform: Desktop/PES/墨水屏校色/display_gray16_144.py.
 * Native BW reset, addressing, polarity, refresh and sleep are recovered from
 * the running e0266a128 kernel driver. See README.md for evidence and limits. */
#define W 296
#define H 152
#define N (W*H/8)
#define CS (1u<<28)
#define SCK (1u<<31)
#define MOSI (1u<<30)
#define DC (1u<<25)
#define RST (1u<<17)
#define BUSY (1u<<26)
#define OUTPUTS (CS|SCK|MOSI|DC|RST)
#ifndef C1GRAY_RUN
#define C1GRAY_RUN "/tmp/c1gray"
#endif
#define RUN C1GRAY_RUN
typedef struct { volatile int ui_paused, hotkey_paused, valid, touched, asleep; unsigned char frame[N]; } Backup;
static volatile uint32_t *gpio;
static int memfd=-1, heartbeat=-1, restoring=0;
static volatile sig_atomic_t quit=0;
static unsigned char levels[W*H];
static int inputfd[2]={-1,-1};
static long long mono_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000000000+t.tv_nsec;}
static long long ms(void){return mono_ns()/1000000;}
static void on_signal(int s){(void)s;quit=1;}
static void beat(void){if(heartbeat>=0){char c=0;(void)write(heartbeat,&c,1);}}
static int write_all(int fd,const void *v,size_t n){const char *p=v;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return -1;p+=k;n-=k;}return 0;}
static void save(const char *name,const void *p,size_t n){char path[160];snprintf(path,sizeof(path),RUN "/%s",name);int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY,0600);if(fd>=0){write_all(fd,p,n);close(fd);}}
static void marker(const char *s){save("state",s,strlen(s));printf("%s",s);fflush(stdout);}
static int process_is(pid_t pid,const char *name){char path[64],buf[64]={0};snprintf(path,sizeof(path),"/proc/%ld/comm",(long)pid);int f=open(path,O_RDONLY);if(f<0)return 0;read(f,buf,63);close(f);buf[strcspn(buf,"\r\n")]=0;return !strcmp(buf,name);}
#ifndef C1GRAY_CUSTOM_KEYS
static void poll_keys(void){for(int i=0;i<2;i++)if(inputfd[i]>=0){struct input_event e;while(read(inputfd[i],&e,sizeof(e))==sizeof(e))if(e.type==EV_KEY&&e.value==1&&(e.code==KEY_BACK||e.code==KEY_ESC))quit=1;}}
#else
static void poll_keys(void);
#endif
static void idle_ms(int n){long long end=ms()+n;while(ms()<end){beat();if(!restoring)poll_keys();usleep(10000);}}
static inline void pin(uint32_t mask,int high){gpio[(high?0x44:0x48)/4]=mask;__sync_synchronize();}
static void spi_delay(void){
#ifdef C1GRAY_SPI_DELAY_NS
    unsigned ns=restoring?1000:C1GRAY_SPI_DELAY_NS;
    if(!ns){
#ifdef C1GRAY_SPI_NOP_COUNT
        unsigned count=C1GRAY_SPI_NOP_COUNT;
#else
        unsigned count=128;
#endif
        for(unsigned i=0;i<count;i++)__asm__ volatile("nop");return;
    }
#else
    unsigned ns=1000;
#endif
    long long end=mono_ns()+ns;while(mono_ns()<end){}
}
static void byte_out(unsigned char v){
 static int last_mosi=-1;
 if(restoring){for(unsigned i=0;i<8;i++){pin(MOSI,!!(v&0x80));spi_delay();pin(SCK,1);spi_delay();pin(SCK,0);v<<=1;}last_mosi=-1;return;}
 int first=!!(v&128);if(first!=last_mosi){pin(MOSI,first);last_mosi=first;}
 #define CLOCK_PREP(mask) do {pin(SCK,1);if(!(v&(mask))){pin(SCK|MOSI,0);last_mosi=0;}else{pin(SCK,0);if(!last_mosi){pin(MOSI,1);last_mosi=1;}}}while(0)
 CLOCK_PREP(64);CLOCK_PREP(32);CLOCK_PREP(16);CLOCK_PREP(8);
 CLOCK_PREP(4);CLOCK_PREP(2);CLOCK_PREP(1);pin(SCK,1);pin(SCK,0);
 #undef CLOCK_PREP
}

static void command(unsigned char c){pin(CS,1);pin(SCK,0);pin(DC,0);pin(CS,0);spi_delay();byte_out(c);pin(CS,1);}
static void data(const unsigned char *p,size_t n){pin(DC,1);pin(CS,0);for(size_t i=0;i<n;i++){byte_out(p[i]);if((i&1023)==0)beat();}pin(CS,1);}
static void reg(unsigned char c,const unsigned char *p,size_t n){command(c);data(p,n);}
#define REG(c,...) do{const unsigned char b[]={__VA_ARGS__};reg(c,b,sizeof(b));}while(0)
static int busy_wait(const char *label,int timeout){long long start=ms();int seen=0;idle_ms(2);while(gpio[0]&BUSY){seen=1;if(ms()-start>timeout){printf("BUSY_TIMEOUT %s\n",label);return -1;}beat();usleep(2000);}printf("BUSY_OK %s elapsed_ms=%lld asserted=%d\n",label,ms()-start,seen);return 0;}
static int update(unsigned char mode,const char *label,int timeout){REG(0x22,mode);command(0x20);return busy_wait(label,timeout);}
static void reset(void){pin(CS,1);pin(SCK,0);pin(RST,0);idle_ms(2);pin(RST,1);idle_ms(2);}
static void window(void){REG(0x44,0x12,0);REG(0x45,0,0,0x27,1);REG(0x4e,0x12);REG(0x4f,0,0);}
static void plane(unsigned char c,const unsigned char *b){REG(0x4e,0x12);REG(0x4f,0,0);reg(c,b,N);}
static int native_init(void){reset();command(0x12);idle_ms(10);if(busy_wait("native_reset",5000))return -1;REG(0x01,0x27,1,0);REG(0x11,6);window();REG(0x3c,5);REG(0x21,0x88,0x80);REG(0x18,0x80);return 0;}
static int native_frame(const unsigned char *b){if(native_init())return -1;plane(0x24,b);plane(0x26,b);return update(0xf7,"native_full",10000);}
static void deep_sleep(void){REG(0x10,1);pin(CS,1);}
static int kernel_match(void){unsigned char code[6144];if(pread(memfd,code,sizeof(code),0x703000)!=sizeof(code))return 0;uint32_t hash=2166136261u;for(unsigned i=0;i<sizeof(code);i++)hash=(hash^code[i])*16777619u;return hash==0x74e18da6u;}
static int kernel_quiet(void){long long start=ms(),quiet=ms();while(ms()-start<12000){uint32_t active=1;if(pread(memfd,&active,4,0x954fa4)!=4)return -1;if(active)quiet=ms();if(ms()-quiet>500)return 0;beat();usleep(20000);}return -1;}
static pid_t guard_start(pid_t ui,pid_t hotkey,Backup *b,int lockfd){int p[2];if(pipe2(p,O_CLOEXEC|O_NONBLOCK))return -1;pid_t parent=getpid(),pid=fork();if(pid<0)return -1;if(!pid){close(p[1]);heartbeat=-1;restoring=1;signal(SIGTERM,SIG_IGN);signal(SIGINT,SIG_IGN);signal(SIGHUP,SIG_IGN);struct pollfd f={p[0],POLLIN|POLLHUP,0};char buf[2048];for(;;){int r=poll(&f,1,6000);if(r<0&&errno==EINTR)continue;if(r<=0){kill(parent,SIGKILL);puts("WATCHDOG_KILLED_PARENT");break;}ssize_t n=read(p[0],buf,sizeof(buf));if(n==0)break;if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;}
        usleep(200000);int ok=1;
#ifdef C1GRAY_GUARD_CLEANUP
        C1GRAY_GUARD_CLEANUP();
#endif
#ifdef C1GRAY_RETAIN_ON_SHUTDOWN
        /* Only a committed gallery shutdown bypasses normal screen/UI restore. */
        if(C1GRAY_RETAIN_ON_SHUTDOWN){marker("SHUTDOWN_SCREEN_RETAINED\n");close(p[0]);close(lockfd);close(memfd);_exit(0);}
#endif
        if(b->touched&&b->valid){marker("RESTORING\n");if(!b->asleep)busy_wait("restore_wait",10000);ok=native_frame(b->frame)==0;if(!ok)ok=native_frame(b->frame)==0;deep_sleep();}
        if(b->ui_paused)kill(ui,SIGCONT);if(b->hotkey_paused&&hotkey>1)kill(hotkey,SIGCONT);
        marker(ok?"RESTORED_UI_RESUMED\n":"RESTORE_FAILED_UI_RESUMED\n");close(p[0]);close(lockfd);close(memfd);_exit(ok?0:1);
    }close(p[0]);heartbeat=p[1];return pid;}
static int capture(pid_t ui,Backup *b){char path[64];snprintf(path,sizeof(path),"/proc/%ld/mem",(long)ui);int f=open(path,O_RDONLY);if(f<0)return -1;ssize_t n=pread(f,b->frame,N,0x82e7b0);close(f);if(n!=N)return -1;b->valid=1;save("original.bin",b->frame,N);return 0;}
static void pixel(int x,int y,int level){if(x>=0&&x<W&&y>=0&&y<H)levels[y*W+x]=level;}
static void text(int x,int y,const char *s){for(;*s;s++,x+=8){unsigned char c=(unsigned char)*s;if(c>127)c='?';for(int j=0;j<8;j++)for(int i=0;i<8;i++)pixel(x+i,y+j,(font8x8_basic[c][j]>>i)&1?0:15);}}
static void pattern(int bw){memset(levels,15,sizeof(levels));text(8,4,bw?"C1 DIRECT SPI / B&W":"C1 GRAY / DARK TUNE 2");for(int y=22;y<123;y++)for(int x=4;x<292;x++){int level=(x-4)*16/288;pixel(x,y,bw?(level&1?15:0):level);}for(int i=0;i<16;i++){char c[2]={"0123456789ABCDEF"[i],0};text(8+i*18,126,c);}text(8,142,"BACK:EXIT / AUTO RESTORE");}
static int load_pgm(const char *path){FILE *f=fopen(path,"rb");if(!f)return -1;char line[128];int w=0,h=0,m=0;if(!fgets(line,sizeof(line),f)||strcmp(line,"P5\n")){fclose(f);return -1;}do{if(!fgets(line,sizeof(line),f)){fclose(f);return -1;}}while(line[0]=='#');if(sscanf(line,"%d %d",&w,&h)!=2||!fgets(line,sizeof(line),f)||sscanf(line,"%d",&m)!=1||w!=W||h!=H||m!=255){fclose(f);return -1;}size_t n=fread(levels,1,sizeof(levels),f);int extra=fgetc(f);fclose(f);if(n!=sizeof(levels)||extra!=EOF)return -1;for(size_t i=0;i<sizeof(levels);i++)levels[i]=(levels[i]+8)/17;return 0;}
static void mask(unsigned char *b,unsigned bits){memset(b,255,N);for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(bits&(1u<<levels[y*W+x]))b[(y/8)*W+x]&=~(1u<<(7-y%8));}
static int gray_frame(void){unsigned char b[N]={0};
    /* Two white clears using this panel's factory waveform, followed by the
       reference's selected-transition pulses. Retain native source offset and
       landscape address mode; masks use 0=driven black, 1=white. */
    for(int i=0;i<2;i++){if(quit)return -1;if(native_frame(b))return -1;}
    REG(0x21,0,0x80);REG(0x74,0x54);REG(0x7e,0x3b);REG(0x3c,5);REG(0x2c,0x70);REG(0x03,0x15);REG(0x04,0x41,0xa8,0x32);REG(0x3a,0x11);REG(0x3b,8);REG(0x11,6);window();REG(0x37,0,0,0,0,0,0x40,0,0,0,0);REG(0x3c,5);REG(0x18,0x80);if(update(0xc0,"gray_power",5000))return -1;
    /* Dark tune 2: shortening pulses alone left levels 0-4 indistinguishable.
       Reduce both duration and VSH for 1-4 substantially; blend 5-6 into the
       existing lighter levels. Level 0 remains the original black reference.
       These are optical-test candidates, not a calibrated luminance mapping.
       Tune 1 and the original waveform are archived in profiles/. */
    puts("PROFILE_RUNTIME");gray_dump();
    unsigned done=0;
    for(int level=14;level>=0;level--){GrayStep *s=&gray_steps[level];poll_keys();if(quit)return -1;mask(b,done);plane(0x26,b);done|=1u<<level;mask(b,done);plane(0x24,b);REG(0x3a,s->gate);REG(0x3b,s->source);REG(0x3f,7);REG(0x03,0x15);REG(0x04,s->vsh,0xa8,0x32);REG(0x2c,0x20);
        unsigned char lut[153]={0};for(unsigned g=0;g<s->groups;g++){lut[24+g]=0x40;lut[60+g*7]=s->duration;}lut[144]=0x20;reg(0x32,lut,sizeof(lut));char label[48];snprintf(label,sizeof(label),"gray_level_%02d",level);if(update(4,label,10000))return -1;
    }return update(3,"gray_poweroff",5000);
}
#ifdef C1GRAY_WEB_PANEL
#include "../gui/web_profile.h"
#endif
int main(int argc,char **argv){setvbuf(stdout,0,_IONBF,0);
    if(argc==3&&!strcmp(argv[1],"--check-config")){if(gray_config(argv[2]))return 2;gray_dump();return 0;}
    if(argc<5||argc>6){fprintf(stderr,"c1gray UI_PID HOTKEY_PID bw|gray|IMAGE.pgm HOLD_SECONDS [CONFIG]\n");return 2;}
    if(argc==6&&gray_config(argv[5]))return 2;
    pid_t ui=atoi(argv[1]),hotkey=atoi(argv[2]);int hold=atoi(argv[4]),bw=!strcmp(argv[3],"bw");if(!process_is(ui,"mpenMain")||hold<5||hold>600||(hotkey>1&&!process_is(hotkey,"c1hotkey")))return 2;
    if(bw||!strcmp(argv[3],"gray"))pattern(bw);else if(load_pgm(argv[3])){fprintf(stderr,"Expected P5 PGM, 296x152, maxval255\n");return 2;}
    int lockfd=open("/tmp/c1term.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);if(lockfd<0||flock(lockfd,LOCK_EX|LOCK_NB)){fprintf(stderr,"Display app already active\n");return 2;}
    memfd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(memfd<0)return 3;if(!kernel_match()){fprintf(stderr,"Kernel display code differs; no writes performed\n");return 3;}gpio=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,memfd,0x10010000);if(gpio==MAP_FAILED)return 3;
    uint32_t all=OUTPUTS|BUSY;if((gpio[0x10/4]&all)||(gpio[0x20/4]&all)!=all||(gpio[0x30/4]&all)!=BUSY){fprintf(stderr,"Unexpected GPIO mux/direction; no writes performed\n");return 3;}
    mkdir(RUN,0700);save("levels.raw",levels,sizeof(levels));Backup *b=mmap(0,sizeof(*b),PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_SHARED,-1,0);if(b==MAP_FAILED)return 3;pid_t guard=guard_start(ui,hotkey,b,lockfd);if(guard<0)return 3;
    signal(SIGTERM,on_signal);signal(SIGINT,on_signal);signal(SIGHUP,on_signal);signal(SIGPIPE,SIG_IGN);char pidstr[32];snprintf(pidstr,sizeof(pidstr),"%ld\n",(long)getpid());save("pid",pidstr,strlen(pidstr));int result=0;
    b->ui_paused=1;if(kill(ui,SIGSTOP)){result=3;goto done;}if(hotkey>1){b->hotkey_paused=1;if(kill(hotkey,SIGSTOP)){result=3;goto done;}}idle_ms(150);if(capture(ui,b)||kernel_quiet()){puts("Cannot back up/quiesce native display");result=3;goto done;}
    for(int i=0;i<2;i++){char path[80];snprintf(path,sizeof(path),"/dev/input/event%d",i);inputfd[i]=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(inputfd[i]<0||ioctl(inputfd[i],EVIOCGRAB,1)){perror("grab keyboard");result=3;goto done;}}
    marker("DRAWING\n");b->touched=1;
    if(bw){unsigned char frame[N];mask(frame,1);for(int i=0;i<N;i++)frame[i]=~frame[i];result=native_frame(frame);}else result=gray_frame();
    if(!result&&!quit){marker(bw?"BW_READY\n":"GRAY_READY\n");long long end=ms()+hold*1000LL;while(!quit&&ms()<end)idle_ms(20);}
done:
    for(int i=0;i<2;i++)if(inputfd[i]>=0){ioctl(inputfd[i],EVIOCGRAB,0);close(inputfd[i]);inputfd[i]=-1;}
    close(heartbeat);heartbeat=-1;int status=0;while(waitpid(guard,&status,0)<0&&errno==EINTR){}close(lockfd);munmap((void*)gpio,4096);close(memfd);return result||!WIFEXITED(status)||WEXITSTATUS(status)?1:0;
}
