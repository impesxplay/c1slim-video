#define _GNU_SOURCE
#include "media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include "font5x7.h"
#define BASE "/storage/c1-video"
static const char*pcm_path(void){static char path[1024];const char*cache=getenv("C1_VIDEO_CACHE");snprintf(path,sizeof(path),"%s/osmo-device.s16le",cache?cache:BASE "/device-cache");return path;}
#define PCM pcm_path()
typedef struct {volatile unsigned seq,play,frame,volume,volseq,quit,ready; volatile uint64_t command_us; volatile pid_t player;} Shared;
static Shared *sh;
static pid_t worker;
static int vol=40;
static unsigned hud_revision;
static uint64_t hud_until;
static volatile sig_atomic_t stopping;
static uint64_t now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void stop_signal(int n){(void)n;stopping=1;}
static void child_defaults(void){signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);signal(SIGPIPE,SIG_DFL);}
static void close_extra(void){for(int f=3;f<256;f++)close(f);}
static int mix(const char *op,const char *control,const char *value,int mapped,char *out,size_t cap){
 int fds[2];if(pipe2(fds,O_CLOEXEC))return -1;pid_t parent=getpid(),pid=fork();
 if(pid<0){close(fds[0]);close(fds[1]);return -1;}
 if(!pid){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(126);child_defaults();int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(fds[1],1);dup2(n,2);close_extra();
  if(mapped)execl("/usr/bin/amixer","amixer","-q","-c","0","-M",op,control,value,(char*)0);
  else if(value)execl("/usr/bin/amixer","amixer","-c","0",op,control,value,(char*)0);
  else execl("/usr/bin/amixer","amixer","-c","0",op,control,(char*)0);_exit(127);}
 close(fds[1]);fcntl(fds[0],F_SETFL,O_NONBLOCK);int status=0,done=0;size_t used=0;uint64_t end=now_us()+1800000;
 while(now_us()<end){char b[2048];ssize_t n;while((n=read(fds[0],b,sizeof(b)))>0)if(out&&used+1<cap){size_t z=n;if(z>cap-used-1)z=cap-used-1;memcpy(out+used,b,z);used+=z;}if(waitpid(pid,&status,WNOHANG)==pid){done=1;break;}usleep(5000);}
 if(!done){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}}
 if(out&&cap){ssize_t n;while(used+1<cap&&(n=read(fds[0],out+used,cap-used-1))>0)used+=n;out[used]=0;}close(fds[0]);return done&&WIFEXITED(status)&&!WEXITSTATUS(status)?0:-1;
}
static int mixer_get(int id){char key[32],buf[2048];snprintf(key,sizeof(key),"numid=%d",id);if(mix("cget",key,0,0,buf,sizeof(buf)))return -1;char *s=strstr(buf,": values=");if(!s)return -1;s+=9;return !strncmp(s,"on",2)?1:!strncmp(s,"off",3)?0:atoi(s);}
static int mixer_set(int id,int v){char key[32],value[32];snprintf(key,sizeof(key),"numid=%d",id);snprintf(value,sizeof(value),"%d",v);return mix("cset",key,value,0,0,0);}
static int persist(unsigned v){char text[32];int n=snprintf(text,sizeof(text),"%u\n",v);int f=open(BASE "/volume.tmp",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(f<0)return -1;int ok=write(f,text,n)==n&&fsync(f)==0;close(f);if(!ok||rename(BASE "/volume.tmp",BASE "/volume"))return -1;f=open(BASE,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(f<0)return -1;ok=fsync(f)==0;close(f);return ok?0:-1;}
static void stop_player(void){pid_t pid=sh->player;if(pid<2)return;kill(pid,SIGTERM);uint64_t end=now_us()+120000;int status;while(now_us()<end){pid_t r=waitpid(pid,&status,WNOHANG);if(r==pid||(r<0&&errno==ECHILD)){sh->player=0;return;}usleep(2000);}kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}sh->player=0;}
static int start_player(unsigned frame,uint64_t requested){
 int fd=open(PCM,O_RDONLY|O_CLOEXEC);if(fd<0)return -1;
 uint64_t elapsed=now_us()-requested;if(elapsed>500000)elapsed=0;
 off_t offset=(off_t)frame*3200+(off_t)(elapsed*48000/1000000)*2;
 if(lseek(fd,offset,SEEK_SET)<0){close(fd);return -1;}pid_t parent=getpid(),pid=fork();
 if(pid<0){close(fd);return -1;}
 if(!pid){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(126);child_defaults();dup2(fd,0);int n=open("/dev/null",O_WRONLY),log=open(BASE "/audio.log",O_WRONLY|O_CREAT|O_APPEND,0600);dup2(n,1);if(log>=0)dup2(log,2);close_extra();execl("/usr/bin/aplay","aplay","-q","-D","hw:0,0","-t","raw","-f","S16_LE","-r","48000","-c","1","--period-size=512","--buffer-size=2048",(char*)0);_exit(127);}
 close(fd);sh->player=pid;printf("AUDIO_START frame=%u offset=%lld pid=%d\n",frame,(long long)offset,(int)pid);return 0;
}
static void audio_worker(pid_t parent){
 close_extra();prctl(PR_SET_NAME,"c1-video-audio");prctl(PR_SET_PDEATHSIG,SIGTERM);signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);signal(SIGHUP,stop_signal);if(getppid()!=parent)_exit(126);
 int dac=mixer_get(1),hp=mixer_get(15),sp=mixer_get(16);int valid=dac>=0&&hp>=0&&sp>=0;unsigned seq=~0u,vseq=~0u;int playing=0;
 if(!valid){puts("AUDIO_MIXER_SNAPSHOT_FAILED");sh->ready=2;_exit(1);}mixer_set(15,0);sh->ready=1;
 while(!stopping&&!sh->quit){
  if(sh->volseq!=vseq){vseq=sh->volseq;unsigned v=sh->volume;char b[16];snprintf(b,sizeof(b),"%u%%",v);int rc=mix("sset","DAC",b,1,0,0);rc|=mixer_set(16,v?1:0);int saved=persist(v);printf("VOLUME_APPLIED value=%u mixer=%d saved=%d\n",v,rc,saved);}
  unsigned cur=sh->seq;if(!(cur&1)&&cur!=seq){__sync_synchronize();unsigned play=sh->play,frame=sh->frame;uint64_t requested=sh->command_us;__sync_synchronize();if(cur==sh->seq){seq=cur;stop_player();playing=play;if(play&&start_player(frame,requested)){puts("AUDIO_START_FAILED");playing=0;}if(!play)puts("AUDIO_PAUSED");}}
  if(sh->player){int status;pid_t pid=waitpid(sh->player,&status,WNOHANG);if(pid>0){printf("AUDIO_CHILD_END status=%d playing=%d\n",status,playing);sh->player=0;}}
  usleep(5000);
 }
 stop_player();persist(sh->volume);int rc=mixer_set(16,0);rc|=mixer_set(1,dac);rc|=mixer_set(15,hp);rc|=mixer_set(16,sp);printf("AUDIO_RESTORED result=%d\n",rc);_exit(rc?1:0);
}
int media_init(void){
 if(sh){munmap(sh,sizeof(*sh));sh=0;}
 FILE*f=fopen(BASE "/volume","r");if(f){int v;if(fscanf(f,"%d",&v)==1&&v>=0&&v<=100)vol=v;fclose(f);}
 sh=mmap(0,sizeof(*sh),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);if(sh==MAP_FAILED){sh=0;return -1;}memset(sh,0,sizeof(*sh));sh->volume=vol;
 pid_t parent=getpid();worker=fork();if(worker<0)return -1;if(!worker)audio_worker(parent);printf("MEDIA_INIT volume=%d worker=%d\n",vol,(int)worker);return 0;
}
static void command(unsigned play,unsigned frame){if(!sh)return;sh->seq++;__sync_synchronize();sh->play=play;sh->frame=frame;sh->command_us=now_us();__sync_synchronize();sh->seq++;}
void media_play(unsigned frame){command(1,frame);}
void media_pause(void){command(0,0);}
void media_shutdown(void (*heartbeat)(void)){if(!sh||worker<2)return;sh->quit=1;uint64_t end=now_us()+5000000;int status;while(now_us()<end){pid_t r=waitpid(worker,&status,WNOHANG);if(r==worker||(r<0&&errno==ECHILD)){worker=0;return;}if(heartbeat)heartbeat();usleep(10000);}kill(worker,SIGTERM);while(waitpid(worker,&status,0)<0&&errno==EINTR){}worker=0;}
void media_volume_step(int delta){int next=vol+delta;if(next<0)next=0;if(next>100)next=100;vol=next;hud_revision++;hud_until=now_us()+2500000;if(sh){sh->volume=vol;__sync_synchronize();sh->volseq++;}printf("VOLUME_REQUEST value=%d\n",vol);}
int media_volume(void){return vol;}
unsigned media_hud_token(void){return (hud_revision<<1)|(now_us()<hud_until);}
void media_overlay(uint8_t*ranks){if(!(media_hud_token()&1))return;for(int y=134;y<150;y++)memset(ranks+y*296+4,0,82);char text[20];snprintf(text,sizeof(text),"VOL %d%%",vol);for(int i=0;text[i];i++)for(int x=0;x<5;x++)for(int y=0;y<7;y++)if(font5x7[(unsigned char)text[i]][x]&(1<<y))ranks[(138+y)*296+8+i*6+x]=31;}
