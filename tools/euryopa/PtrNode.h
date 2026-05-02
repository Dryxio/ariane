class CPtrNode
{
public:
	void *item;
	CPtrNode *prev;
	CPtrNode *next;

	void *operator new(size_t);
	void operator delete(void*, size_t);
};
