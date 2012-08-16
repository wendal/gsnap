/*
 * File:    gsnap.c
 * Author:  Li XianJing <xianjimli@hotmail.com>
 * Brief:   snap the linux mobile device screen.
 *
 * Copyright (c) 2009  Li XianJing <xianjimli@hotmail.com>
 *
 * Licensed under the Academic Free License version 2.1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * History:
 * ================================================================
 * 2009-08-20 Li XianJing <xianjimli@hotmail.com> created
 * 2011-02-28 Li XianJing <xianjimli@hotmail.com> suppport RGB888 framebuffer.
 * 2011-04-09 Li XianJing <xianjimli@hotmail.com> merge figofuture's png output.
 * 	ref: http://blog.chinaunix.net/space.php?uid=15059847&do=blog&cuid=2040565
 *
 */

#include <png.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "jpeglib.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fb.h>
#include <linux/kd.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <sys/time.h>
#include <errno.h>

struct _FBInfo;
typedef struct _FBInfo FBInfo;
typedef int (*UnpackPixel) (FBInfo * fb, unsigned char *pixel, unsigned char *r, unsigned char *g, unsigned char *b);
int sockfd, newfd;

int _quality, _reSize;

char _needAdjustRGB = 0;
int _adjust_red, _adjust_green, _adjust_blue;
int _width;

struct _FBInfo {
	int fd;
	UnpackPixel unpack;
	unsigned char *bits;
	struct fb_fix_screeninfo fi;
	struct fb_var_screeninfo vi;
};

#define fb_width(fb)  ( _width?_width:(fb)->vi.xres)
#define fb_height(fb) ((fb)->vi.yres)
#define fb_bpp(fb)    ((fb)->vi.bits_per_pixel>>3)
#define fb_size(fb)   ((_width?_width:(fb)->vi.xres) * (fb)->vi.yres * fb_bpp(fb))

#define myprintf(a)  ;

static int fb_unpack_rgb565(FBInfo * fb, unsigned char *pixel, unsigned char *r, unsigned char *g, unsigned char *b)
{
	unsigned short color = *(unsigned short *)pixel;

	*r = ((color >> 11) & 0xff) << 3;
	*g = ((color >> 5) & 0xff) << 2;
	*b = (color & 0xff) << 3;

	return 0;
}

static int fb_unpack_rgb24(FBInfo * fb, unsigned char *pixel, unsigned char *r, unsigned char *g, unsigned char *b)
{
	*r = pixel[fb->vi.red.offset >> 3];
	*g = pixel[fb->vi.green.offset >> 3];
	*b = pixel[fb->vi.blue.offset >> 3];

	return 0;
}

static int fb_unpack_argb32(FBInfo * fb, unsigned char *pixel, unsigned char *r, unsigned char *g, unsigned char *b)
{
	*r = pixel[fb->vi.red.offset >> 3];
	*g = pixel[fb->vi.green.offset >> 3];
	*b = pixel[fb->vi.blue.offset >> 3];

	return 0;
}

static int fb_unpack_none(FBInfo * fb, unsigned char *pixel, unsigned char *r, unsigned char *g, unsigned char *b)
{
	*r = *g = *b = 0;

	return 0;
}

static void set_pixel_unpacker(FBInfo * fb)
{
	if (fb_bpp(fb) == 2) {
		fb->unpack = fb_unpack_rgb565;
	} else if (fb_bpp(fb) == 3) {
		fb->unpack = fb_unpack_rgb24;
	} else if (fb_bpp(fb) == 4) {
		fb->unpack = fb_unpack_argb32;
	} else {
		fb->unpack = fb_unpack_none;
		myprintf("%s: not supported format.\n");
	}

	return;
}

static int fb_open(FBInfo * fb, char *fbfilename)
{
	fb->fd = open(fbfilename, O_RDWR);

	if (fb->fd < 0) {
		myprintf("can't open device file");
		fprintf(stderr, "can't open %s\n", fbfilename);

		return -1;
	}

	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
		goto fail;

	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vi) < 0)
		goto fail;

	fb->bits = mmap(0, fb_size(fb), PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);

	if (fb->bits == MAP_FAILED)
		goto fail;

	myprintf("---------------framebuffer---------------\n");
	myprintf("%s: \n  width : %8d\n  height: %8d\n  bpp   : %8d\n  r(%2d, %2d)\n  g(%2d, %2d)\n  b(%2d, %2d)\n");
	myprintf("-----------------------------------------\n");

	set_pixel_unpacker(fb);

	return 0;

 fail:
	myprintf("%s is not a framebuffer.\n");
	close(fb->fd);

	return -1;
}

static void fb_close(FBInfo * fb)
{
	munmap(fb->bits, fb_size(fb));
	close(fb->fd);

	return;
}

static int snap2jpg(char *outdata, int quality, int resize, FBInfo * fb, int *nSize)
{
	int row_stride = 0;
	JSAMPROW row_pointer[1] = { 0 };
	struct jpeg_error_mgr jerr;
	struct jpeg_compress_struct cinfo;

	memset(&jerr, 0x00, sizeof(jerr));
	memset(&cinfo, 0x00, sizeof(cinfo));

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	jpeg_stdio_dest(&cinfo, outdata, nSize);
	cinfo.image_width = fb_width(fb) / resize;
	cinfo.image_height = fb_height(fb) / resize;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);

	cinfo.dct_method = JDCT_IFAST;
	cinfo.optimize_coding = FALSE;
	cinfo.do_fancy_downsampling = FALSE;

	jpeg_start_compress(&cinfo, TRUE);

	row_stride = fb_width(fb) * 2 / resize;
	JSAMPLE *image_buffer = malloc(3 * fb_width(fb) / resize);

	while (cinfo.next_scanline < cinfo.image_height) {
		int i = 0;
		int offset = 0;
		unsigned char *line = fb->bits + cinfo.next_scanline * fb_width(fb) * fb_bpp(fb) * resize;

		for (i = 0; i < fb_width(fb) / resize; i++, offset += 3, line += fb_bpp(fb) * resize) {
			fb->unpack(fb, line, image_buffer + offset, image_buffer + offset + 1, image_buffer + offset + 2);
		}

		row_pointer[0] = image_buffer;
		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);

	jpeg_destroy_compress(&cinfo);

	return 0;
}


void adjustRGB(FBInfo * fb)
{
	fb->vi.red.offset = _adjust_red;
	fb->vi.green.offset = _adjust_green;
	fb->vi.blue.offset = _adjust_blue;
}

/*  Read a line from a socket  */

ssize_t readline(int sockd, void *vptr, size_t maxlen)
{
	ssize_t n, rc;
	char c, *buffer;

	buffer = vptr;

	for (n = 1; n < maxlen; n++) {

		if ((rc = read(sockd, &c, 1)) == 1) {
			*buffer++ = c;
			if (c == '\n')
				break;
		} else if (rc == 0) {
			if (n == 1)
				return 0;
			else
				break;
		} else {
			if (errno == EINTR)
				continue;
			return -1;
		}
	}

	*buffer = 0;
	return n;
}

/*  Write msg to a socket  */

ssize_t sendmsgcomplete(int sockd, const void *vptr, size_t n)
{
	size_t nleft;
	ssize_t nwritten;
	const char *buffer;

	buffer = vptr;
	nleft = n;

	while (nleft > 0) {
		if ((nwritten = write(sockd, buffer, nleft)) <= 0) {
			if (errno == EINTR)
				nwritten = 0;
			else
				return -1;
		}

		nleft -= nwritten;
		buffer += nwritten;
	}

	return n;
}

ssize_t senddata(int sockd, const void *vptr, size_t n)
{
	size_t ret;
	char by[4];
	by[0] = (n & 0xFF000000) >> 24;
	by[1] = (n & 0x00FF0000) >> 16;
	by[2] = (n & 0x0000FF00) >> 8;
	by[3] = (n & 0x000000FF);
	ret = sendmsgcomplete(sockd, by, 4);
	if (ret == -1) {
		return -1;
	}
	ret = sendmsgcomplete(sockd, vptr, n);
	return ret;
}

void read_socket()
{
	int recv_num, sent_num, recv_num_total = 0;
	char recv_buf[50];
	char outdata[300000];
	int nSize = 0;
	char *sent_buf;

	FBInfo fb;
	char *filename = "/sdcard/test/1.jpg";
	char *fbfilename = "/dev/graphics/fb0";

	while (1) {
		myprintf("start while loop...\n");
		memset(recv_buf, 0, sizeof(recv_buf));	/*清空一下recv_buf缓存区 */

		recv_num = readline(newfd, recv_buf, 49);
		if (recv_num < 0) {
			myprintf("Call recv failed!\n");
			break;
		} else if (recv_num == 0) {
			close(newfd);
			break;
		} else {
			printf("recv command: %s\n", recv_buf);
			if (strncmp(recv_buf, "snap", 4) == 0) {	//只取前面4位
				float timeuse;
				struct timeval start, end;

				gettimeofday(&start, NULL);

				memset(&fb, 0x00, sizeof(fb));

				if (fb_open(&fb, fbfilename) == 0) {
					if (_needAdjustRGB) {
						adjustRGB(&fb);
					}

					myprintf("start snap...");
					snap2jpg(outdata, _quality, _reSize, &fb, &nSize);
					fb_close(&fb);
				}
				//gettimeofday( &end, NULL );
				//timeuse = 1000000 * ( end.tv_sec - start.tv_sec ) + end.tv_usec - start.tv_usec; 
				//timeuse /= 1000000;
				//printf("-----Time Use: %f -----\n", timeuse);


				if (senddata(newfd, outdata, nSize) < 0) {
					printf("Send File is Failed\n");
					break;
				}
			} else if (recv_buf[0] == 'r') {	//缩小率变化
				_reSize = atoi((char *)recv_buf + 1);	//往前移一位
				printf("size changed: \"%d\"\n", _reSize);
			} else if (recv_buf[0] == 'q') {	//图像质量变化
				_quality = atoi((char *)recv_buf + 1);	//往前移一位
				printf("img quality changed: \"%d\"\n", _quality);
			} else if (strncmp(recv_buf, "kill", 4) == 0) {
				printf("Close capture agent by command.\n");
				close(newfd);
				exit(5);
			} else if (strncmp(recv_buf, "info", 4) == 0) {
				printf("Get framebuffer info from the client.\n");
				memset(&fb, 0x00, sizeof(fb));
				if (fb_open(&fb, fbfilename) == 0) {
					char info[50] = { 0 };
					sprintf(info, "%d,%d,%d,%d,%d", fb_width(&fb), fb_height(&fb), (&fb)->vi.red.offset, (&fb)->vi.green.offset, (&fb)->vi.blue.offset);
					senddata(newfd, info, strlen(info));
					fb_close(&fb);
				}
			} else {
				printf("receive invalid commands: \"%s\". Get %d bytes this time.\n", recv_buf, recv_num);
				senddata(newfd, "finish", 6);
			}
		}
	}
}

void beforeclose()
{
	printf("at exist");
}

int main(int argc, char *argv[])
{

	int ret;

	atexit(beforeclose);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;	/*设置域为IPV4 */
	server_addr.sin_addr.s_addr = INADDR_ANY;	/*绑定到 INADDR_ANY 地址 */
	server_addr.sin_port = htons(5678);	/*通信端口号为5678，注意这里必须要用htons函数处理一下，不能直接写5678，否则可能会连不上 */

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("server");
		printf("opend socket failed!\n");
		exit(1);
	}
	ret = bind(sockfd, (struct sockaddr *)(&server_addr), sizeof(server_addr));
	if (ret < 0) {
		perror("server");
		printf("server bind to the address failed!\n");
		exit(2);
	}
	ret = listen(sockfd, 4);
	if (ret < 0) {
		perror("server");
		printf("listen to the socket failed!\n");
		exit(3);
	}

        if (argc > 3)
	    _quality = atoi(argv[3]);
        else
            _quality = 80;
	if (argc > 4)
            _reSize = atoi(argv[4]);
        else
            _reSize = 1;

	if (argc > 6) {
		_needAdjustRGB = 1;
		_adjust_red = atoi(argv[5]);
		_adjust_green = atoi(argv[6]);
		_adjust_blue = atoi(argv[7]);
	}

	if (argc > 7) {
		_width = atoi(argv[8]);
	}

	while (1) {
		newfd = accept(sockfd, NULL, NULL);	/*newfd连接到调用connect的客户端 */
		if (newfd < 0) {
			if (errno == EINTR) {
				continue;
			}

			perror("server");
			printf("accept connection failed!\n");
			exit(4);
		}

		printf("Successfully connect to client!\n");

		read_socket();
	}

	return 0;
}
