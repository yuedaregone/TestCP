#include <string.h>
#include "skt.h"
#include "array.h"
#include "buffer.h"
#ifndef _WIN32
#include <sys/select.h>
#include <sys/time.h>
#endif


struct skt_server* skt_server_create()
{
#ifdef _WIN32
	WSADATA data;
	int result = WSAStartup(0x0202, &data);
	if (result != 0)
	{
		int err = GET_ERROR_CODE;
		skt_error("Error:Start up socket! %d\n", err);
		return NULL;
	}
#endif
    struct skt_server* skt = (struct skt_server*)malloc(sizeof(struct skt_server));
    skt->skt = INVALID_SOCKET;
    skt->sta = skt_noactivate;   
	skt->err_no = SKT_OK; 
    memset(skt->conn_ip, 0, IP_MAX_LEN);    
    skt->conn_port = 0;
	skt->recv_cb = NULL;
    skt->skt_ios = array_create(sizeof(struct skt_io*));
    return skt;
}

void skt_server_destroy(struct skt_server* skt)
{
    for (int i = 0; i < skt->skt_ios->count; ++i)
    {
        struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, i);
        skt_destroy_io(io);
    }
    array_destroy(skt->skt_ios);
    free(skt);

#ifdef _WIN32
	int result = WSACleanup();
	if (result != 0)
	{
		int err = GET_ERROR_CODE;
		skt_error("Error:Destroy socket! %d\n", err);
	}
#endif
}

int skt_server_open(struct skt_server* skt, const char* ip, uint16_t port)
{
    skt->skt = socket(AF_INET, SOCK_STREAM, 0);
	if (skt->skt == INVALID_SOCKET)
	{
		int err = GET_ERROR_CODE;
		skt_error("Socket Error: Create Server Socket Error! %d\n", err);
		skt->err_no = err;
		return SKT_ERR;
	}

	int32_t op = 1;
	int32_t ret = setsockopt(skt->skt, SOL_SOCKET, SO_REUSEADDR, (char*)&op, sizeof(op));
	if (ret != 0)
	{
		int err = GET_ERROR_CODE;
		skt_error("Socket Error: Set Socket Opt Error! %d\n", err);
		skt->err_no = err;
		return SKT_ERR;
	}
	if (ip != NULL && strlen(ip) > 0)
	{
		memcpy(skt->conn_ip, ip, strlen(ip));
	}
	skt->conn_port = port;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	if (strlen(skt->conn_ip) == 0)
	{
		printf("%s\n", "bind any");
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(skt->conn_ip);
	}
	addr.sin_port = htons(port);

	ret = bind(skt->skt, (struct sockaddr*)&addr, sizeof(addr));
	if (ret != 0)
	{
		int err = GET_ERROR_CODE;
		skt_error("Socket Error: Bind Server Socket Error! %d\n", err);
		skt_server_close(skt);
		skt->err_no = err;
		return SKT_ERR;
	}

	ret = listen(skt->skt, SOMAXCONN);
	if (ret != 0)
	{
		int err = GET_ERROR_CODE;
		skt_error("Socket Error: Listen Socket Error! %d\n", err);
		skt_server_close(skt);
		skt->err_no = err;
		return SKT_ERR;
	}
	skt_set_non_block(skt->skt);   
	skt->sta = skt_created;
	return SKT_OK;
}

static int skt_accept_client(struct skt_server* skt)
{
    struct sockaddr_in addr;
#ifdef _WIN32
	int size = (int)sizeof(addr);
#else
	socklen_t size = (socklen_t)sizeof(addr);
#endif
	int ret = accept(skt->skt, (struct sockaddr*)&addr, &size);
	if (ret <= SKT_OK)
	{
		int err = GET_ERROR_CODE;;
#ifdef _WIN32
		if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
#else
        if (err == EINPROGRESS || err == EWOULDBLOCK)
#endif
		{
			skt_error("Socket Error: Connect Error %d\n", err);
			skt_server_close(skt);
			skt->err_no = err;
			return SKT_ERR;
		}
		skt->err_no = err;
		return SKT_ERR;
	}
	return ret;
}

static void skt_check_accept(struct skt_server* skt)
{
	int sk = skt_accept_client(skt);
	if (sk != SKT_ERR)
	{
		printf("accpet - %d\n", sk);
		struct skt_io* io = skt_create_io(sk, skt->recv_cb);
		array_add(skt->skt_ios, (void*)&io);
		skt_set_non_block(sk); 
	}
}

static int skt_select_fds(struct skt_server* skt, double maxtime, fd_set* rd, fd_set* wr)
{    
	//fd_set *rd = NULL, *wr = NULL;
	struct timeval tmout;

	FD_ZERO(rd); FD_ZERO(wr);
	FD_SET(skt->skt, rd);
    skt_d max = skt->skt;
    for (int i = 0; i < skt->skt_ios->count; ++i)
    {
        struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, i);
        FD_SET(io->skt, rd);
		if (io->wt_flag) 
			FD_SET(io->skt, wr);
        max = max > io->skt ? max : io->skt;
    }

	tmout.tv_sec = (long)maxtime;
	tmout.tv_usec = (long)(1000000 * (maxtime - (long)maxtime));
	
	return select(max + 1, rd, wr, NULL, &tmout);
}


static int skt_find_io_index(struct skt_server* skt, skt_d id)
{
	int i = -1;
	for (i = 0; i < skt->skt_ios->count; ++i)
	{
		struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, i);
		if (io->skt == id)
		{
			break;
		}
	}
	return i;
}

static int32_t skt_send_to_skt_index(struct skt_server* skt, int idx, int8_t* buf, int32_t len)
{
	if (idx < 0 || idx >= skt->skt_ios->count)
		return SKT_ERR;
	struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, idx);
	return skt_send_io(io, buf, len);
}

int32_t skt_server_send_to(struct skt_server* skt, skt_d id, int8_t* buf, int32_t len)
{
	int idx = skt_find_io_index(skt, id);
	return skt_send_to_skt_index(skt, idx, buf, len);
}

void skt_server_update_state(struct skt_server* skt)
{
    fd_set fd_read;
	fd_set fd_write;
    int num = skt_select_fds(skt, 0, &fd_read, &fd_write);	
    if (num == 0) return;
    if (num == -1)
    {
        int err = GET_ERROR_CODE;;
#ifndef _WIN32
        if (err == EINTR)
            return;        
#endif		
        skt_error("Socket Error: Connect Error %d\n", err);
		skt_server_close(skt);
        skt->err_no = err;
        return;
    }
	
    if (FD_ISSET(skt->skt, &fd_read))
    {
        skt_check_accept(skt);
    }
        
    static int rmv[1024]; int idx = 0;
    for (int i = 0; i < skt->skt_ios->count; ++i)
    {
        struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, i);
		int readable = FD_ISSET(io->skt, &fd_read);
		int writeable = io->wt_flag && (FD_ISSET(io->skt, &fd_write));
		 
        if (readable || writeable)
        {
			if (readable)			
				skt_update_recv_io(io);

			if (writeable)
				skt_update_send_io(io);
            
            if (io->err_no != SKT_OK)
            {
                rmv[idx++] = i;
            }
        }
    }	
    for (int i = idx - 1; i >= 0; --i)
    {
		struct skt_io* io = *(struct skt_io**)array_index(skt->skt_ios, rmv[i]);
		skt_close(io->skt);
		skt_destroy_io(io);
        array_remove(skt->skt_ios, rmv[i]);
    }
}

void skt_server_close(struct skt_server* skt)
{
	skt_close(skt->skt);
}
