/* Local Variables: */
/* compile-command: "cc picross.c -Wall -Wextra -pedantic -o p" */
/* End: */

#include <stdlib.h>
#include <stdio.h>

struct Hint;
struct Hint {
	int hint;
	struct Hint* next;
};

struct Game {
	int nrows;
	int ncols;
	struct Hint **rowhints;
	struct Hint **colhints;
	unsigned long long int *rowfilledmasks;
	unsigned long long int *rowrejectmasks; /* crossed-out cells */
	int posx, posy;
};

static inline unsigned long long int binStoint(const char *s);

static void parselevel(); // todo
static inline int rowlength(unsigned long long int row);
static struct Hint *findrowhints(unsigned long long int row);
static void printhints(struct Hint *first);
static char cellstatus(int x, int y); // todo: need Game object first
static void markcell(int x, int y, int reject); /* reject: 0=fill, 1=reject */ //todo
static void creategame(); // todo: returns fully populated Game struct

static unsigned long long int
binStoint(const char *s)
{
        unsigned long long int i = 0;
        while (*s) {
                i <<= 1;
                i += *s++ - '0';
        }
        return i;
}

static inline int
rowlength(unsigned long long int row)
{
	int length = 0;
	while (row >> length) {
		length += 1;
	}
	return length - 1; /* first 1 allows for leading 0s in row content*/
}

static struct Hint
*findrowhints(unsigned long long int row)
{
	int maxhints = rowlength(row) / 2 + 1;
	int hints[maxhints];
	int nhints = 0;
	int chain = 0;
	for (int i = 0; i < rowlength(row); i++) {
		if (row >> i & 1) {
			if (!chain)
				nhints++;
			chain++;
			hints[nhints-1] = chain;
		} else
			chain = 0;
	}
	if (!nhints)
		exit(1);

	struct Hint *first = malloc (sizeof(struct Hint));
	if (first == 0)
		exit(1);
	first->hint = hints[0];
	struct Hint *this = first;
	for (int i = 1; i < nhints; i++) {
		struct Hint *next = malloc (sizeof(struct Hint));
		if (next == 0)
			exit (1);
		next->hint = hints[i];
		this->next = next;
		this = next;
	}
	return first;
}

static void
printhints(struct Hint *first)
{
	for (struct Hint *this = first;	this; this = this->next) {
		printf("%d ", this->hint);
	}
	puts("\n");
}


int
main ()
{
	unsigned long long int rowcontent = binStoint("110101");
	struct Hint *hint = findrowhints(rowcontent);
	printhints(hint);

	return 0;
}
