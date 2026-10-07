/* 字体分页与书架隔离。/ Font pages isolated from book shelves. */
#define _POSIX_C_SOURCE 200809L
#include "pn_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){char root[]="/tmp/pn-font-list-XXXXXX";assert(mkdtemp(root));char path[512],first[512];
 for(unsigned i=0;i<9;i++){snprintf(path,sizeof path,"%s/font-%03u.%s",root,i,i==8?"TTF":"ttf");FILE *f=fopen(path,"wb");assert(f && fputs("not-a-font-yet",f)>0 && !fclose(f));if(!i)strcpy(first,path);}
 snprintf(path,sizeof path,"%s/book.txt",root);FILE *f=fopen(path,"wb");assert(f && fputs("book",f)>0 && !fclose(f));char link[512];snprintf(link,sizeof link,"%s/link.ttf",root);assert(!symlink(first,link));
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_catalog_page_t *page=malloc(sizeof *page);assert(page);
 assert(pn_catalog_font_page(&media,&lease,root,"",page)==PN_OK && page->count==6 && page->more && page->items[0].format==PN_FILE_TTF && !strcmp(page->items[0].name,"font-000.ttf"));
 char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,page->items[5].name);assert(pn_catalog_font_page(&media,&lease,root,cursor,page)==PN_OK && page->count==3 && !page->more && !strcmp(page->items[0].name,"font-006.ttf"));
 strcpy(cursor,page->items[0].name);assert(pn_catalog_font_page_before(&media,&lease,root,cursor,page)==PN_OK && page->count==6 && page->more && !strcmp(page->items[0].name,"font-000.ttf"));
 assert(pn_catalog_page(&media,&lease,root,"",page)==PN_OK && page->count==1 && !strcmp(page->items[0].name,"book.txt"));
 assert(pn_media_detach(&media)==PN_OK && pn_catalog_font_page(&media,&lease,root,"",page)==PN_STALE_MEDIA);assert(pn_media_release(&media,&lease)==PN_OK);free(page);
 for(unsigned i=0;i<9;i++){snprintf(path,sizeof path,"%s/font-%03u.%s",root,i,i==8?"TTF":"ttf");assert(!unlink(path));}snprintf(path,sizeof path,"%s/book.txt",root);assert(!unlink(path) && !unlink(link) && !rmdir(root));return 0;}
