/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

double curTotalPayload;
double maxTotalPayload;

void updateMax() {
    if (curTotalPayload>maxTotalPayload) {
        maxTotalPayload = curTotalPayload;
    }
}

void toggleHeader(sf_block *block) {
    block->header^=MAGIC;
}

sf_header magicHeader(sf_header head) {
    return head ^ MAGIC;
}

sf_header magicBlockHeader(sf_block *block) {
    return magicHeader(block->header);
}

sf_size_t getBlockSize(sf_block *block) {

    return magicHeader(block->header) & 0x00000000FFFFFFF0;
}

//get the previous block size of a block that is preceded by a free block and has pal set to 0
sf_size_t getPrevBlockSize(sf_block *block) {
    return magicHeader(block->prev_footer) & 0x00000000FFFFFFF0;
}

void setHeader(sf_block *block,sf_size_t payload_size,sf_size_t block_size,int al,int pal,int qklist) {
    toggleHeader(block);
    block->header = payload_size;
    block->header <<= 32;
    block->header += block_size | (al<<2) | (pal<<1) | qklist;
    toggleHeader(block);
}

void setBlockSize(sf_block *block,sf_size_t size) {
    toggleHeader(block);
    block->header = (block->header & ~(0x00000000FFFFFFF0)) | size;
    toggleHeader(block);
}

void setAlloc(sf_block *block,int alloc) {

}

void setPrvAlloc(sf_block *block,int pal) {
    toggleHeader(block);
    block->header = (block->header & ~(PREV_BLOCK_ALLOCATED)) | pal;
    toggleHeader(block);
}

sf_size_t getPayloadSize(sf_block *block) {
    return (sf_size_t) (magicBlockHeader(block) >> 32);
}

void setPayload(sf_block *block,sf_size_t payload) {
    toggleHeader(block);
    sf_header newP = payload;
    newP<<=32;
    block->header = (block->header & 0x00000000FFFFFFFF) | newP;
    toggleHeader(block);

}




//get the next chronological block by computing the block size and adding it to the address using pointer arithmetic
sf_block *getNext(sf_block *block) {
    //printf("yu%ld\n",(block->header & 0x00000000FFFFFFF0) /sizeof(sf_block));
    return (void*) block + getBlockSize(block);
}

//get the previous block of a block whose previous allocated is set
sf_block *getPrev(sf_block *block) {
    return (void*) block - getPrevBlockSize(block);
}

int getFreeListIndex(sf_size_t size) {
    int index = 0;
    sf_size_t curSize = 32;
    while (curSize<size) {
        index++;
        if (index==NUM_FREE_LISTS-1) {
            return index;
        }
        curSize*=2;
    }
    return index;
}


//always insert at front    
void insertFreeBlock(sf_block *newBlk) {
    int ind = getFreeListIndex(getBlockSize(newBlk));
    sf_block *sentinel = &sf_free_list_heads[ind];

    //prev of the first elemeent set to new block
    sentinel->body.links.next->body.links.prev = newBlk;
    //next of the new blk set to the previous first element
    newBlk->body.links.next=sentinel->body.links.next;
    //prev of the new blk set to the sentinel
    newBlk->body.links.prev=sentinel;
    //next of the sentinel set to the new block
    sentinel->body.links.next=newBlk;
    
}

int initializeHeap() {
    sf_block *sentinel;
    curTotalPayload = 0;
    maxTotalPayload = 0;
    // initialize the free list heads
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        sentinel = &sf_free_list_heads[i];
        // sentinel.prev_footer = NULL;
        // sentinel.header=NULL;
        sentinel->body.links.next = sentinel;
        sentinel->body.links.prev = sentinel;
    }


    for (int i = 0;i<NUM_QUICK_LISTS;i++) {
        sf_quick_lists[i].length = 0;
        sf_quick_lists[i].first = NULL;
    }

    // retrieve a page of data
    void *page = sf_mem_grow();
    if (page==NULL) {
        sf_errno = ENOMEM;
        return 0;
    }

    sf_block *prologue = page;

    sf_block *epilogue; // start location of the epilogue

    // set min block size and padding
    prologue->header = 32 | 4;

    toggleHeader(prologue);

    sf_block *first = getNext(prologue); // start of first free block

    first->header = (PAGE_SZ - (sizeof(*prologue) + sizeof(sf_header) + 8)) | PREV_BLOCK_ALLOCATED;

    toggleHeader(first);

    // sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = first;
    // sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = first;

    // first->body.links.next = &(sf_free_list_heads[NUM_FREE_LISTS - 1]);
    // first->body.links.prev = &(sf_free_list_heads[NUM_FREE_LISTS - 1]);

    insertFreeBlock(first);

    // create foote

    epilogue = getNext(first);
    epilogue->prev_footer = first->header;

    epilogue->header = 4;
    toggleHeader(epilogue);

    return 1;
}

//assumes block is already in a free list
void removeFromFreeList(sf_block *block) {
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next->body.links.prev = block->body.links.prev;

    block->body.links.prev = 0;
    block->body.links.next = 0;
}


sf_size_t getMinSize(sf_size_t size) {
    size+=8;



    if (size<32) {
        return 32;
    }

    //16 byte align
    if (size%16==0) {
        return size;
    }
    return size + 16 - (size%16);

    //potnetnially check if size is big enough for free block

}



//get a free block from the given list. Return NULL if list empty or no valid block available
sf_block *getMainFreeBlk(sf_block *list_head,sf_size_t minSize) {
    // if (list_head->body.links.next == list_head && list_head->body.links.next==list_head) {
    //     return NULL;
    // }
    // return list_head->body.links.prev;

    sf_block *iter = list_head;
    while (iter->body.links.next!=list_head) {
        iter = iter->body.links.next;
        if (getBlockSize(iter)>=minSize) {
            //remove from the freelist
            // iter->body.links.prev->body.links.next = iter->body.links.next;
            // iter->body.links.next->body.links.prev = iter->body.links.prev;
            return iter;
        }
    }
    return NULL;
}

//return a pointer to a free sf_block big enough to contain the minsize.
//If no valid  freeblocks in the main free lists, return NULL
sf_block *getMainList(sf_size_t minSize) {
    // int curSize = 32;
    for (int i = getFreeListIndex(minSize);i<NUM_FREE_LISTS;i++) {
        sf_block* freeBlk = getMainFreeBlk(&sf_free_list_heads[i],minSize);
        //if not empty and 
        if (freeBlk!=NULL) {
            return freeBlk;
        }
    }   
    return NULL;
}

//set the footer of a free block to be equal to its header
void setFooter(sf_block *block) {
    getNext(block)->prev_footer = block->header;
}


//coalescene free blocks before and after input free block return resulting free block
// BLOCK IS NOT VALID FREE BLOCK and ALLOCATED needs to be set to 0 before
sf_block *coalesceLeft(sf_block *block) {
    //if previous block is not allocated
    sf_block *ret_block = block;

    if (!(magicBlockHeader(block) & PREV_BLOCK_ALLOCATED)) {
        //removeFromFreeList(block);
        sf_block *prevBlock = getPrev(block);
        removeFromFreeList(prevBlock);
        //update the block size
        setBlockSize(prevBlock,getBlockSize(prevBlock)+getBlockSize(block));
        //set the footer to be equal to the header

        block->prev_footer = 0;
        block->header = 0;
        ret_block = prevBlock;
    }

    block = getNext(ret_block);
    if (!(magicHeader(block->header) & THIS_BLOCK_ALLOCATED)) {
        removeFromFreeList(block);
        setBlockSize(ret_block,getBlockSize(ret_block)+getBlockSize(block));
        block->header = 0;
    }



    setFooter(ret_block);
    
    //if next block 
    return ret_block;
}





//grow heap and coalesce return a block big enough for the minsie
sf_block *growHeap(sf_size_t minSize) {
    sf_block *newPageBlk, *new_ep, *finBlock;
    while (true) {
        newPageBlk = sf_mem_end() - 16; //epilogue block
        if (sf_mem_grow()==NULL) {
            sf_errno = ENOMEM;
            return NULL;
        }

        //set the header of this new free block newPageBlk setting al to 0 and keeping pal the same
        setHeader(newPageBlk,0,PAGE_SZ,0,newPageBlk->header & PREV_BLOCK_ALLOCATED,0);

        new_ep = getNext(newPageBlk);
        // set the header of the epilogue
        new_ep->header = 4;
        toggleHeader(new_ep);

        finBlock = coalesceLeft(newPageBlk);
        insertFreeBlock(finBlock);

        if (getBlockSize(finBlock)>=minSize) {
            return finBlock;
        }
    }
    return NULL;
}


//splits block into sets size of new block changes size of the block 
sf_block *split(sf_block *block,sf_size_t minSize,sf_size_t payload_size) {
    sf_size_t original_size = getBlockSize(block);
    sf_header newSize = minSize;
    toggleHeader(block);
    block->header = (newSize | (block->header & 2)); //& 2 turns everything else into 0 and keeps the pal
    toggleHeader(block);
    sf_block *rem = getNext(block);
    setHeader(rem,0,original_size-minSize,0,1,0);
    getNext(rem)->prev_footer = rem->header;
    insertFreeBlock(rem);

    return block;
}

//sets the payload size and the 
void allocateBlock(sf_block *block, sf_size_t payload_size) {
    sf_header psize = payload_size;
    psize<<=32;
    toggleHeader(block);
    block->header |= psize | 4;
    toggleHeader(block);


}

//-1 if no valid index for that size, otherwise return the index
int quickListIndex(sf_size_t size) {
    int ind = (size - 32)/16;
    if ((size - 32)%16==0 && ind<QUICK_LIST_MAX) {
        return ind;
    }
    return -1;
}


int quickListEmpty(int ind) {
    if (sf_quick_lists[ind].length==0) {
        return 1;
    }
    return 0;
}

void setQuickBit(sf_block *block,int quick) {
    toggleHeader(block);
    block->header = (block->header & ~(IN_QUICK_LIST)) | quick;
    toggleHeader(block);
}

//pops a quicklist, decrements length, sets quckbit to 0, doesn't touch allocate to prevallocate
sf_block *popQuickList(int ind) {
    if (ind<0 || quickListEmpty(ind)) {
        return NULL;
    }
    sf_block *qBlock = sf_quick_lists[ind].first;
    sf_quick_lists[ind].first=qBlock->body.links.next;
    sf_quick_lists[ind].length--;
    qBlock->body.links.next=NULL;
    setQuickBit(qBlock,0);
    return qBlock;
}

void flushQuickList(int index) {
    sf_block *block;
    while ((block=popQuickList(index))!=NULL) {
        setAlloc(block,0);
        block = coalesceLeft(block);
        insertFreeBlock(block);
    }
}

//assumes block is an allocated block, return 0 if fails, 1 if successful
int insertToQuickList(sf_block *block) {
    int ind = quickListIndex(getBlockSize(block));
    if (ind<0) {
        return 0;
    }

    //quick list at its max, flish
    if (sf_quick_lists[ind].length>=QUICK_LIST_MAX) {
        flushQuickList(ind);
    }

    setQuickBit(block,1);
    setPayload(block,0);

    block->body.links.next = sf_quick_lists[ind].first;
    sf_quick_lists[ind].first = block;
    sf_quick_lists[ind].length++;

    return 1;
}

//try to retrive quick list block for malloc call given a size, removes it from the list, decrements the length, set quickbit to 0
//doesn't change the allocatedbit 
//return NULL if no valid quick list
sf_block *getQuickList(sf_size_t size) {
    int ind = quickListIndex(size);

    if (ind<0 || quickListEmpty(ind)) {
        return NULL;
    }

    sf_block *qBlock = sf_quick_lists[ind].first;
    sf_quick_lists[ind].first=qBlock->body.links.next;
    sf_quick_lists[ind].length--;
    qBlock->body.links.next=NULL;
    setQuickBit(qBlock,0);
    return qBlock;

}



void *sf_malloc(sf_size_t size) {
    //sf_set_magic(0x0);
    if (size == 0) {
        return NULL;
    }

    //if heap is empty then grow the heap by 1 page
    if (sf_mem_start() == sf_mem_end()) {
        if (!initializeHeap()) {
            return NULL;
        }
    }
        
    sf_size_t minSize = getMinSize(size);
    //printf("min: %d\n",minSize);    


    
    sf_block *qListBlk = getQuickList(minSize);

    //retrun qList
    if (qListBlk!=NULL) {
        setPayload(qListBlk,size);
        //printf("\nblock\n");
        //sf_show_block(qListBlk);
        //printf("\n");
        
        return qListBlk->body.payload;
    }

    //check if size exists in main list 
    sf_block *mListBlk = getMainList(minSize);
    
    //if no valid block from main list, grow heap
    if (mListBlk == NULL) {
        mListBlk = growHeap(minSize);
        if (mListBlk == NULL) {
            return NULL;
        }
    }

    //sf_show_block(mListBlk);

    //sf_block *retBlk = mListBlk;

    //final sf_block whose payload will be returned. Try splitting
    removeFromFreeList(mListBlk);

    if (getBlockSize(mListBlk) - minSize >= 32) {
        mListBlk = split(mListBlk,minSize,size);
    }

    curTotalPayload+=size;
    updateMax();
    //set payload size, allocation block, and sets next and prev to 0
    allocateBlock(mListBlk,size);
    return mListBlk->body.payload;
    
    //return retBlk->body.payload;

}






int isValidPointer (void *pp) {
    uintptr_t point_int = (uintptr_t) pp;
    if (pp==NULL) {
        return 0;
    }
    if (point_int %16!=0) {
        return 0;
    }
    sf_block *p_block = (void*) ((char *) pp - 16);
    
    sf_size_t size = getBlockSize(p_block);

    sf_block *next = getNext(p_block);
    if (size<32 || (size%16!=0)) {
        return 0;
    }

    
    if (((uintptr_t) &(p_block->header) < (uintptr_t) sf_mem_start() + 32) || 
        ((uintptr_t) &(next->prev_footer) > (uintptr_t) sf_mem_end()-8)) {
            return 0;
    }

    if ((magicHeader(p_block->header) & THIS_BLOCK_ALLOCATED)==0) {
        return 0;
    } 

    if (((magicHeader(p_block->header) & PREV_BLOCK_ALLOCATED) == 0) && 
        ((magicHeader(getPrev(p_block)->header) & THIS_BLOCK_ALLOCATED)!=0)) {
            return 0;
        }
    return 1;

}

void sf_free(void *pp) {
    if (!isValidPointer(pp)) {
        abort();
    }

    sf_block *block = (void*) ((char*) pp - 16);
    //printf("Hello %d\n",getBlockSize(block));

    curTotalPayload-=getPayloadSize(block);

    // if insert quick lsit successful, return
    if (insertToQuickList(block)) {
        return;
    }

    //set the allocated to 0, keep previous allocated the same, set quick list 0
    setHeader(block,0,getBlockSize(block),0,(magicHeader(block->header) & PREV_BLOCK_ALLOCATED)>>1,0);
    block = coalesceLeft(block);
    insertFreeBlock(block);
    setPrvAlloc(getNext(block),0);

}



void *sf_realloc(void *pp, sf_size_t rsize) {
    if (!isValidPointer(pp)) {
        sf_errno = EINVAL;
        return NULL;
    }

    if (rsize==0) {
        free(pp);
        return NULL;
    }

    sf_size_t minNewSize = getMinSize(rsize);

    sf_block *curBlock = (void*) ((char*) pp - 16);
    sf_size_t curSize = getBlockSize(curBlock);
    
    curTotalPayload-=getPayloadSize(curBlock);
    curTotalPayload+=rsize;
    updateMax();

    if (minNewSize == curSize) {
        setPayload(curBlock,rsize);
        return curBlock;
    }
    else if (minNewSize>curSize) {
        void* newMalloc = sf_malloc(rsize);
        if (newMalloc==NULL) {
            return NULL;
        }
        //sf_block *newBlock = (void*) ((char*)newMalloc - 16);
        memcpy(newMalloc,pp,getPayloadSize(curBlock));
        sf_free(pp);
        return newMalloc;
    } else { //minsize < curSize
        if (curSize - minNewSize >= 32) {
            sf_header newSize = minNewSize;
            toggleHeader(curBlock);
            curBlock->header = (newSize | (curBlock->header & 2)); //& 2 turns everything else into 0 and keeps the pal
            toggleHeader(curBlock);
            sf_block *rem = getNext(curBlock);
            setHeader(rem,0,curSize-minNewSize,0,1,0);
            getNext(rem)->prev_footer = rem->header;
            coalesceLeft(rem);
            insertFreeBlock(rem);

            allocateBlock(curBlock,rsize);
            return pp;
        }
        setPayload(curBlock,rsize);
        return pp;
    }

}


//knows initialized
double getTotalAllocSize() {
    sf_block *start = (void *) ((char*) sf_mem_start() + 32);
    double total = 0;
    while (getBlockSize(start)!=0) {
        if ((magicBlockHeader(start) & THIS_BLOCK_ALLOCATED) && !(magicBlockHeader(start) & IN_QUICK_LIST)) {
            total+=getBlockSize(start);
        }
        start=getNext(start);
    }
    return total;
}

double sf_internal_fragmentation() {
    if (sf_mem_start()==sf_mem_end()) {
        return 0;
    }
    return curTotalPayload/getTotalAllocSize();

}

double sf_peak_utilization() {
    if (sf_mem_start()==sf_mem_end()) {
        return 0;
    }
    
    uintptr_t heapSize = (uintptr_t) (sf_mem_end() - sf_mem_start());
    // printf("%f",maxTotalPayload);
    // printf("\n%ld",heapSize);
    return maxTotalPayload/heapSize;
  
}
