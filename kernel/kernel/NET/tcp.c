#include <kernel/tcp.h>
#include <kernel/net.h>
#include <kernel/errno.h>
#include <libk/string.h>

#define TCP_SLOTS 8
#define TCP_RX 8192U
#define TCP_MSS 536U
#define TCP_IDLE_TICKS 3000U
#define TCP_TIME_WAIT_TICKS 12000U
#define FIN 1U
#define SYN 2U
#define RST 4U
#define PSH 8U
#define ACK 16U
/* One outstanding segment: conservative stop-and-wait transmission. */
enum { FREE, IDLE, ROUTE, SYN_SENT, OPEN, CLOSING, TIME_WAIT, DONE, ERROR };
struct tcp_connection {
    int state, error, detached, closing, peer_fin, fin_sent, fin_acked, ack_pending;
    uint32_t ip, una, next, receive, tx_seq, changed, last_send, wl_seq, wl_ack;
    uint16_t port, local_port, window, mss;
    unsigned tx_length, tx_flags, tries, rto, head, count;
    uint8_t tx[TCP_MSS], rx[TCP_RX];
};
static struct tcp_connection connections[TCP_SLOTS];
static uint32_t tcp_now, isn_counter;
static uint16_t next_port = 49152;
static unsigned tcp_get16(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }
static uint32_t tcp_get32(const uint8_t *p) {
    return ((uint32_t)tcp_get16(p)<<16)|tcp_get16(p+2);
}
static void tcp_put16(uint8_t *p, unsigned n) { p[0]=n>>8; p[1]=n; }
static void tcp_put32(uint8_t *p, uint32_t n) { tcp_put16(p,n>>16);tcp_put16(p+2,n); }
static int before(uint32_t a, uint32_t b) { return (int32_t)(a-b)<0; }
static uint32_t tcp_sum(const uint8_t *p, size_t n, uint32_t sum) {
    while(n>=2) {sum+=tcp_get16(p);p+=2;n-=2;}
    if(n) sum+=(unsigned)*p<<8;
    return sum;
}
static uint16_t tcp_check(uint32_t sum) {
    while(sum>>16) sum=(sum&65535)+(sum>>16);
    return (uint16_t)~sum;
}
static struct tcp_connection *tcp_get(int h) {
    return h>=0 && h<TCP_SLOTS && connections[h].state && !connections[h].detached ? &connections[h] : 0;
}
static int tcp_emit(struct tcp_connection *c, unsigned flags, uint32_t seq,
                     const void *data, unsigned length) {
    uint8_t segment[24+TCP_MSS] = {0};
    unsigned header = flags&SYN ? 24 : 20;
    tcp_put16(segment,c->local_port);tcp_put16(segment+2,c->port);
    tcp_put32(segment+4,seq);tcp_put32(segment+8,flags&ACK?c->receive:0);
    segment[12]=(header/4)<<4;segment[13]=flags;
    tcp_put16(segment+14,c->peer_fin?0:TCP_RX-c->count);
    if(flags&SYN) { segment[20]=2;segment[21]=4;tcp_put16(segment+22,1460); }
    if(length) kmemcpy(segment+header,data,length);
    uint32_t local = net_local_ip();
    uint32_t sum=(local>>16)+(local&65535)+(c->ip>>16)+(c->ip&65535)+6+header+length;
    tcp_put16(segment+16,tcp_check(tcp_sum(segment,header+length,sum)));
    return net_ipv4_send(c->ip,6,segment,header+length);
}
static void tcp_fail(struct tcp_connection *c, int error) {
    c->error=error;c->state=ERROR;c->tx_flags=0;
    if(c->detached) c->state=FREE;
}
static void tcp_ack(struct tcp_connection *c) {
    c->ack_pending=tcp_emit(c,ACK,c->next,0,0)!=0;
}
static void tcp_transmit(struct tcp_connection *c) {
    int result=tcp_emit(c,c->tx_flags,c->tx_seq,c->tx,c->tx_length);
    if(result && result!=-K_EAGAIN) {tcp_fail(c,-result);return;}
    c->last_send=tcp_now;
    if(!result) {++c->tries;c->ack_pending=0;}
}
static void tcp_queue(struct tcp_connection *c,unsigned flags,unsigned length) {
    c->tx_seq=c->next;c->tx_flags=flags;c->tx_length=length;
    c->next+=length+!!(flags&SYN)+!!(flags&FIN);
    c->tries=0;c->rto=100;c->last_send=tcp_now-1;
    tcp_transmit(c);
}
static void tcp_finish(struct tcp_connection *c) {
    if(!c->closing || c->tx_flags || c->fin_sent || c->state!=CLOSING) return;
    if(!c->window) return;
    c->fin_sent=c->peer_fin?2:1;
    tcp_queue(c,FIN|ACK,0);
}
void net_tcp_init(void) {
    for(unsigned i=0;i<TCP_SLOTS;++i) connections[i].state=FREE;
    tcp_now=0;
}
int net_tcp_socket(void) {
    for(int i=0;i<TCP_SLOTS;++i) if(!connections[i].state) {
        struct tcp_connection *c=&connections[i];
        /* Clear metadata only; receive/transmit bytes are never exposed stale. */
        kmemset(c,0,sizeof(*c));c->state=IDLE;c->mss=TCP_MSS;
        for(;;) {
            c->local_port=next_port++;
            if(next_port<49152) next_port=49152;
            int busy=0;
            for(int j=0;j<TCP_SLOTS;++j)
                if(j!=i && connections[j].state && connections[j].local_port==c->local_port) busy=1;
            if(!busy) break;
        }
        return i;
    }
    return -K_ENOSPC;
}
int net_tcp_connect(int handle,uint32_t ip,uint16_t port) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return -K_EBADF;
    if(c->state!=IDLE || !port) return -K_EINVAL;
    uint8_t mac[6];
    int result=net_resolve(ip,mac);
    if(result && result!=-K_EAGAIN) return result;
    c->ip=ip;c->port=port;c->state=ROUTE;c->changed=tcp_now;
    /* Clock plus advancing counter avoids sequence reuse across socket slots. */
    uint32_t lo,hi;
    __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi));
    isn_counter+=64001;c->una=c->next=lo^hi^isn_counter;
    return 0;
}
int net_tcp_status(int handle) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return -K_EBADF;
    if(c->state==ERROR) return -c->error;
    return c->state>=OPEN ? 0 : -K_EAGAIN;
}
int net_tcp_send(int handle,const void *data,size_t length) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return -K_EBADF;
    if(!data && length) return -K_EINVAL;
    if(c->state==ERROR) return -c->error;
    if(c->closing || c->state>=CLOSING) return -K_EPIPE;
    if(c->state!=OPEN || c->tx_flags) return -K_EAGAIN;
    if(!length) return 0;
    if(!c->window) return -K_EAGAIN;
    if(length>c->mss) length=c->mss;
    if(length>c->window) length=c->window;
    c->changed=tcp_now;
    kmemcpy(c->tx,data,length);tcp_queue(c,ACK|PSH,(unsigned)length);
    return c->state==ERROR ? -c->error : (int)length;
}
int net_tcp_read(int handle,void *data,size_t capacity) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return -K_EBADF;
    if(!data && capacity) return -K_EINVAL;
    if(!capacity) return 0;
    if(c->count) {
        if(capacity>c->count) capacity=c->count;
        for(unsigned i=0;i<capacity;++i) {
            ((uint8_t *)data)[i]=c->rx[c->head];c->head=(c->head+1)%TCP_RX;
        }
        c->count-=capacity;c->changed=tcp_now;
        if(c->state!=ERROR) tcp_ack(c);
        return (int)capacity;
    }
    if(c->state==ERROR) return -c->error;
    return c->peer_fin ? 0 : -K_EAGAIN;
}
int net_tcp_shutdown(int handle) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return -K_EBADF;
    if(c->state==ERROR) return -c->error;
    if(c->state<OPEN) return -K_EAGAIN;
    if(!c->closing) {c->closing=1;c->state=CLOSING;c->changed=tcp_now;}
    tcp_finish(c);return 0;
}
void net_tcp_release(int handle) {
    struct tcp_connection *c=tcp_get(handle);
    if(!c) return;
    if(c->state>=OPEN && c->state<=CLOSING && !c->count) {
        net_tcp_shutdown(handle);c->detached=1;return;
    }
    if(c->state>=SYN_SENT && c->state<=CLOSING) tcp_emit(c,RST|ACK,c->next,0,0);
    if(c->state==TIME_WAIT) c->detached=1;
    else c->state=FREE;
}
void net_tcp_poll(uint32_t now) {
    tcp_now=now;
    for(unsigned i=0;i<TCP_SLOTS;++i) {
        struct tcp_connection *c=&connections[i];
        if(c->state==FREE || c->state==IDLE || c->state==DONE || c->state==ERROR) continue;
        if(c->ack_pending) tcp_ack(c);
        if(c->state==TIME_WAIT) {
            if((uint32_t)(now-c->changed)>=TCP_TIME_WAIT_TICKS) {
                c->state=c->detached?FREE:DONE;
            }
            continue;
        }
        if((uint32_t)(now-c->changed)>=TCP_IDLE_TICKS) {
            if(c->state>=OPEN) tcp_emit(c,RST|ACK,c->next,0,0);
            tcp_fail(c,K_ETIMEDOUT);continue;
        }
        if(c->state==ROUTE) {
            uint8_t mac[6];
            int r=net_resolve(c->ip,mac);
            if(r==-K_EAGAIN) continue;
            if(r) {tcp_fail(c,-r);continue;}
            c->state=SYN_SENT;tcp_queue(c,SYN,0);continue;
        }
        if(c->tx_flags && (uint32_t)(now-c->last_send)>=(c->tries?c->rto:1)) {
            if(c->tries>=5) {tcp_fail(c,K_ETIMEDOUT);continue;}
            if(c->tries && c->rto<800) c->rto*=2;
            tcp_transmit(c);
        }
        tcp_finish(c);
    }
}
void net_tcp_input(const uint8_t *ip,unsigned header,unsigned total) {
    const uint8_t *t=ip+header;
    if(total<header || total-header<20) return;
    unsigned bytes=total-header;
    unsigned th=(t[12]>>4)*4;
    if(th<20 || th>bytes || (t[12]&15) || (t[13]&0xc0)) return;
    if(tcp_check(tcp_sum(t,bytes,tcp_sum(ip+12,8,6+bytes)))) return;
    unsigned flags=t[13],length=bytes-th;
    if(flags&32) return; /* Urgent data is unsupported. */
    unsigned mss=TCP_MSS;
    for(unsigned at=20;at<th;) {
        unsigned kind=t[at++];
        if(!kind) break;
        if(kind==1) continue;
        if(at>=th || t[at]<2 || t[at]>th-(at-1)) return;
        unsigned n=t[at++];
        if(kind==2) {
            if(n!=4 || !(flags&SYN)) return;
            unsigned offered=tcp_get16(t+at);
            if(!offered) return;
            if(offered<mss) mss=offered;
        }
        at+=n-2;
    }
    uint32_t source=tcp_get32(ip+12),seq=tcp_get32(t+4),ack=tcp_get32(t+8);
    for(unsigned i=0;i<TCP_SLOTS;++i) {
        struct tcp_connection *c=&connections[i];
        if(c->state<SYN_SENT || c->state==ERROR || c->state==DONE || c->ip!=source ||
           c->port!=tcp_get16(t) || c->local_port!=tcp_get16(t+2)) continue;
        if(c->state==SYN_SENT) {
            if(!(flags&ACK) || ack!=c->next) return;
            if(flags&RST) {tcp_fail(c,K_ECONNREFUSED);return;}
            if(!(flags&SYN) || (flags&FIN) || length) return;
            c->receive=seq+1;c->una=ack;c->tx_flags=0;c->state=OPEN;
            c->window=tcp_get16(t+14);c->mss=mss;c->wl_seq=seq;c->wl_ack=ack;
            c->changed=tcp_now;tcp_ack(c);return;
        }
        if(flags&SYN) {tcp_ack(c);return;} /* Includes retransmitted SYN/ACK. */
        if(flags&RST) {
            if(seq==c->receive) tcp_fail(c,K_ECONNRESET);
            else tcp_ack(c);
            return;
        }
        if(!(flags&ACK)) return;
        if(c->state==TIME_WAIT) {
            if((flags&FIN) && seq+length+1==c->receive) {
                tcp_ack(c);c->changed=tcp_now;
            }
            return;
        }
        unsigned space=TCP_RX-c->count;
        if(seq!=c->receive && (before(seq,c->receive) ?
           !length || !before(c->receive,seq+length) :
           !space || !before(seq,c->receive+space))) {tcp_ack(c);return;}
        if(before(c->next,ack)) {tcp_ack(c);return;}
        if(!before(ack,c->una)) {
            if(before(c->una,ack)) {
                unsigned advanced=ack-c->una;
                c->una=ack;c->changed=tcp_now;
                if(ack==c->next) {
                    if(c->tx_flags&FIN) c->fin_acked=1;
                    c->tx_flags=0;c->tx_length=0;
                } else if(c->tx_length && advanced<=c->tx_length) {
                    for(unsigned j=advanced;j<c->tx_length;++j) c->tx[j-advanced]=c->tx[j];
                    c->tx_length-=advanced;c->tx_seq=ack;c->tries=0;c->rto=100;
                }
            }
            if(before(c->wl_seq,seq) || (c->wl_seq==seq && !before(ack,c->wl_ack))) {
                c->window=tcp_get16(t+14);c->wl_seq=seq;c->wl_ack=ack;
            }
        }
        if(length || (flags&FIN)) {
            if(!space && !c->peer_fin) {tcp_ack(c);tcp_finish(c);return;}
            uint32_t end=seq+length;
            if(before(seq,c->receive)) {
                unsigned trim=c->receive-seq;
                if(trim>length) trim=length;
                seq+=trim;th+=trim;length-=trim;
            }
            if(seq==c->receive && !c->peer_fin) {
                unsigned take=length<space?length:space;
                for(unsigned j=0;j<take;++j) c->rx[(c->head+c->count+j)%TCP_RX]=t[th+j];
                c->count+=take;c->receive+=take;
                if(take) c->changed=tcp_now;
                if((flags&FIN) && c->receive==end) {++c->receive;c->peer_fin=1;c->changed=tcp_now;}
            }
            tcp_ack(c);
        }
        if(c->closing && c->fin_acked && c->peer_fin) {
            /* Active closes wait 2 MSL; passive closes may release immediately. */
            if(c->fin_sent==2) c->state=c->detached?FREE:DONE;
            else {c->state=TIME_WAIT;c->changed=tcp_now;}
        }
        tcp_finish(c);
        return;
    }
}

void net_tcp_config_changed(void) {
    for (unsigned i = 0; i < TCP_SLOTS; ++i)
        if (connections[i].state != FREE && connections[i].state != IDLE)
            tcp_fail(&connections[i], K_ECONNRESET);
}
