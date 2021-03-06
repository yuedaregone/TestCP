#include "skt.h"
#include "msgs.h"
#include <stdlib.h>
#include "buffer.h"
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#define CONFIG_IMPLEMENT
#include "config.h"

static struct skt_server* s_sock = NULL;
static struct config* s_cfg = NULL;

void net_init()
{
	if (s_sock != NULL)
		return;

	dispatch_init();
	s_sock = skt_server_create();
	s_sock->recv_cb = dispatch_update_data;
}

void net_destroy()
{
	dispatch_destroy();
	skt_server_destroy(s_sock);
	s_sock = NULL;
}

void net_listen(const char* ip, uint16_t port)
{
	skt_server_open(s_sock, ip, port);
}

void net_send_to(uint16_t msg_id, void* data, uint16_t length)
{
	uint32_t sz = MSG_HEAD_SIZE + length;
	if (sz > MAX_MSG_SIZE)
	{
		printf("msg is too long! %d\n", sz);
		return;
	}
	/*uint32_t lft = (uint32_t)buf_space_circle(s_sock->send_buf);
	if (sz > lft)
	{
		printf("send buffer is full! %d\n", lft);
		return;
	}*/
	struct msg_head head;
	head.msg_id = msg_id;
	head.length = length;
	skt_server_send_to(s_sock, 0, (int8_t*)&head, (int32_t)MSG_HEAD_SIZE);
	skt_server_send_to(s_sock, 0, (int8_t*)data, (int32_t)length);
}

void net_update()
{
	skt_server_update_state(s_sock);
}

int main()
{
	const char* ip = "192.168.31.132";
	int port = 38086;

	s_cfg = config_create();
	config_load_local_data(s_cfg, "/Users/yuegangyang/ts.cfg");

	if (0 == config_load_local_data(s_cfg, "/Users/yuegangyang/ts.cfg"))
	{
		ip = config_get_str_value(s_cfg, "IP");
		port = config_get_int_value(s_cfg, "Port");
	}

	net_init();
	net_listen(ip, port);

	while (1)
	{
		net_update();
#ifdef _WIN32
		Sleep(100);
#else
		usleep(100000);
#endif        
	}
	net_destroy();
	return 0;
}

