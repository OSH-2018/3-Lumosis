# 作业提交  

题目：文件系统  

日期：2018/5/19  

状态：已完成（可以评测）  


## 文件系统说明  

* 本文件系统由64*1024个block构成,每个block都在mem数组中有对应的元素，即:block-->mem[blockID]  
* 第一个block大小为 17*65336 Bytes，用来存放root指针、分配块时所需的队列以及该队列中的节点　　
* 其余block大小为 65336 Bytes，用来存放文件  
* 每个文件结构如下：  
*文件所占用的第一个块：*  
--------------文件信息-----------------  
--------------文件信息-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件信息-----------------  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件内容-----------------  
------下一个块的块号（blockID）--------  
*文件所占用的最后一个块：*  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件内容-----------------  
----------结束标志（-1）------------  
*文件所占用的其它块：*  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------文件内容-----------------  
--------------........-----------------  
--------------........-----------------  
--------------文件内容-----------------  
------下一个块的块号（blockID）--------  
  
* 文件系统运行过程中涉及到分配块和释放块的操作使用一个队列来实现，该队列的每个节点的data部分储存的是空闲块的blockID,该队列及队列节点储存在mem[0]中 
* 该文件系统最高性能可达330+m/s
