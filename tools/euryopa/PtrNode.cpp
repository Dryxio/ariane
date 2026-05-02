#include "euryopa.h"

static CPtrNode *freeList;

void*
CPtrNode::operator new(size_t)
{
	CPtrNode *n;
	if(freeList){
		n = freeList;
		freeList = n->next;
		n->next = nil;
		n->prev = nil;
		return n;
	}
	return rwNewT(CPtrNode, 1, 0);
}

void
CPtrNode::operator delete(void *p, size_t)
{
	CPtrNode *n = (CPtrNode*)p;
	n->prev = nil;
	n->next = freeList;
	freeList = n;
}
