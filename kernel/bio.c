// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct
{
	struct spinlock lock[NBUCKETS];
	struct buf buf[NBUF];
	int rubucketno;

	// Linked list of all buffers, through prev/next.
	// Sorted by how recently the buffer was used.
	// head.next is most recent, head.prev is least.
	struct buf hashbucket[NBUCKETS];
} bcache;

void binit(void)
{
	struct buf *b;
	int j = 0;

	bcache.rubucketno = 0;

	//init spinlocks of ever buckets
	for(int i = 0;i < NBUCKETS;i++)
	{
		initlock(&bcache.lock[i], "bcache");
		bcache.hashbucket[i].prev = &bcache.hashbucket[i];
		bcache.hashbucket[i].next = &bcache.hashbucket[i];
		b = bcache.buf;
		for (j = 0; j < NBUCKETS; j++,b++)
		{
			b->next = bcache.hashbucket[j].next;
			b->prev = &bcache.hashbucket[j];
			initsleeplock(&b->lock, "buffer");
			bcache.hashbucket[j].next = b;
			bcache.hashbucket[j].last = b;
		}
	}

	// Create linked list of buffers
	for (j = 0; b < bcache.buf + NBUF; b++)
	{
		b->next = bcache.hashbucket[j].next;
		b->prev = &bcache.hashbucket[j];
		initsleeplock(&b->lock, "buffer");
		bcache.hashbucket[j].next->prev = b;
		bcache.hashbucket[j].next = b;
		j = (j+1)%NBUCKETS;
	}
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
	struct buf *b;
	int bucketno = blockno % NBUCKETS;
	int bucketno_shouldbe = blockno % NBUCKETS;
	// Is the block already cached?
	
	acquire(&bcache.lock[bucketno_shouldbe]);
	bcache.rubucketno = bucketno;
	for (b = bcache.hashbucket[bucketno].last; b != &bcache.hashbucket[bucketno]; b = b->prev)
	{
		if (b->dev == dev && b->blockno == blockno)
		{
			b->refcnt++;
			release(&bcache.lock[bucketno_shouldbe]);
			acquiresleep(&b->lock);
			return b;
		}
	}
	
	// Not cached.
	// Recycle the least recently used (LRU) unused buffer and steel buffer from other buckets.
	for(int i = 0;i < NBUCKETS;i++,bucketno = (bucketno+1)%NBUCKETS)
	{
		if(bucketno != bucketno_shouldbe)
		{
			if(bcache.lock[bucketno].locked)
			{
				continue;
			}
			acquire(&bcache.lock[bucketno]);
		}
		b = bcache.hashbucket[bucketno].next;
		if (b != &bcache.hashbucket[bucketno] && b->refcnt == 0)
		{
			b->dev = dev;
			b->blockno = blockno;
			b->valid = 0;
			b->refcnt = 1;
			
			b->next->prev = b->prev;
			b->prev->next = b->next;
			b->next = &bcache.hashbucket[bucketno_shouldbe];
			b->prev = bcache.hashbucket[bucketno_shouldbe].last;
			bcache.hashbucket[bucketno_shouldbe].last->next = b;
			bcache.hashbucket[bucketno_shouldbe].last = b;
			
			if(bucketno != bucketno_shouldbe) 
			{
				release(&bcache.lock[bucketno]);
			}
			release(&bcache.lock[bucketno_shouldbe]);
			acquiresleep(&b->lock);
			return b;
		}
		if(bucketno != bucketno_shouldbe) 
		{
			release(&bcache.lock[bucketno]);
		}
	}
	panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
	struct buf *b;

	b = bget(dev, blockno);
	if (!b->valid)
	{
		virtio_disk_rw(b, 0);
		b->valid = 1;
	}
	return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
	if (!holdingsleep(&b->lock))
		panic("bwrite");
	virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
	int bucketno = b->blockno % NBUCKETS;
	if (!holdingsleep(&b->lock))
		panic("brelse");

	releasesleep(&b->lock);
	acquire(&bcache.lock[bucketno]);
	b->refcnt--;	
	if (b->refcnt == 0)
	{			
		// no one is waiting for it.
		if(b == bcache.hashbucket[bucketno].last)
		{
			bcache.hashbucket[bucketno].last = b->prev;
			b->prev->next = b->next;
			b->next = bcache.hashbucket[bucketno].next;
			b->prev = &bcache.hashbucket[bucketno];
			bcache.hashbucket[bucketno].next->prev = b;
			bcache.hashbucket[bucketno].next = b;
		}
		else 
		{
			b->next->prev = b->prev;
			b->prev->next = b->next;
			b->next = bcache.hashbucket[bucketno].next;
			b->prev = &bcache.hashbucket[bucketno];
			bcache.hashbucket[bucketno].next->prev = b;
			bcache.hashbucket[bucketno].next = b;
		}
	}
	release(&bcache.lock[bucketno]);
}

void bpin(struct buf *b)
{
	int bucketno = b->blockno % NBUCKETS;
	acquire(&bcache.lock[bucketno]);
	b->refcnt++;
	release(&bcache.lock[bucketno]);
}

void bunpin(struct buf *b)
{
	int bucketno = b->blockno % NBUCKETS;
	acquire(&bcache.lock[bucketno]);
	b->refcnt--;
	release(&bcache.lock[bucketno]);
}
