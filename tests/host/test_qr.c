/* 中文：载荷转义、参数拒绝和真实矩阵捕获。/ English: payload escaping, argument rejection and actual matrix captures. */
#include "pn_qr.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int argc,char **argv){
 char payload[256];assert(pn_qr_wifi_payload("小纸;Pico:\\热点","ABCD:EFG;1234",payload,sizeof payload)==PN_OK);
 assert(!strcmp(payload,"WIFI:T:WPA;S:小纸\\;Pico\\:\\\\热点;P:ABCD\\:EFG\\;1234;H:false;;"));
 char short_out[8]="keep";assert(pn_qr_wifi_payload("Pico","12345678",short_out,sizeof short_out)==PN_LIMIT && !strcmp(short_out,"keep"));
 assert(pn_qr_wifi_payload("Pico","short",payload,sizeof payload)==PN_INVALID);
 uint8_t *pixels=calloc(1,342u*1216u);assert(pixels);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,342u*1216u,684,1216));pn_frame_clear(&frame,15);
 assert(pn_qr_draw("http://192.168.4.1",&frame,32,232,280)==PN_OK);
 assert(pn_qr_wifi_payload("小纸 Pico-1234","ABCDEFGH2345",payload,sizeof payload)==PN_OK);assert(pn_qr_draw(payload,&frame,364,232,280)==PN_OK);
 assert(pn_qr_draw(payload,&frame,-1,0,280)==PN_INVALID && pn_qr_draw(payload,&frame,0,0,12)==PN_LIMIT);
 if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&frame,x,y)*17,f);assert(!fclose(f));}
 free(pixels);return 0;
}
