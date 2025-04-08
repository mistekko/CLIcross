/* Local Variables: */
/* compile-command: "cc picross.c -Wall -Wextra -pedantic -o p" */
/* End: */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define MAXROWS 100

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
static char cellstatus(int x, int y); // todo: need full Game object first
static void markcell(int x, int y, int reject); /* reject: 0=fill, 1=reject */ //todo
static void parselevel();

static struct Game game = {
	.posx = 0,
	.posy = 0
};

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
	return length - 1; /* first 1 allows for leading 0s in row content */
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
	struct Hint *first = malloc (sizeof(struct Hint));
	if (first == 0)
		exit(1);
	first->hint = 0;
	if (!nhints)
		return first;
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
	printf("\n");
}

static void
parselevel(const char* levelpath)
{
	FILE *levelf = fopen(levelpath, "r");
	if (!levelf) {
		exit(1);
	}
	struct Hint *hints[MAXROWS];
	char *buffer = NULL;
	char *line = NULL;
	ssize_t llength = 0;
	size_t n = 0;
	int nrows = 0;
	getline(&buffer, &n, levelf);
	for (; (llength = getline(&buffer, &n, levelf)) != -1; nrows++) {
		if (nrows == 0) {
			game.ncols = llength - 3;
			line = malloc(sizeof(char *) * (llength - 3 + 1 + 1));
			if (!line) {
				exit(1);
			}
			line[0] = '1'; /* see rowlength() */
			line++;
		} else if (llength - 3 > game.ncols) {
			printf("Warning: line %d is too long. "
			       "Ignoring excess characters...\n",
			       nrows + 1);
		} else if (llength - 3 < game.ncols) {
			printf("Error: line %d is too short (%ld). "
			       "Exiting...\n",
			       nrows + 1, llength);
			exit(1);
		}
		strlcpy(line, buffer + 1, game.ncols + 1);
		hints[nrows] = findrowhints(binStoint(line - 1));
	}
	free(buffer);
	free(--line);
	game.nrows = nrows;
	game.rowhints = malloc(sizeof(void *) * nrows);
	memcpy(game.rowhints, hints, sizeof(void *) * (nrows - 1));
}

int
main ()
{
	parselevel("./level.pic");
	printf("rows: %d\n", game.nrows);
	printf("cols: %d\n", game.ncols);

	for (int i = 0; i < game.nrows; i++) {
		printf("%.2d: ", i + 1);
		printhints(game.rowhints[i]);
	}
	return 0;
}
