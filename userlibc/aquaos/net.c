#include <aquaos/net.h>
#include <aquaos/syscall.h>
#include <aquaos/sysret.h>
#include <errno.h>
int aquaos_ping_start(uint32_t ip) {
    return aquaos_sysret(aquaos_syscall1(SYS_PING_START, ip));
}
int aquaos_ping_result(void) {
    return aquaos_sysret(aquaos_syscall1(SYS_PING_RESULT, 0));
}

int aquaos_udp_socket(void) {
    return aquaos_sysret(aquaos_syscall1(SYS_UDP_SOCKET, 0));
}
int aquaos_udp_bind(int fd, uint16_t port) {
    return aquaos_sysret(aquaos_syscall3(SYS_UDP_BIND, fd, port, 0));
}
int aquaos_udp_sendto(int fd, const void *data, size_t length, uint32_t ip, uint16_t port) {
    aquaos_udp_request_t request = {(uintptr_t)data, length, ip, port, 0};
    return aquaos_sysret(aquaos_syscall3(SYS_UDP_SEND, fd, (intptr_t)&request, 0));
}
int aquaos_udp_recvfrom(int fd, void *data, size_t capacity, aquaos_udp_address *source) {
    if (!source) { errno = EINVAL; return -1; }
    aquaos_udp_request_t request = {(uintptr_t)data, capacity, 0, 0, 0};
    int result = aquaos_sysret(aquaos_syscall3(SYS_UDP_RECEIVE, fd, (intptr_t)&request, (intptr_t)source));
    return result;
}

int aquaos_tcp_socket(void) { return aquaos_sysret(aquaos_syscall1(SYS_TCP_SOCKET,0)); }
int aquaos_tcp_connect(int fd,uint32_t ip,uint16_t port) {
    return aquaos_sysret(aquaos_syscall3(SYS_TCP_CONNECT,fd,ip,port));
}
int aquaos_tcp_status(int fd) { return aquaos_sysret(aquaos_syscall1(SYS_TCP_STATUS,fd)); }
int aquaos_tcp_shutdown(int fd) { return aquaos_sysret(aquaos_syscall1(SYS_TCP_SHUTDOWN,fd)); }

int aquaos_get_net_info(aquaos_net_info *info) {
    return aquaos_sysret(aquaos_syscall1(SYS_NET_INFO, (intptr_t)info));
}
