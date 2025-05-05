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
	Row *level; /* level row data as parsed by parselevel */
	Row *filledmasks;
	Row *rejectmasks; /* Row masks of crossed-out cell */
	int posx, posy;
};

static inline Row binStoint(const char *s);
static void inttobinS(Row r, char * buffer);
static inline int rowlength(Row r);
static struct Hint *chainhints(int *hints, int nhints);
static struct Hint *findrowhints(Row r);
static struct Hint *findcolhints(Row *level, int col);
// returns largest number of hints held by a col/row, which we use to determine how much space to allocate to hint printing
static int mosthints(struct Hint **hintarr, int nhints);
static void printhints(struct Hint *first);
static inline int cellstatus(int x, int y); /* 0=empty, 1=filled, 2=rejected */
static void markcell(int x, int y, int reject);
static void parselevel(const char *path);

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

	struct Hint *first = malloc(sizeof(struct Hint));
	if (!first)
		exit(1);
	first->hint = 0;
	if (!nhints)
		return first;
	first->hint = hints[nhints - 1];
	struct Hint *this = first;
	for (int i = nhints - 2; i >= 0; i--) {
		struct Hint *next = malloc(sizeof(struct Hint));
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
findcolhints(Row *level, int col)
{
	int hints[game.nrows / 2 + 1]; /* max. possible */
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

static inline int
cellstatus(int x, int y)
{
	return ((game.filledmasks[y] >> (game.ncols - x)) & 1)
		+ ((game.rejectmasks[y] >> (game.ncols - x)) & 1) * 2;
}

static inline void
markcell(int x, int y, int reject)
{
	if (reject)
		game.rejectmasks[y] |= (1 << (game.ncols - x));
	else
		game.filledmasks[y] |= (1 << (game.ncols - x));
}

static void
parselevel(const char* levelpath)
{
	FILE *levelf = fopen(levelpath, "r");
	if (!levelf)
		exit(1);
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
			/* 3: 1st and last char (see level syntax) and \n */
			game.ncols = llength - 3;
			/* 1 + 1: leading '1' and terminating '\0'*/
			line = malloc(sizeof(char *) * (llength - 3 + 1 + 1));
			line[(llength - 3 + 1 + 1) - 1] = '\0';
			if (!line)
				exit(1);
			line[0] = '1';
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
		strncpy(line, buffer + 1, game.ncols);
		hints[nrows] = findrowhints(binStoint(line - 1));
		level[nrows] = binStoint(line - 1);
	}
	free(buffer);
	free(--line);
	game.nrows = --nrows; /* ignore last row (see level syntax) */
	game.rowhints = malloc(sizeof(void *) * nrows);
	memcpy(game.rowhints, hints, sizeof(void *) * nrows);
	game.level = malloc(sizeof(Row) * nrows);
	memcpy(game.level, level, sizeof(void *) * nrows);
	game.colhints = malloc(sizeof(void *) * game.ncols);
	for (int i = 0; i < game.ncols; i++)
		game.colhints[i] = findcolhints(game.level, i+1);
	game.filledmasks = malloc(sizeof(Row) * nrows);
	game.rejectmasks = malloc(sizeof(Row) * nrows);
	memset(game.filledmasks, 0, sizeof(Row) * nrows);
	memset(game.rejectmasks, 0, sizeof(Row) * nrows);
}

int
main (void)
{
	/* level parsing */
	parselevel("./level.pic");
	printf("rows: %d\n", game.nrows);
	printf("cols: %d\n", game.ncols);
	char *buffer = malloc(sizeof(char) * game.ncols + 1);
	for (int i = 0; i < game.nrows; i++) {
		printf("%.2d: ", i + 1);
		inttobinS(game.level[i], buffer);
		printf("%s\n", buffer);
	}

	/* rowhints */
	for (int i = 0; i < game.nrows; i++) {
		printf("%.2d: ", i + 1);
		printhints(game.rowhints[i]);
	}

	/* colhints */
	for (int i = 0; i < game.ncols; i++) {
		printf("%.2d: ", i + 1);
		printhints(game.colhints[i]);
	}

	/* cellstatus, markcell */
	markcell(4, 5, 0);
	markcell(9, 7, 1);
	for (int y = 0; y < game.nrows; y++) {
		for (int x = 0; x < game.ncols; x++)
			printf("%d ", cellstatus(x, y));
		puts("");
	}

	return 0;
}
