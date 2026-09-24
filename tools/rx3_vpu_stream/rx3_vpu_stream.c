/* SPDX-License-Identifier: MPL-2.0 */
/* Continuous i.MX6 IPU -> VPU H.264 framebuffer streamer for RX3 1.19. */

#include "vpu_lib.h"
#include "vpu_io.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long ulong;
typedef unsigned long long u64;
typedef signed int s32;

#define SYS_EXIT 1
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_IOCTL 54
#define SYS_MUNMAP 91
#define SYS_MMAP2 192
#define SYS_CLOCK_GETTIME 263
#define SYS_CLOCK_NANOSLEEP 265
#define SYS_SOCKET 281
#define SYS_BIND 282
#define SYS_LISTEN 284
#define SYS_ACCEPT 285
#define SYS_SEND 289
#define SYS_SETSOCKOPT 294

#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_SNDTIMEO 21
#define MSG_NOSIGNAL 0x4000
#define O_RDONLY 0
#define O_RDWR 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define CLOCK_MONOTONIC 1
#define TIMER_ABSTIME 1

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FB_VMODE_YWRAP 256u
#define IPU_CHECK_TASK 0xc0884901u
#define IPU_QUEUE_TASK 0x40884902u
#define IPU_ALLOC 0xc0044903u
#define IPU_FREE 0x40044904u
#define PAGE_SIZE 4096u
#define WIDTH 640u
#define HEIGHT 400u
#define I420_BYTES (WIDTH * HEIGHT * 3u / 2u)
#define DMA_BYTES ((I420_BYTES + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u))
#define STREAM_BYTES (1024u * 1024u)
#define CAPTURE_BUFFERS 3
#define DEFAULT_PORT 7353u
#define FRAME_INTERVAL_NS 33333333u
#define TARGET_BITRATE_KBPS 3000u
#define PICTURE_QP 20

#define FOURCC(a,b,c,d) ((u32)(a)|((u32)(b)<<8)|((u32)(c)<<16)|((u32)(d)<<24))
#define IPU_PIX_FMT_RGB565 FOURCC('R','G','B','P')
#define IPU_PIX_FMT_YUV420P FOURCC('I','4','2','0')

struct fb_bitfield { u32 offset, length, msb_right; };
struct fb_var_screeninfo {
    u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
    u32 bits_per_pixel, grayscale;
    struct fb_bitfield red, green, blue, transp;
    u32 nonstd, activate, height, width, accel_flags, pixclock;
    u32 left_margin, right_margin, upper_margin, lower_margin;
    u32 hsync_len, vsync_len, sync, vmode, rotate, reserved[5];
};
struct fb_fix_screeninfo {
    char id[16]; u32 smem_start, smem_len, type, type_aux, visual;
    u16 xpanstep, ypanstep, ywrapstep, padding;
    u32 line_length, mmio_start, mmio_len, accel;
    u16 capabilities, reserved[2], tail_padding;
};
struct ipu_pos { u32 x, y; };
struct ipu_crop { struct ipu_pos pos; u32 w, h; };
struct ipu_deinterlace { u8 enable, motion, field_fmt, padding; };
struct ipu_input {
    u32 width, height, format; struct ipu_crop crop; u32 paddr;
    struct ipu_deinterlace deinterlace; u32 paddr_n;
};
struct ipu_alpha { u8 mode, gvalue; u16 padding; u32 loc_alp_paddr; };
struct ipu_colorkey { u8 enable, padding[3]; u32 value; };
struct ipu_overlay {
    u32 width, height, format; struct ipu_crop crop;
    struct ipu_alpha alpha; struct ipu_colorkey colorkey; u32 paddr;
};
struct ipu_output {
    u32 width, height, format; u8 rotate, padding[3];
    struct ipu_crop crop; u32 paddr;
};
struct ipu_task {
    struct ipu_input input; struct ipu_output output;
    u8 overlay_en, overlay_padding[3]; struct ipu_overlay overlay;
    u8 priority, task_id; u16 task_padding; s32 timeout;
};
struct sockaddr_in { u16 family, port; u32 address; u8 zero[8]; };
struct timespec32 { s32 sec, nsec; };
struct timeval32 { s32 sec, usec; };

struct dma_buffer { u32 physical; ulong mapped; int allocated; };
struct vpu_buffer { vpu_mem_desc memory; FrameBuffer frame; };
struct stream_state {
    int fb_fd, ipu_fd, listen_fd, client_fd;
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    struct ipu_task task;
    struct dma_buffer capture[CAPTURE_BUFFERS];
    struct vpu_buffer *references;
    int reference_count;
    struct vpu_buffer subsample[2];
    vpu_mem_desc bitstream;
    EncHandle encoder;
    u8 parameter_sets[512];
    u32 parameter_set_size;
    u32 sequence;
    u32 frame_number;
    int force_idr;
};

typedef char assert_var[(sizeof(struct fb_var_screeninfo)==160)?1:-1];
typedef char assert_fix[(sizeof(struct fb_fix_screeninfo)==68)?1:-1];
typedef char assert_ipu[(sizeof(struct ipu_task)==136)?1:-1];

static inline long sc1(long n,long a){register long r7 __asm__("r7")=n,r0 __asm__("r0")=a;__asm__ volatile("svc 0":"+r"(r0):"r"(r7):"memory");return r0;}
static inline long sc2(long n,long a,long b){register long r7 __asm__("r7")=n,r0 __asm__("r0")=a,r1 __asm__("r1")=b;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory");return r0;}
static inline long sc3(long n,long a,long b,long c){register long r7 __asm__("r7")=n,r0 __asm__("r0")=a,r1 __asm__("r1")=b,r2 __asm__("r2")=c;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");return r0;}
static inline long sc4(long n,long a,long b,long c,long d){register long r7 __asm__("r7")=n,r0 __asm__("r0")=a,r1 __asm__("r1")=b,r2 __asm__("r2")=c,r3 __asm__("r3")=d;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r7):"memory");return r0;}
static inline long sc6(long n,long a,long b,long c,long d,long e,long f){register long r7 __asm__("r7")=n,r0 __asm__("r0")=a,r1 __asm__("r1")=b,r2 __asm__("r2")=c,r3 __asm__("r3")=d,r4 __asm__("r4")=e,r5 __asm__("r5")=f;__asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r3),"r"(r4),"r"(r5),"r"(r7):"memory");return r0;}

static void zero(void *p,u32 n){u8 *b=p;while(n--)*b++=0;}
static void copy(void *d,const void *s,u32 n){u8 *o=d;const u8 *i=s;while(n--)*o++=*i++;}
static u32 length(const char *s){u32 n=0;while(s[n])n++;return n;}
static void log_text(const char *s){sc3(SYS_WRITE,2,(long)s,length(s));}
static int failed(long r){return r<0&&r>=-4095;}
static u16 be16(u16 v){return (u16)((v>>8)|(v<<8));}
static u32 be32(u32 v){return (v>>24)|((v>>8)&0xff00)|((v<<8)&0xff0000)|(v<<24);}
static void put_be64(u8 *p,u64 value){u32 hi=be32((u32)(value>>32)),lo=be32((u32)value);copy(p,&hi,4);copy(p+4,&lo,4);}

static int setup_listener(u16 port){
    struct sockaddr_in sa; int yes=1; long fd;
    fd=sc3(SYS_SOCKET,AF_INET,SOCK_STREAM,0);if(failed(fd)){log_text("rx3-vpu-stream: socket failed\n");return -1;}
    sc6(SYS_SETSOCKOPT,fd,SOL_SOCKET,SO_REUSEADDR,(long)&yes,sizeof(yes),0);
    zero(&sa,sizeof(sa));sa.family=AF_INET;sa.port=be16(port);
    if(failed(sc3(SYS_BIND,fd,(long)&sa,sizeof(sa)))){log_text("rx3-vpu-stream: bind failed\n");goto fail;}
    if(failed(sc2(SYS_LISTEN,fd,1))){log_text("rx3-vpu-stream: listen failed\n");goto fail;}
    return (int)fd;
fail: sc1(SYS_CLOSE,fd);return -1;
}

static int accept_client(struct stream_state *s){
    struct timeval32 timeout={0,100000};long fd;if(s->client_fd>=0)return 0;
    fd=sc3(SYS_ACCEPT,s->listen_fd,0,0);
    if(!failed(fd)){
        sc6(SYS_SETSOCKOPT,fd,SOL_SOCKET,SO_SNDTIMEO,(long)&timeout,sizeof(timeout),0);
        s->client_fd=(int)fd;s->force_idr=1;log_text("rx3-vpu-stream: client connected\n");
        return 1;
    }
    return 0;
}

/* Complete the current record, bounded by SO_SNDTIMEO. Disconnect instead of
 * accumulating later frames when the receiver cannot drain this one. */
static int send_bytes(struct stream_state *s,const void *data,u32 bytes){
    const u8 *cursor=data;long r;if(s->client_fd<0)return -1;
    while(bytes){
        r=sc4(SYS_SEND,s->client_fd,(long)cursor,bytes,MSG_NOSIGNAL);
        if(failed(r)||r==0){sc1(SYS_CLOSE,s->client_fd);s->client_fd=-1;return -1;}
        cursor+=r;bytes-=(u32)r;
    }
    return 0;
}

static int send_packet(struct stream_state *s,u8 type,u8 flags,const void *payload,u32 bytes,u32 sec,u32 nsec){
    u8 h[24];u32 v;zero(h,sizeof(h));copy(h,"RX3H",4);h[4]=1;h[5]=type;h[6]=0;h[7]=0;
    v=be32(s->sequence++);copy(h+8,&v,4);put_be64(h+12,(u64)sec*1000000u+nsec/1000u);v=be32(bytes);copy(h+20,&v,4);h[5]=type;h[6]=0;h[7]=flags;
    if(send_bytes(s,h,sizeof(h))<0)return -1;
    return bytes?send_bytes(s,payload,bytes):0;
}

static int alloc_vpu_buffer(struct vpu_buffer *b,u32 size){
    zero(b,sizeof(*b));b->memory.size=size;if(IOGetPhyMem(&b->memory))return -1;
    if(IOGetVirtMem(&b->memory)==-1){IOFreePhyMem(&b->memory);zero(b,sizeof(*b));return -1;}
    b->frame.bufY=b->memory.phy_addr;b->frame.bufCb=b->frame.bufY+WIDTH*HEIGHT;
    b->frame.bufCr=b->frame.bufCb+WIDTH*HEIGHT/4;b->frame.strideY=WIDTH;b->frame.strideC=WIDTH/2;return 0;
}

static int init_framebuffer(struct stream_state *s){
    u32 addr;int i;s->fb_fd=(int)sc3(SYS_OPEN,(long)"/dev/fb0",O_RDONLY,0);s->ipu_fd=(int)sc3(SYS_OPEN,(long)"/dev/mxc_ipu",O_RDWR,0);
    if(s->fb_fd<0||s->ipu_fd<0)return -1;
    if(failed(sc3(SYS_IOCTL,s->fb_fd,FBIOGET_FSCREENINFO,(long)&s->fixed))||failed(sc3(SYS_IOCTL,s->fb_fd,FBIOGET_VSCREENINFO,(long)&s->variable)))return -1;
    if(s->variable.bits_per_pixel!=16||s->variable.red.offset!=11||s->variable.green.offset!=5||s->variable.blue.offset!=0||s->fixed.line_length!=s->variable.xres_virtual*2u||(s->variable.vmode&FB_VMODE_YWRAP))return -1;
    for(i=0;i<CAPTURE_BUFFERS;i++){
        addr=DMA_BYTES;if(failed(sc3(SYS_IOCTL,s->ipu_fd,IPU_ALLOC,(long)&addr)))return -1;
        s->capture[i].physical=addr;s->capture[i].allocated=1;
        s->capture[i].mapped=sc6(SYS_MMAP2,0,DMA_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,s->ipu_fd,addr/PAGE_SIZE);
        if(failed(s->capture[i].mapped))return -1;
    }
    zero(&s->task,sizeof(s->task));s->task.input.width=s->variable.xres_virtual;s->task.input.height=s->variable.yres_virtual;s->task.input.format=IPU_PIX_FMT_RGB565;s->task.input.crop.w=s->variable.xres;s->task.input.crop.h=s->variable.yres;s->task.input.paddr=s->fixed.smem_start;
    s->task.output.width=WIDTH;s->task.output.height=HEIGHT;s->task.output.format=IPU_PIX_FMT_YUV420P;s->task.output.crop.w=WIDTH;s->task.output.crop.h=HEIGHT;
    return 0;
}

static int capture_frame(struct stream_state *s,int index){
    if(failed(sc3(SYS_IOCTL,s->fb_fd,FBIOGET_VSCREENINFO,(long)&s->variable)))return -1;
    s->task.input.crop.pos.x=s->variable.xoffset;s->task.input.crop.pos.y=s->variable.yoffset;
    s->task.output.paddr=s->capture[index].physical;
    if(sc3(SYS_IOCTL,s->ipu_fd,IPU_CHECK_TASK,(long)&s->task)!=0)return -1;
    return failed(sc3(SYS_IOCTL,s->ipu_fd,IPU_QUEUE_TASK,(long)&s->task))?-1:0;
}

static int copy_header(struct stream_state *s,int type){
    EncHeaderParam h;u32 offset;zero(&h,sizeof(h));h.headerType=type;
    if(vpu_EncGiveCommand(s->encoder,ENC_PUT_AVC_HEADER,&h)!=RETCODE_SUCCESS)return -1;
    offset=h.buf-s->bitstream.phy_addr;if(offset+h.size>STREAM_BYTES||s->parameter_set_size+(u32)h.size>sizeof(s->parameter_sets))return -1;
    copy(s->parameter_sets+s->parameter_set_size,(void *)(s->bitstream.virt_uaddr+offset),h.size);s->parameter_set_size+=h.size;return 0;
}

static int init_encoder(struct stream_state *s){
    EncOpenParam o;EncInitialInfo info;EncExtBufInfo ext;FrameBuffer *refs;int i;
    s->bitstream.size=STREAM_BYTES;if(IOGetPhyMem(&s->bitstream)||IOGetVirtMem(&s->bitstream)==-1)return -1;
    zero(&o,sizeof(o));o.bitstreamBuffer=s->bitstream.phy_addr;o.bitstreamBufferSize=STREAM_BYTES;o.bitstreamFormat=STD_AVC;o.picWidth=WIDTH;o.picHeight=HEIGHT;o.frameRateInfo=30;o.bitRate=TARGET_BITRATE_KBPS;o.gopSize=30;o.mapType=LINEAR_FRAME_MAP;o.rcIntraQp=-1;o.userGamma=24576;o.RcIntervalMode=1;o.MESearchRange=3;o.EncStdParam.avcParam.avc_deblkFilterOffsetAlpha=6;o.EncStdParam.avcParam.avc_chromaQpOffset=10;
    if(vpu_EncOpen(&s->encoder,&o)!=RETCODE_SUCCESS)return -1;
    i=1;vpu_EncGiveCommand(s->encoder,ENC_SET_INTRA_REFRESH_MODE,&i);
    zero(&info,sizeof(info));if(vpu_EncGetInitialInfo(s->encoder,&info)!=RETCODE_SUCCESS)return -1;
    s->reference_count=info.minFrameBufferCount;s->references=(struct vpu_buffer *)0; /* allocated below using a fixed conservative local pool */
    if(s->reference_count<1||s->reference_count>8)return -1;
    /* Avoid a libc allocator: reserve storage from a static pool. */
    {static struct vpu_buffer pool[8];s->references=pool;}
    for(i=0;i<s->reference_count;i++)if(alloc_vpu_buffer(&s->references[i],I420_BYTES)<0)return -1;
    for(i=0;i<2;i++)if(alloc_vpu_buffer(&s->subsample[i],I420_BYTES)<0)return -1;
    {static FrameBuffer frames[8];refs=frames;for(i=0;i<s->reference_count;i++){refs[i]=s->references[i].frame;refs[i].myIndex=i;}}
    zero(&ext,sizeof(ext));if(vpu_EncRegisterFrameBuffer(s->encoder,refs,s->reference_count,WIDTH,WIDTH,s->subsample[0].frame.bufY,s->subsample[1].frame.bufY,&ext)!=RETCODE_SUCCESS)return -1;
    if(copy_header(s,SPS_RBSP)<0||copy_header(s,PPS_RBSP)<0)return -1;
    return 0;
}

static int encode_frame(struct stream_state *s,int index,EncOutputInfo *out){
    EncParam p;FrameBuffer source;int loops=0;zero(&source,sizeof(source));source.bufY=s->capture[index].physical;source.bufCb=source.bufY+WIDTH*HEIGHT;source.bufCr=source.bufCb+WIDTH*HEIGHT/4;source.strideY=WIDTH;source.strideC=WIDTH/2;
    source.myIndex=s->reference_count+index;
    zero(&p,sizeof(p));p.sourceFrame=&source;p.quantParam=PICTURE_QP;p.forceIPicture=s->force_idr||(s->frame_number%30u)==0;p.enableAutoSkip=1;
    if(vpu_EncStartOneFrame(s->encoder,&p)!=RETCODE_SUCCESS)return -1;
    while(vpu_IsBusy()){if(vpu_WaitForInt(40)<0&&++loops>10){vpu_SWReset(s->encoder,0);return -1;}}
    zero(out,sizeof(*out));s->frame_number++;s->force_idr=0;return vpu_EncGetOutputInfo(s->encoder,out)==RETCODE_SUCCESS?0:-1;
}

static int run(struct stream_state *s){
    struct timespec32 next,now;EncOutputInfo out;u32 offset;int capture_index=0;
    sc2(SYS_CLOCK_GETTIME,CLOCK_MONOTONIC,(long)&next);
    for(;;){
        if(accept_client(s)){sc2(SYS_CLOCK_GETTIME,CLOCK_MONOTONIC,(long)&now);send_packet(s,1,1,s->parameter_sets,s->parameter_set_size,now.sec,now.nsec);}
        if(capture_frame(s,capture_index)<0||encode_frame(s,capture_index,&out)<0)return -1;
        sc2(SYS_CLOCK_GETTIME,CLOCK_MONOTONIC,(long)&now);offset=out.bitstreamBuffer-s->bitstream.phy_addr;
        if(!out.skipEncoded&&offset+out.bitstreamSize<=STREAM_BYTES&&s->client_fd>=0)send_packet(s,2,out.picType==0?1:0,(void *)(s->bitstream.virt_uaddr+offset),out.bitstreamSize,now.sec,now.nsec);
        capture_index=(capture_index+1)%CAPTURE_BUFFERS;next.nsec+=FRAME_INTERVAL_NS;while(next.nsec>=1000000000){next.sec++;next.nsec-=1000000000;}
        if(now.sec>next.sec||(now.sec==next.sec&&now.nsec>next.nsec)){next=now;}else sc4(SYS_CLOCK_NANOSLEEP,CLOCK_MONOTONIC,TIMER_ABSTIME,(long)&next,0);
    }
}

static void free_vpu_buffer(struct vpu_buffer *buffer){
    if(!buffer->memory.phy_addr)return;
    if(buffer->memory.virt_uaddr)IOFreeVirtMem(&buffer->memory);
    IOFreePhyMem(&buffer->memory);
    zero(buffer,sizeof(*buffer));
}

static void shutdown_pipeline(struct stream_state *s){
    int i;
    if(s->client_fd>=0)sc1(SYS_CLOSE,s->client_fd);
    if(s->listen_fd>=0)sc1(SYS_CLOSE,s->listen_fd);
    if(s->encoder){
        RetCode result=vpu_EncClose(s->encoder);
        if(result==RETCODE_FRAME_NOT_COMPLETE){vpu_SWReset(s->encoder,0);vpu_EncClose(s->encoder);}
    }
    for(i=0;i<s->reference_count;i++)free_vpu_buffer(&s->references[i]);
    for(i=0;i<2;i++)free_vpu_buffer(&s->subsample[i]);
    if(s->bitstream.virt_uaddr)IOFreeVirtMem(&s->bitstream);
    if(s->bitstream.phy_addr)IOFreePhyMem(&s->bitstream);
    for(i=0;i<CAPTURE_BUFFERS;i++){
        if(s->capture[i].mapped&&!failed(s->capture[i].mapped))sc2(SYS_MUNMAP,s->capture[i].mapped,DMA_BYTES);
        if(s->ipu_fd>=0&&s->capture[i].allocated)sc3(SYS_IOCTL,s->ipu_fd,IPU_FREE,(long)&s->capture[i].physical);
    }
    if(s->ipu_fd>=0)sc1(SYS_CLOSE,s->ipu_fd);
    if(s->fb_fd>=0)sc1(SYS_CLOSE,s->fb_fd);
    vpu_UnInit();
}

__attribute__((used,noinline)) static int program_main(int argc,char **argv){
    static struct stream_state s;u16 port=DEFAULT_PORT;int result;(void)argc;(void)argv;zero(&s,sizeof(s));s.fb_fd=s.ipu_fd=s.listen_fd=s.client_fd=-1;
    if(vpu_Init(0)!=RETCODE_SUCCESS){log_text("rx3-vpu-stream: vpu init failed\n");return 1;}
    if(init_framebuffer(&s)<0||init_encoder(&s)<0){log_text("rx3-vpu-stream: pipeline init failed\n");shutdown_pipeline(&s);return 1;}
    s.listen_fd=setup_listener(port);if(s.listen_fd<0){log_text("rx3-vpu-stream: TCP bind failed\n");shutdown_pipeline(&s);return 1;}
    log_text("rx3-vpu-stream: listening on 169.254.100.2:7353\n");result=run(&s)<0?1:0;shutdown_pipeline(&s);return result;
}

__attribute__((naked,noreturn)) void _start(void){__asm__ volatile("ldr r0,[sp]\nadd r1,sp,#4\nbl program_main\nmov r7,#1\nsvc 0\n");}
