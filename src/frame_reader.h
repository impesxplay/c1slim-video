#pragma once
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#define FR_SLOTS 64
#define FR_BYTES (296*152)
static struct {pthread_t thread;pthread_mutex_t mutex;pthread_cond_t changed;int fd,stop,error;unsigned total,head,size,next,generation;unsigned ids[FR_SLOTS];uint8_t data[FR_SLOTS][FR_BYTES];unsigned waits,seeks;uint64_t max_wait;} fr={.mutex=PTHREAD_MUTEX_INITIALIZER,.changed=PTHREAD_COND_INITIALIZER};
static uint64_t fr_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static void*fr_worker(void*unused){
 (void)unused;setpriority(PRIO_PROCESS,0,0);uint8_t frame[FR_BYTES];pthread_mutex_lock(&fr.mutex);
 while(!fr.stop){
  while(fr.size==FR_SLOTS&&!fr.stop)pthread_cond_wait(&fr.changed,&fr.mutex);if(fr.stop)break;
  unsigned idx=fr.next,gen=fr.generation;pthread_mutex_unlock(&fr.mutex);
  size_t n=0;while(n<FR_BYTES){ssize_t z=pread(fr.fd,frame+n,FR_BYTES-n,20+(off_t)idx*FR_BYTES+n);if(z<0&&errno==EINTR)continue;if(z<=0)break;n+=z;}
  pthread_mutex_lock(&fr.mutex);if(gen!=fr.generation)continue;
  if(n!=FR_BYTES){fr.error=1;pthread_cond_broadcast(&fr.changed);break;}
  unsigned slot=(fr.head+fr.size)%FR_SLOTS;memcpy(fr.data[slot],frame,FR_BYTES);fr.ids[slot]=idx;fr.size++;fr.next=(idx+1)%fr.total;pthread_cond_broadcast(&fr.changed);
 }
 pthread_mutex_unlock(&fr.mutex);return 0;
}
static int fr_active;
static int fr_init(const char*path,unsigned count){
 fr.stop=fr.error=0;fr.head=fr.size=fr.next=fr.generation=fr.waits=fr.seeks=0;fr.max_wait=0;
 fr.fd=open(path,O_RDONLY|O_CLOEXEC);if(fr.fd<0)return -1;fr.total=count;
 if(pthread_create(&fr.thread,0,fr_worker,0)){close(fr.fd);return -1;}
 fr_active=1;
 pthread_mutex_lock(&fr.mutex);while(fr.size<FR_SLOTS&&!fr.error)pthread_cond_wait(&fr.changed,&fr.mutex);pthread_mutex_unlock(&fr.mutex);
 printf("FRAME_BUFFER ready=%u capacity=%u bytes=%u error=%d\n",fr.size,FR_SLOTS,FR_SLOTS*FR_BYTES,fr.error);return fr.error?-1:0;
}
static int fr_get(unsigned index,uint8_t*out){
 uint64_t t=fr_us();pthread_mutex_lock(&fr.mutex);
 unsigned expected=fr.size?fr.ids[fr.head]:fr.next;
 if(expected!=index){fr.head=fr.size=0;fr.next=index;fr.generation++;fr.error=0;fr.seeks++;pthread_cond_broadcast(&fr.changed);}
 int waited=0;while(!fr.size&&!fr.error){waited=1;pthread_cond_wait(&fr.changed,&fr.mutex);}
 if(fr.error||fr.ids[fr.head]!=index){pthread_mutex_unlock(&fr.mutex);return -1;}
 memcpy(out,fr.data[fr.head],FR_BYTES);fr.head=(fr.head+1)%FR_SLOTS;fr.size--;pthread_cond_broadcast(&fr.changed);pthread_mutex_unlock(&fr.mutex);
 uint64_t dt=fr_us()-t;if(dt>fr.max_wait)fr.max_wait=dt;if(waited)fr.waits++;return 0;
}
static void fr_close(void){if(!fr_active)return;pthread_mutex_lock(&fr.mutex);fr.stop=1;pthread_cond_broadcast(&fr.changed);pthread_mutex_unlock(&fr.mutex);pthread_join(fr.thread,0);close(fr.fd);fr_active=0;printf("FRAME_BUFFER_END waits=%u seeks=%u max_get_us=%llu\n",fr.waits,fr.seeks,(unsigned long long)fr.max_wait);}
