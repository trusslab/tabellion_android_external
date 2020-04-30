/*
 *
 * Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* This code is designed to make calls to different primitives
 * through hypercalls to the hypervisor and optee invoke calls
 * to the optee core
 * Android app may run this code using 'optee_example_hello_world param'
 * param is between 1-5
 */

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

/* OP-TEE TEE client API (built by optee_client) */
#include <tee_client_api.h>

#define STATS_UUID \
                { 0xd96a5b40, 0xe2c7, 0xb1af, \
                        { 0x87, 0x94, 0x10, 0x02, 0xa5, 0xd5, 0xc6, 0x1b } }

//Saeed time
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h> /* gettimeofday() */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <asm-generic/unistd.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include <time.h> /* for time() and ctime() */

/* The function IDs implemented in this TA */
#define TA_SYNC_START		0
#define TA_SYNC_SERVER		1
#define TA_SIGN_FB		2
#define TA_SIGN_CAM		5

#define UTC_NTP 2208988800U /* 1970 - 1900 */
uint32_t t2_time0;
uint32_t t3_time0;
uint32_t t2_time1;
uint32_t t3_time1;
uint32_t nw_time0;
uint32_t nw_time1;
uint32_t delay;

/* get Timestamp for NTP in LOCAL ENDIAN */
void gettime64(uint32_t ts[])
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	ts[0] = tv.tv_sec + UTC_NTP;
	ts[1] = (4294*(tv.tv_usec)) + ((1981*(tv.tv_usec))>>11);
}

int die(const char *msg)
{
	fputs(msg, stderr);
	exit(-1);
}

int useage(const char *path)
{
	printf("Useage:\n\t%s <server address>\n", path);
	return 1;
}

int open_connect(const char* server)
{
	int s;
	struct addrinfo *saddr;

	/* printf("Connecting to server: %s\n", server); */
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		die("Can not create socket.\n");
	}

	if (0 != getaddrinfo(server, "123", NULL, &saddr)) {
		die("Server address not correct.\n");
	}

	if (connect(s, saddr->ai_addr, saddr->ai_addrlen) != 0) {
		printf("Oh dear, something went wrong with connect()! %s\n", strerror(errno));
		die("Connect error\n");
	}

	freeaddrinfo(saddr);

	return s;
}

void request(int fd)
{
	unsigned char buf[48] = {0};
	uint32_t tts[2]; /* Transmit Timestamp */

	/* LI VN MODE = 00 100 011*/
	buf[0] = 0x23;

	gettime64(tts);
	//printf("T1=%u\n", tts);
	(*(uint32_t *)&buf[40]) = htonl(tts[0]);
	(*(uint32_t *)&buf[44])= htonl(tts[1]);
	
	nw_time0 = (*(uint32_t *)&buf[40]);
	nw_time1 = (*(uint32_t *)&buf[44]);

	if (send(fd, buf, 48, 0) !=48 ) {
		die("Send error\n");
	}
}

unsigned char sig_buf[128];

void get_reply(int fd)
{
	unsigned char buf[176]; // 48 + 128 for signature
	uint32_t *pt;
	uint32_t t1[2]; /* t1 = Originate Timestamp  */
	uint32_t t2[2]; /* t2 = Receive Timestamp @ Server */
	uint32_t t3[2]; /* t3 = Transmit Timestamp @ Server */
	uint32_t t4[2]; /* t4 = Receive Timestamp @ Client */
	double T1, T2, T3, T4;
	double tfrac = 4294967296.0;
	time_t curr_time;
	time_t diff_sec;

	if (recv(fd, buf, 176, 0) < 176) {
		die("Receive error\n");
	}
	gettime64(t4);
	pt = (uint32_t *)&buf[24];

	t1[0] = htonl(*pt++);
	t1[1] = htonl(*pt++);

	t2[0] = htonl(*pt++);
	t2[1] = htonl(*pt++);

	t3[0] = htonl(*pt++);
	t3[1] = htonl(*pt++);

	memcpy(sig_buf, pt, 128);

	T1 = t1[0] + t1[1]/tfrac;
	T2 = t2[0] + t2[1]/tfrac;
	T3 = t3[0] + t3[1]/tfrac;
	T4 = t4[0] + t4[1]/tfrac;

	t2_time0 = t2[0];
	t3_time0 = t3[0];
	t2_time1 = t2[1];
	t3_time1 = t3[1];

	delay = ((int32_t)(t2[0] - t1[0]) + (int32_t)(t4[0] - t3[0])) ;

	diff_sec = ((int32_t)(t2[0] - t1[0]) + (int32_t)(t3[0] - t4[0])) /2;
	curr_time = time(NULL) - diff_sec;
}

int nonce;
int client(const char* server, TEEC_Session sess, TEEC_Operation op, uint32_t err_origin)
{
	int fd;
	TEEC_Result res;

	unsigned char packet[144];
	uint32_t *ptr = packet;
	void* virt;
	struct timeval now;


	/* SYNC start */
	/*
	 * Prepare the argument. Pass a value in the first parameter,
	 * the remaining three parameters are unused.
	 */
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INOUT, TEEC_VALUE_INOUT,
					 TEEC_NONE, TEEC_NONE);

	op.params[0].value.a = nw_time0 - UTC_NTP;
	op.params[1].value.a = nw_time1;
	res = TEEC_InvokeCommand(&sess, TA_SYNC_START, &op,
				 &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

	nonce = op.params[0].value.a;

	sleep(3);

	fd = open_connect(server);
	request(fd);

	get_reply(fd);
	
	/* SYNC SERVER */
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_WHOLE, TEEC_NONE,
					 TEEC_NONE, TEEC_NONE);

	TEEC_SharedMemory buf_shm = {
		.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT,
		.buffer = NULL,
		.size = 148 /* put size of the buffer you need */
	};
	res = TEEC_AllocateSharedMemory (sess.ctx, &buf_shm);
	if (res != TEEC_SUCCESS)
	      return 1;
	
	op.params[0].memref.parent = &buf_shm;
	
	t2_time0 -= UTC_NTP;
	t3_time0 -= UTC_NTP;

	memcpy(ptr, &t2_time0, 4);
	ptr++;
	memcpy(ptr, &t2_time1, 4);
	ptr++;
	memcpy(ptr, &t3_time0, 4);
	ptr++;
	memcpy(ptr, &t3_time1, 4);
	ptr++;
	memcpy(ptr, sig_buf, 128);
	ptr += 32;
	memcpy(ptr, &nonce, 4);

	memcpy(buf_shm.buffer, packet, 148);

	/* set our nw system time too */
	now.tv_sec = t3_time0;
	now.tv_usec= t3_time1;
	settimeofday(&now, NULL);

	res = TEEC_InvokeCommand(&sess, TA_SYNC_SERVER, &op,
				 &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

	close(fd);

	return 0;
}
char *not_cert;
char *photo;

int sign_fb(TEEC_Session sess, TEEC_Operation op, uint32_t err_origin, TEEC_Context ctx)
{
	TEEC_Result res;
        int fp = 0;
	char *ret_buf;
	uint32_t t, tm, r, rm;
	char is_photo;
	char fname[8];
	unsigned int fb_size = 8294400;
	int ret_buf_size = 145 + 128 + fb_size + 128;

	if (!ret_buf) {
		ret_buf = (char*)malloc(ret_buf_size);
	}
	if (!not_cert) {
		not_cert = calloc(1, 128);
	}
	memcpy (ret_buf, not_cert, 128); /* pass the not_cert as the shared memory */

	if (!photo) {
		photo = malloc(fb_size);
	}

	TEEC_SharedMemory buf_shm = {
		.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT,
		.buffer = (void*)ret_buf,
		.size = ret_buf_size /* size of the buffer */
	};

	res = TEEC_RegisterSharedMemory(&ctx, &buf_shm);
	if (res != TEEC_SUCCESS)
		printf("TEEC_RegisterSharedMemory failed\n");

	
	op.params[0].memref.parent = &buf_shm;
	op.params[0].memref.size = buf_shm.size;
	op.params[0].memref.offset = 0;

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_WHOLE,
			                 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);

	res = TEEC_InvokeCommand(&sess, TA_SIGN_FB, &op,
				 &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

	/* Get back the return data from OPTEE */
	memcpy(photo, ret_buf, fb_size);
	ret_buf += fb_size;
	sprintf (fname, "screen");	
	fp = open(fname, O_CREAT | O_WRONLY, 0600);
	if (!fp) {
		printf("File open failed\n");
		return 1;
	}
	write(fp, photo, fb_size);
	close(fp);

	sprintf (fname, "sgn");	
	fp = open(fname, O_CREAT | O_WRONLY, 0600);
	if (!fp) {
		printf("File open failed\n");
		return 1;
	}
	write(fp, ret_buf, 128);
	close(fp);
	ret_buf += 128;

	t = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	tm = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	r = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	rm = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	is_photo = *(uint32_t*)(ret_buf);
	ret_buf += 1;

	not_cert = memcpy(not_cert, ret_buf, 128);
		
	TEEC_ReleaseSharedMemory(&buf_shm);
	return 0;
}

int sign_cam(TEEC_Session sess, TEEC_Operation op, uint32_t err_origin, TEEC_Context ctx)
{
	TEEC_Result res;
        int fp = 0;
	char *ret_buf;
	uint32_t t, tm, r, rm;
	char is_photo;
	char fname[6];
	unsigned int photo_size = 320 * 240 * 2;

	int ret_buf_size = 145 + 128 + photo_size;
	char *photo;

	if (!ret_buf) {
		ret_buf = (char*)malloc(ret_buf_size);
	}

	if (!photo) {
		photo = malloc(photo_size);
	}

	TEEC_SharedMemory buf_shm = {
		.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT,
		.buffer = (void*)ret_buf,
		.size = ret_buf_size /* size of the buffer */
	};

	res = TEEC_RegisterSharedMemory(&ctx, &buf_shm);
	if (res != TEEC_SUCCESS)
		printf("TEEC_RegisterSharedMemory failed\n");

	
	op.params[0].memref.parent = &buf_shm;
	op.params[0].memref.size = buf_shm.size;
	op.params[0].memref.offset = 0;

	op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_WHOLE,
			                 TEEC_NONE,
					 TEEC_NONE,
					 TEEC_NONE);

	res = TEEC_InvokeCommand(&sess, TA_SIGN_CAM, &op,
				 &err_origin);
	if (res != TEEC_SUCCESS)
		errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

	/* Get back the return data */
	memcpy(photo, ret_buf, photo_size);
	ret_buf += photo_size;
	sprintf (fname, "photo");
	
	fp = open(fname, O_CREAT | O_WRONLY, 0600);
	if (!fp) {
		printf("File open failed\n");
		return 1;
	}
	write(fp, photo, photo_size);
	close(fp);

	/* signature */
	sprintf (fname, "sgn");
	fp = open(fname, O_CREAT | O_WRONLY, 0600);
	if (!fp) {
		printf("File open failed\n");
		return 1;
	}
	write(fp, ret_buf, 128);
	close(fp);
	ret_buf += 128;

	t = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	tm = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	r = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	rm = *(uint32_t*)(ret_buf);
	ret_buf += 4;

	is_photo = *(unsigned char*)(ret_buf);
	ret_buf += 1;
	
	TEEC_ReleaseSharedMemory(&buf_shm);
	return 0;
}

int main(int argc, char **argv)
{
	TEEC_Result res;
	TEEC_Context ctx;
	TEEC_Session sess;
	TEEC_Operation op;
	TEEC_UUID uuid = STATS_UUID;
	uint32_t err_origin;
	char *argg;

	/* Clear the TEEC_Operation struct */
	memset(&op, 0, sizeof(op));

	argg = argv[1];

	if(argg[0] == '1') {

		printf("sign_fb\n");
		/* Initialize a context connecting us to the TEE */
		res = TEEC_InitializeContext(NULL, &ctx);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

		res = TEEC_OpenSession(&ctx, &sess, &uuid,
				       TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);

		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
				res, err_origin);
	
		sign_fb(sess, op, err_origin, ctx);

		TEEC_CloseSession(&sess);
		TEEC_FinalizeContext(&ctx);
	}
	else if (argg[0] == '2') {
		printf("time_synch\n");
		/* Initialize a context connecting us to the TEE */
		res = TEEC_InitializeContext(NULL, &ctx);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

		res = TEEC_OpenSession(&ctx, &sess, &uuid,
				       TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);

		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
				res, err_origin);
	
		/* time synchronization */
		client("128.195.55.158", sess, op, err_origin); /* Set this to the Time server */

		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InvokeCommand failed with code 0x%x origin 0x%x",
			res, err_origin);

		TEEC_CloseSession(&sess);
		TEEC_FinalizeContext(&ctx);
	
	}
	else if(argg[0] == '3') {
		printf("sign_cam\n");
		/* Initialize a context connecting us to the TEE */
		res = TEEC_InitializeContext(NULL, &ctx);
		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

		res = TEEC_OpenSession(&ctx, &sess, &uuid,
				       TEEC_LOGIN_PUBLIC, NULL, NULL, &err_origin);

		if (res != TEEC_SUCCESS)
			errx(1, "TEEC_Opensession failed with code 0x%x origin 0x%x",
				res, err_origin);
	
		sign_cam(sess, op, err_origin, ctx);

		TEEC_CloseSession(&sess);
		TEEC_FinalizeContext(&ctx);
	}
	else if (argg[0] == '4') {
		/* freeze syscall */
	        syscall(292, 0);
	}
	else if (argg[0] == '5') {
		/* unfreeze syscall */
	        syscall(293, 0);
	}

	else
	{
		printf("argument is invalid\n");
	}

	return 0;
}

