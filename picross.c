/* Local Variables: */
/* compile-command: "cc picross.c -Wall -Wextra -pedantic -o p" */
/* End: */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define MAXROWS 100
typedef unsigned long long int Row;

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
	Row *level; /* all level data */
	Row *filledmasks;
	Row *rejectedmasks; /* Row masks of crossed-out cell */
	int posx, posy;
};

static inline Row binStoint(const char *s);
static void inttobinS(Row r, char * buffer);

static void parselevel(); // todo
static inline int rowlength(Row r);
static struct Hint *chainhints(int *hints, int nhints);
static struct Hint *findrowhints(Row r);
static struct Hint *findcolhints(Row *level, int col);
static void printhints(struct Hint *first);
static char cellstatus(int x, int y); // todo: need full Game object first
static void markcell(int x, int y, int reject); /* reject: 0=fill, 1=reject */ //todo
static void parselevel();

static struct Game game = {
	.posx = 0,
	.posy = 0
};

static Row
binStoint(const char *s)
{
        Row r = 0;
        while (*s) {
                r <<= 1;
                r += *s++ - '0';
        }
        return r;
}

static void
inttobinS(Row r, char *buffer)
{
	buffer += game.ncols;
	*buffer = '\0';
	while (r ^ 1) {
		*--buffer = (r & 1) + '0';
		r >>= 1;
	}
}

static inline int
rowlength(Row r)
{
	int length = 0;
	while (r >> length) {
		length += 1;
	}
	return length - 1; /* first 1 allows for leading 0s in row content */
}

struct Hint *
chainhints(int *hints, int nhints)
{

	struct Hint *first = malloc (sizeof(struct Hint));
	if (!first)
		exit(1);
	first->hint = 0;
	if (!nhints)
		return first;
	first->hint = hints[nhints - 1];
	struct Hint *this = first;
	for (int i = nhints - 2; i >= 0; i--) {
		struct Hint *next = malloc (sizeof(struct Hint));
		if (!next)
			exit (1);
		next->hint = hints[i];
		this->next = next;
		this = next;
	}
	return first;
}

struct Hint *
findrowhints(Row r)
{
	int hints[game.ncols / 2 + 1];
	int nhints = 0;
	for (int i = 0, chain = 0; i < game.ncols; i++) {
		if (r >> i & 1) {
			if (!chain)
				nhints++;
			chain++;
			hints[nhints-1] = chain;
		} else
			chain = 0;
	}
	return chainhints(hints, nhints);
}

struct Hint *
findcolwhints(Row *level, int col)
{
	int hints[game.nrows / 2 + 1];
	int nhints = 0;
	for (int i = 0, chain = 0; i < game.nrows; i++) {
		if (level[i] >> (game.ncols - col) & 1) {
			if (!chain)
				nhints++;
			chain++;
			hints[nhints-1] = chain;
		} else
			chain = 0;
	}
	return chainhints(hints, nhints);
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
	Row level[MAXROWS];
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
		level[nrows] = binStoint(line - 1);
	}
	free(buffer);
	free(--line);
	game.nrows = --nrows; // last row has no level info
	game.rowhints = malloc(sizeof(void *) * nrows);
	memcpy(game.rowhints, hints, sizeof(void *) * nrows);
	game.level = malloc(sizeof(Row) * nrows);
	memcpy(game.level, level, sizeof(void *) * nrows);
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

	char *buffer = malloc(sizeof(char) * game.ncols + 1);
	for (int i = 0; i < game.nrows; i++) {
		printf("%.2d: ", i + 1);
		inttobinS(game.level[i], buffer);
		printf("%s\n", buffer);
	}
	return 0;
}
