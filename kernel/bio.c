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
	int freeblocknum;

	// Linked list of all buffers, through prev/next.
	// Sorted by how recently the buffer was used.
	// head.next is most recent, head.prev is least.
	struct buf hashbucket[NBUCKETS];
} bcache;

void binit(void)
{
	struct buf *b;
	int i = 0;
	bcache.freeblocknum = 0;
	for (i = 0; i < NBUCKETS; i++)
	{
		initlock(&bcache.lock[i], "bcache");
		bcache.hashbucket[i].prev = &bcache.hashbucket[i];
		bcache.hashbucket[i].next = &bcache.hashbucket[i];
	}

	// Create linked list of buffers
	for (b = bcache.buf; b < bcache.buf + NBUF; b++,i = (i+1)%NBUCKETS)
	{
		i = 0;
		b->next = bcache.hashbucket[i].next;
		b->prev = &bcache.hashbucket[i];
		initsleeplock(&b->lock, "buffer");
		bcache.hashbucket[i].next->prev = b;
		bcache.hashbucket[i].next = b;
		bcache.freeblocknum++;
	}
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
	struct buf *b;
	int bucketno_shouldbe = blockno % NBUCKETS;
	int bucketno_current = bucketno_shouldbe; 

	// Is the block already cached?
	acquire(&bcache.lock[bucketno_shouldbe]);
	for (b = bcache.hashbucket[bucketno_shouldbe].next; b != &bcache.hashbucket[bucketno_shouldbe]; b = b->next)
	{
		if (b->dev == dev && b->blockno == blockno)
		{
			b->refcnt++;
			release(&bcache.lock[bucketno_shouldbe]);
			acquiresleep(&b->lock);
			return b;
		}
	}
	// release(&bcache.lock[bucketno_shouldbe]);

	// Not cached.
	// Recycle the least recently used (LRU) unused buffer.
	for(bucketno_current = bucketno_shouldbe;bcache.freeblocknum > 0;bucketno_current = (bucketno_current+1)%NBUCKETS)
	{
		if(bcache.lock[bucketno_current].locked) continue;

		if(bucketno_current != bucketno_shouldbe) {acquire(&bcache.lock[bucketno_current]);}
		for (b = bcache.hashbucket[bucketno_current].next; b != &bcache.hashbucket[bucketno_current]; b = b->next)
		{
			if (b->refcnt == 0)
			{
				// if(bucketno_current != bucketno_shouldbe) {acquire(&bcache.lock[bucketno_shouldbe]);}
				b->dev = dev;
				b->blockno = blockno;
				b->valid = 0;
				b->refcnt = 1;
				if(bucketno_current != bucketno_shouldbe)
				{
					b->next->prev = b->prev;
					b->prev->next = b->next;
					b->next = bcache.hashbucket[bucketno_shouldbe].next;
					b->prev = &bcache.hashbucket[bucketno_shouldbe];
					bcache.hashbucket[bucketno_shouldbe].next->prev = b;
					bcache.hashbucket[bucketno_shouldbe].next = b;
				}

				release(&bcache.lock[bucketno_shouldbe]);
				if(bucketno_current != bucketno_shouldbe) {release(&bcache.lock[bucketno_current]);}
				acquiresleep(&b->lock);
				bcache.freeblocknum--;
				return b;
			}
		}
		if(bucketno_current != bucketno_shouldbe) {release(&bcache.lock[bucketno_current]);}
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
		bcache.freeblocknum++;
		b->next->prev = b->prev;
		b->prev->next = b->next;
		b->next = bcache.hashbucket[bucketno].next;
		b->prev = &bcache.hashbucket[bucketno];
		bcache.hashbucket[bucketno].next->prev = b;
		bcache.hashbucket[bucketno].next = b;
	}

	release(&bcache.lock[bucketno]);
}

void bpin(struct buf *b)
{
	int bucketno = b->blockno%NBUCKETS;
	acquire(&bcache.lock[bucketno]);
	b->refcnt++;
	release(&bcache.lock[bucketno]);
}

void bunpin(struct buf *b)
{
	int bucketno = b->blockno%NBUCKETS;
	acquire(&bcache.lock[bucketno]);
	b->refcnt--;
	release(&bcache.lock[bucketno]);
}
