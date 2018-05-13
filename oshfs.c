#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdio.h>

struct filenode {
    char *filename;
    void *content;
    int start_block;//存储的第一个块的块号
    struct stat *st;
    struct filenode *next;
};

/*
每个文件
第一个block的开始处，存放文件信息
每个block最后sizeof(int)字节处存放下一个block的块号，凭借此号进行索引
若无下一个块，则此处存-1
*/

int filenode_block=0;

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[64 * 1024];

static struct filenode *root = NULL;

int get_free_block(){
    int i;
    size_t block_nr=sizeof(mem) / sizeof(mem[0]);
    for(i=0;i<block_nr;i++){
        if(mem[i]==NULL)
            return i;
    }
    if(i==block_nr)
        return -1;
}

static struct filenode *get_filenode(const char *name)
{   
    printf("调用get_filenode\n");
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else{
            printf("get_filenode调用完成\n");
            return node;
        }
    }
    printf("get_filenode调用完成\n");
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)
{   

    printf("调用create_filenode\n");
    size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    size_t blocksize = size / blocknr;
    printf("开始创建文件节点\n");

    int n;
    size_t offset=0;
    n=get_free_block();
    printf("文件%s的第一块为block[%d]\n",filename,n);
    if(n!=-1){
        mem[n]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    else{
        printf("内存已满!\n");
        return -ENOSPC;
    }
    
    struct filenode *new = (struct filenode *)mem[n];
    offset+=sizeof(struct filenode);

    new->filename = (char *)mem[n]+offset;
    offset+=strlen(filename)+1;
    memcpy(new->filename, filename, strlen(filename) + 1);

    new->st = (struct stat *)mem[n]+offset;
    offset+=sizeof(struct stat);
    memcpy(new->st, st, sizeof(struct stat));


    
    new->start_block=n;
    new->next = root;
    new->content = NULL;
    root = new;
    int *next_block;
    next_block=(char *)mem[new->start_block]+blocksize-sizeof(int);
    *next_block=-1;
    printf("%s \n",new->filename);
}

static void *oshfs_init(struct fuse_conn_info *conn)
{   
    printf("调用init\n");
    size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    size_t blocksize = size / blocknr;
    // Demo 1
    for(int i = 0; i < blocknr; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    // Demo 2
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < blocknr; i++) {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    for(int i=0;i<blocknr;i++){
        mem[i]=NULL;
    }
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{   
    printf("调用getattr\n");
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    } else {
        ret = -ENOENT;
    }
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{   
    printf("调用readdir\n");
    struct filenode *node = root;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node) {
        filler(buf, node->filename, node->st, 0);
        node = node->next;
    }
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{   
    printf("调用mknod\n");
    struct stat st;
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{   
    printf("调用open\n");
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{   
    printf("调用write\n");
    size_t blocksize = (size_t)65536;
    int *next_block;
    printf("开始写入文件，文件名：%s \nsize:%d  \noffset:%d\nbuf:%s \n",path,size,offset,buf);

    struct filenode *node = get_filenode(path);
    if(offset + size > node->st->st_size)
        node->st->st_size = offset + size;
    printf("node->st->st_size=%d\n",node->st->st_size);
    if(node->content==NULL){
        printf("文件在此之前无内容，现在写入内容\n");
        printf("文件的start block号码为： %d \n",node->start_block);

        node->content=(char *)mem[node->start_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat);
        printf("文件内容空间分配完成\n");
        if(size<=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int)){
            printf("content开始写入\n");
            memcpy(node->content,buf,size);
            printf("content写入完成\n");
            next_block=(char *)mem[node->start_block]+blocksize-sizeof(int);
            *next_block=-1;

        }
        else{
            size_t copying=0;
            size_t copied=0;
            //将数据copy入第一个block
            copying=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);
            memcpy(node->content,buf,copying);
            copied+=copying;
            next_block=(char *)mem[node->start_block]+blocksize-sizeof(int);
            *next_block=get_free_block();
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
            else
                mem[*next_block]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            //将数据copy入若干个block
            while(size-copied>blocksize-sizeof(int)){
                copying=blocksize-sizeof(int);
                memcpy(mem[*next_block],(char *)buf+copied,copying);
                copied+=copying;
                next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
                *next_block=get_free_block();
                if(*next_block==-1){
                    printf("内存已满!\n");
                    return -ENOSPC;
                }
                else
                    mem[*next_block]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
            //将数据copy入最后一个block
            copying=size-copied;
            memcpy(mem[*next_block],(char *)buf+copied,copying);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            *next_block=-1;//copy完成
        }
        
    }
    else{

        char *offset_addr;
        off_t offsetting=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);
        next_block=&(node->start_block);
        //确定offset所对应的地址
        printf("offsetting=%d \nstart_block:%d\n",offsetting,node->start_block);
        while(offset-offsetting>0){       
            offsetting+=blocksize-sizeof(int);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        }
        if(next_block==&(node->start_block)){
            offset_addr=(char *)mem[*next_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)+offset;
        }
        else{
            offset_addr=(char *)mem[*next_block]+offset-(offsetting-blocksize-sizeof(int));
        }//offset对应地址找到！
        //开始改写
        printf("找到对应地址\n");
        size_t copying;
        size_t copied=0;
        copying=(char *)mem[*next_block]+blocksize-sizeof(int)-offset_addr;
        printf("copying=%d \n",(size_t)copying);
        printf("%d\n",copied+copying);
        while(size>=copied+copying){
            memcpy(offset_addr,(char *)buf + copied,copying);
            copied+=copying;
            copying=blocksize-sizeof(int);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            if(*next_block==-1){
                *next_block=get_free_block();
                if(*next_block==-1){
                    printf("内存已满!\n");
                    return -ENOSPC;
                }
               else{
                    mem[*next_block]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                    int *one;
                    one=(char *)mem[*next_block]+blocksize-sizeof(int);
                    *one=-1;

                }
            }
            offset_addr=(char *)mem[*next_block];
        }
        if(copied==0){
            printf("开始改写\n");
            printf("offset_addr=%d \n size=%d",offset_addr,size);
            memcpy(offset_addr,buf,size);
        }
        else{
            copying=size-copied;
            memcpy(offset_addr,(char *)buf+copied,copying);
        }//改写完成！
    }

    printf("写入完成！\n");

    return size;
}

static int oshfs_truncate(const char *path, off_t size)
{   
    printf("调用truncate\n");
    size_t blocksize=(size_t)65536;
    struct filenode *node = get_filenode(path);
    int *next_block;
    next_block=&node->start_block;
    off_t sizing=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);

    while(sizing<size&&*next_block!=-1){
        sizing+=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
    }
    if(*next_block!=-1){
        int next;
        int freeing_block;
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        freeing_block=*next_block;

        while(freeing_block!=-1){
        next_block=(char *)mem[freeing_block]+blocksize-sizeof(int);
        next=*next_block;
        munmap(mem[freeing_block], blocksize);
        mem[freeing_block]=NULL;
        freeing_block=next;
        }
    }
    else{
        while(sizing<size){
            *next_block=get_free_block();
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
            else
                mem[*next_block]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            sizing+=blocksize-sizeof(int);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        }
        *next_block=get_free_block();
        if(*next_block==-1){
            printf("内存已满!\n");
            return -ENOSPC;
        }
        else
            mem[*next_block]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        *next_block=-1;

    }
    
    node->st->st_size = size;
    return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{   
    printf("调用read\n");
    size_t blocksize=(size_t)65536;
    int *next_block;
    struct filenode *node = get_filenode(path);
    int ret = size;
    printf("文件名：%s \nsize:%d \noffset:%d \n",path,size,offset);
    if(offset + size > node->st->st_size)
        ret = node->st->st_size - offset;

    char *offset_addr;
    off_t offsetting=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);
    next_block=&(node->start_block);
    //确定offset所对应的地址
    while(offset>offsetting){       
        offsetting+=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
    }
    if(next_block==&(node->start_block)){
        offset_addr=(char *)mem[*next_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)+offset;
    }
    else{
        offset_addr=(char *)mem[*next_block]+offset-(offsetting-blocksize-sizeof(int));
    }//offset对应地址找到！
    size_t copying;
    size_t copied=0;
    copying=(char *)mem[*next_block]+blocksize-sizeof(int)-offset_addr;
    while(ret>=copied+copying){
        memcpy((char *)buf+copied,offset_addr,copying);
        copied+=copying;
        copying=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        offset_addr=(char *)mem[*next_block];
    }
    memcpy((char *)buf+copied,offset_addr,ret-copied);
    return ret;
}

static int oshfs_unlink(const char *path)
{
    // Implementing
    //删除操作
    printf("调用unlink\n");
    printf("删除的是：%s \n",path);
    size_t blocksize=(size_t)65536;
    struct filenode *node = get_filenode(path);
    struct filenode *last =root;
    if(node==root){
        root=root->next;
    }
    else{
        while(last->next) {
            if(strcmp(last->next->filename, path + 1) != 0)
                last = last->next;
            else
                break;
        }
        last->next= last->next->next;
    }
    printf("周围节点已处理完毕\n");

    int *next_block;
    int next;
    int freeing_block= node->start_block;
    int i;


    if(node->content==NULL){
        printf("node->content=NULL\n");
    }
    else
        printf("%s\n",node->content);
    while(freeing_block!=-1){
        printf("freeing_block=%d\n",freeing_block);
        next_block=(char *)mem[freeing_block]+blocksize-sizeof(int);
        next=*next_block;
        munmap(mem[freeing_block], blocksize);
        mem[freeing_block]=NULL;
        freeing_block=next;
    }
    
    printf("unlink完成\n");
    return 0;
}

static const struct fuse_operations op = {
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);
}