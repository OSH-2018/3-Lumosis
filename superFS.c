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

#define blocksize 65536
static const size_t size = 4 * 1024 * 1024 * (size_t)1024;
static void *mem[64 * 1024];

struct filenode {
    char *filename;
    void *content;
    int start_block; //start_block为该文件存储起始块的块号
    int last_block;//最后一个块号
    char *rear_addr;
    struct stat *st;
    struct filenode *next;
};

typedef struct node *PNode;  

typedef struct node{
    int blocknum;//blockID
    PNode next;
}Node;

typedef struct{
    PNode front;//队首
    PNode rear;//队尾
    int size;
}Queue;

static struct filenode *root = NULL;
Queue *block_queue;

Queue *InitQueue(){
    off_t offset=sizeof(struct filenode *);
    Queue *pqueue=(char *)mem[0]+offset;
    if(pqueue!=NULL){
        pqueue->front=NULL;
        pqueue->rear=NULL;
        pqueue->size=0;
    }
    return pqueue;
}

int IsEmpty(Queue *pqueue)  {  
    if(pqueue->front==NULL&&pqueue->rear==NULL&&pqueue->size==0)  
        return 1;  
    else  
        return 0;  
}  

PNode EnQueue(Queue *pqueue,int blocknum){
    off_t offset=sizeof(struct filenode *)+sizeof(Queue);
    PNode pnode=(char *)mem[0]+offset+(blocknum-1)*sizeof(Node);//找到blocknum对应的队列节点，将其插入队尾
    pnode->next=NULL;
    if(IsEmpty(pqueue)){
        pqueue->front=pnode;
    }
    else{
        pqueue->rear->next=pnode;
    }
    pqueue->rear=pnode;
    pqueue->size++;
    return pnode;
}

PNode DeQueue(Queue *pqueue,int *blocknum){
    PNode pnode=pqueue->front;
    if(IsEmpty(pqueue)!=1&&pnode!=NULL){
        if(blocknum!=NULL)
            *blocknum=pnode->blocknum;
        pqueue->size--;
        pqueue->front=pnode->next;
        if(pqueue->size==0)
            pqueue->rear=NULL;
    }
    return pqueue->front;
}


static struct filenode *get_filenode(const char *name){
    struct filenode *node = root;
    while(node) {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;
    }
    return NULL;
}

int my_malloc(){//分配一个块的内存，并返回块号。如果内存已满，则返回-1
    int n;
    if(DeQueue(block_queue,&n)!=NULL){
        printf("分配块block[%d]\n",n);
        mem[n]=mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    else
        return -1;
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
    EnQueue(block_queue,freeing_block);
    freeing_block=next;
    }
}

static void create_filenode(const char *filename, const struct stat *st){   

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

    printf("offset=%d\n", offset);
    new->rear_addr=(char *)mem[n]+offset;
    new->last_block=new->start_block=n;
    new->next = root;
    new->content =(char *)mem[n]+offset;
    new->st->st_blocks++;
    root = new;
    memcpy(mem[0],&root,sizeof(struct filenode *));
    int *next_block;
    next_block=(char *)mem[new->start_block]+blocksize-sizeof(int);
    *next_block=-1;//该块最末端sizeof(int)个字节存入-1，代表该块所存文件到此块截止。
    printf("文件节点创建完成\n");
}

static void *oshfs_init(struct fuse_conn_info *conn){   
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

    //mem[0]用来储存root指针
    mem[0]=mmap(NULL, (sizeof(Node)+1)*blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(mem[0],&root,sizeof(struct filenode *));//root指针动态储存，每改变一次就储存一次
    //初始化空队列
    block_queue=InitQueue();
    //建立队列
    off_t offset;
    offset=sizeof(struct filenode *)+sizeof(Queue);
    for(int i=1;i<blocknr;i++){
        PNode noding;
        noding=(char *)mem[0]+offset;
        noding->blocknum=i;
        noding->next=(char *)noding+sizeof(Node);
        offset+=sizeof(Node);
    }
    PNode noding=(char *)mem[0]+offset-sizeof(Node);
    noding->next=NULL;
    block_queue->front=(char *)mem[0]+sizeof(struct filenode *)+sizeof(Queue);
    block_queue->rear=(char *)mem[0]+offset-sizeof(Node);
    block_queue->size=blocknr-1;
    printf("init调用完成\n");
    return NULL;

}

static int oshfs_getattr(const char *path, struct stat *stbuf){  
    printf("调用getattr\n");
    printf("查询的文件是%s\n",path);
    int ret = 0;
    struct filenode *node = get_filenode(path);
    if(strcmp(path, "/") == 0) {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;
    } else if(node) {
        memcpy(stbuf, node->st, sizeof(struct stat));
        printf("blocksize=%d\nblocks=%d\n",stbuf->st_blksize,stbuf->st_blocks);
    } else {
        printf("没找到\n");
        ret = -ENOENT;
    }
    printf("getattr调用完成\n");
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){   
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

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev){   
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
    st.st_blksize = (size_t)0;
    st.st_blocks = 0;
    st.st_dev = dev;
    create_filenode(path + 1, &st);
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi){
    return 0;
}

static int oshfs_truncate(const char *path, off_t size){//将文件大小修改为size，可能涉及到申请块或释放。注意改变rear_addr和last_block
    printf("调用truncate\n");
    struct filenode *node = get_filenode(path);
    //现有多少块
    int block_exist=(node->st->st_size+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)-1)/(blocksize-sizeof(int))+1;
    //需要多少块
    int block_demand=(size+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)-1)/(blocksize-sizeof(int))+1;
    int block_diff=block_demand-block_exist;
    printf("sizeof(filenode)=%d\nstrlen(filename)=%d\nsizeof(stat)=%d\n",sizeof(struct filenode),strlen(node->filename),sizeof(struct stat));
    printf("exist=%d\ndemand=%d\n",block_exist,block_demand);
    printf("size=%d\n",size);
    if(block_diff>0){
        int *next_block=&node->last_block;
        for(int i=0;i<block_diff;i++){
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            *next_block=my_malloc();
            printf("malloc_block=%d\n",*next_block);
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
        }
        node->st->st_blocks=block_demand;
        //改变last_block
        if(((size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)\
        +1+sizeof(struct stat))))%(blocksize-sizeof(int)))==0){
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            *next_block=my_malloc();
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
            node->st->st_blocks++;
        }//多分配一块
        node->last_block=*next_block;        
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        *next_block=-1;
        //改变rear_addr
        node->rear_addr=(char *)mem[node->last_block]+(size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))))%(blocksize-sizeof(int));
    }
    else if(block_diff<0){
        int *next_block=&node->start_block;
        for(int i=0;i<block_demand-1;i++){
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        }
        node->st->st_blocks=block_demand;
        if((size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)))>=0)\
        &&((size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)\
        +1+sizeof(struct stat))))%(blocksize-sizeof(int)))==0){
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            node->st->st_blocks++;
        }
        node->last_block=*next_block;
        if(size>=blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))){
            node->rear_addr=(char *)mem[node->last_block]+(size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))))%(blocksize-sizeof(int));
        }
        else{
            node->rear_addr=(char *)mem[node->last_block]+(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))+size;
        }
        next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
        free_block(*next_block);
        next_block=(char *)mem[node->last_block]+blocksize-sizeof(int);
        *next_block=-1;
    }
    else{
        if((size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat)))>=0)\
        &&((size-(blocksize-sizeof(int)-(sizeof(struct filenode)+strlen(node->filename)\
        +1+sizeof(struct stat))))%(blocksize-sizeof(int)))==0){
            int *next_block=(char *)mem[node->last_block]+blocksize-sizeof(int);
            *next_block=my_malloc();
            if(*next_block==-1){
                printf("内存已满!\n");
                return -ENOSPC;
            }
            node->st->st_blocks++;
            node->last_block=*next_block;
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            *next_block=-1;
            node->rear_addr=(char *)mem[node->last_block]+(size-(blocksize-sizeof(int)-\
            (sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))))%(blocksize-sizeof(int));
        }
        else{
            if(node->last_block==node->start_block){
                node->rear_addr=(char *)mem[node->last_block]+(sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))+size;
            }
            else{
                node->rear_addr=(char *)mem[node->last_block]+(size-(blocksize-sizeof(int)-\
                (sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat))))%(blocksize-sizeof(int));
            }
        }
    }
    node->st->st_size = size;
    printf("truncate调用完成\n");
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){   
    int *next_block;
    printf("调用write\n");
    struct filenode *node = get_filenode(path);
    char *rear_addr;
    int last_block;
    int flag=0;//当offset==node->st->st_size时，flag设置为1,此时使用算法1（可大幅提高效率）
    if(offset==node->st->st_size){
        flag=1;
        rear_addr=node->rear_addr;
        last_block=node->last_block;
    }
    if(offset + size > node->st->st_size){
        if(oshfs_truncate(path,offset+size)==-ENOSPC){
            return -ENOSPC;
        }
    }

    printf("st_size=%d\n", node->st->st_size );
    if(flag==1){
        printf("flag=1\n");
        char *offset_addr=rear_addr;
        next_block=&last_block;
        size_t copying;
        size_t copied=0;
        //printf("node->last_block=%d\n",last_block);
        copying=(char *)mem[*next_block]+blocksize-sizeof(int)-rear_addr;
        printf("next_block=%d\nsize=%d\ncopying=%d\n",*next_block,size,copying);
        while(size>=copied+copying){
            memcpy(offset_addr,(char *)buf + copied,copying);
            copied+=copying;
            copying=blocksize-sizeof(int);
            next_block=(char *)mem[*next_block]+blocksize-sizeof(int);
            printf("next_block=%d\n",*next_block);
            offset_addr=(char *)mem[*next_block];
        }
        if(copied==0){
            memcpy(offset_addr,buf,size);
        }
        else{
            copying=size-copied;
            memcpy(offset_addr,(char *)buf+copied,copying);
        }//改写完成！
    }
    else{
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
    }
    node->content=(char *)mem[node->start_block]+sizeof(struct filenode)+strlen(node->filename)+1+sizeof(struct stat);
    printf("write调用完成！\n");
    return size;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
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

static int oshfs_unlink(const char *path){
    // Implemented
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
    memcpy(mem[0],&root,sizeof(struct filenode *));
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