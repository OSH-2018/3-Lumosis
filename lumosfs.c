#define FUSE_USE_VERSION 26
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>

#define blocksize 65536

struct filenode {
    char *filename;
    void *content;
    int start_block; //start_block为该文件存储起始块的块号
    struct stat *st;
    struct filenode *next;
};

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[64 * 1024];

static struct filenode *root = NULL;

static struct filenode *get_filenode(const char *name)
{
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

int get_free_block(){//返回一个空块的块号。如果内存已满，则返回-1
    static int i=0;
    static size_t blocknr=sizeof(mem) / sizeof(mem[0]);
    int count;
    for(count=0,i=(i+1)%blocknr; count<blocknr; i=(i+1)%blocknr, count++ ){
        if(mem[i]==NULL)
            return i;
    }
    return -1;
}

int my_malloc(){//分配一个块的内存，并返回块号。如果内存已满，则返回-1
    int n;
    n=get_free_block();
    if(n!=-1){
        printf("分配块block[%d]\n",n);
        mem[n]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    return n;

}

void free_block(int freeing_block){//释放掉块n，及之后的块。
    int *next_block;
    int next;
    while(freeing_block!=-1){
    printf("释放块block[%d]\n",freeing_block);
    next_block=(char *)mem[freeing_block]+blocksize-sizeof(int);
    next=*next_block;
    munmap(mem[freeing_block], blocksize);
    mem[freeing_block]=NULL;
    freeing_block=next;
    }

}

static void create_filenode(const char *filename, const struct stat *st)
{   

    printf("调用create_filenode\n");
    size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    int n;
    size_t offset=0;
    n=my_malloc();
    if(n==-1){
        printf("内存已满!\n");
        return -ENOSPC;
    }
    printf("文件%s的第一块为block[%d]\n",filename,n);
    struct filenode *new = (struct filenode *)mem[n];
    offset+=sizeof(struct filenode);

    new->filename = (char *)mem[n]+offset;
    offset+=strlen(filename)+1;
    memcpy(new->filename, filename, strlen(filename) + 1);

    new->st = (char *)mem[n]+offset;
    offset+=sizeof(struct stat);
    memcpy(new->st, st, sizeof(struct stat));
    
    new->start_block=n;
    new->next = root;
    new->content = NULL;
    root = new;
    int *next_block;
    next_block=(char *)mem[new->start_block]+blocksize-sizeof(int);
    *next_block=-1;//该块最末端sizeof(int)个字节存入-1，代表该块所存文件到此块截止。
    printf("文件节点创建完成\n");
}

static void *oshfs_init(struct fuse_conn_info *conn)
{   
    printf("调用init\n");
    size_t blocknr = sizeof(mem) / sizeof(mem[0]);
    // Demo 1
    for(int i = 0; i < blocknr; i++) {
        mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++) {
        munmap(mem[i], blocksize);
    }
    //将mem数组所有指针初始化指向null
    for(int i=0;i<blocknr;i++){
        mem[i]=NULL;
    }

    //mem[0]用来储存root指针
    mem[0]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(mem[0],&root,sizeof(struct filenode *));
    printf("init调用完成\n");
    return NULL;

}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{   printf("调用getattr\n");
    printf("查询的文件是%s\n",path);
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
    } else {
        printf("没找到\n");
        ret = -ENOENT;
    }
    printf("getattr调用完成\n");
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
    printf("readdir调用完成\n");
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{   
    struct stat st;
    time_t now;
    struct tm *timenow;
    time(&now);
    st.st_mode = S_IFREG | 0644;
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;
    st.st_size = 0;
    st.st_atime = now;
    st.st_ctime = now;
    st.st_mtime = now;
    st.st_blksize = blocksize;
    st.st_blocks = 0;
    st.st_dev = dev;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int oshfs_truncate(const char *path, off_t size)//将文件大小修改为size，可能涉及到申请块或释放块
{
    printf("调用truncate\n");
    struct filenode *node = get_filenode(path);
    int *next_block;
    next_block=&node->start_block;
    off_t sizing=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);

    while(sizing<size&&*next_block!=-1){//寻找第size字节所在的块。该循环退出，当且仅当找到size对应的块或者到了最后一块
        sizing+=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
    }

    if(*next_block!=-1){
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        free_block(*next_block);
        *next_block=-1;
    }

    else{
        while(sizing<size){
            *next_block=my_malloc();
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
            sizing+=blocksize-sizeof(int);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        }
        *next_block=my_malloc();
        if(*next_block==-1){
            printf("内存已满!\n");
            return -ENOSPC;
        }
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        *next_block=-1;
    }
    
    node->st->st_size = size;
    printf("truncate调用完成\n");
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{   
    int *next_block;
    printf("调用write\n");
    struct filenode *node = get_filenode(path);
    if(offset + size > node->st->st_size)
        oshfs_truncate(path,offset+size);

    printf("node->st->st_size=%d\n",node->st->st_size);

    char *offset_addr;
    off_t offsetting=blocksize-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))-sizeof(int);
    next_block=&(node->start_block);
    //确定offset所对应的地址
    while(offsetting<=offset){
        offsetting+=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
    }
    if(next_block==&(node->start_block)){
        offset_addr=(char *)mem[*next_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)+offset;
    }
    else{
        offset_addr=(char *)mem[*next_block]+offset-(offsetting-blocksize+sizeof(int));
    }//offset对应地址找到！
        
    
    printf("找到对应地址\n");
    //开始改写
    size_t copying;
    size_t copied=0;
    copying=(char *)mem[*next_block]+blocksize-sizeof(int)-offset_addr;
    while(size>=copied+copying){
        memcpy(offset_addr,(char *)buf + copied,copying);
        copied+=copying;
        copying=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        offset_addr=(char *)mem[*next_block];
     }
    if(copied==0){
        memcpy(offset_addr,buf,size);
    }
    else{
        copying=size-copied;
        memcpy(offset_addr,(char *)buf+copied,copying);
    }//改写完成！
    printf("write调用完成！\n");
    return size;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("调用read\n");
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
    while(offsetting<=offset){
        offsetting+=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
    }
    if(next_block==&(node->start_block)){
        offset_addr=(char *)mem[*next_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)+offset;
    }
    else{
        offset_addr=(char *)mem[*next_block]+offset-(offsetting-blocksize+sizeof(int));
    }//offset对应地址找到！

    size_t copying;
    size_t copied=0;
    copying=(char *)mem[*next_block]+blocksize-sizeof(int)-offset_addr;
    while(ret>=copied+copying){
        memcpy((char *)buf+copied,offset_addr,copying);
        copied+=copying;
        copying=blocksize-sizeof(int);
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        if(*next_block==-1)
            return ret;
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
    printf("文件链表已处理完毕\n");


    /*
    if(node->content==NULL){
        printf("node->content=NULL\n");
    }
    else
        printf("%s\n",node->content);
    */
    free_block(node->start_block);
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