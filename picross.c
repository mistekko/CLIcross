#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define MAXROWS    100
#define CHARPERCOL 2
#define LINEPERROW 1

#define DONTIMES(THING, N) for (int _IDX = 0; _IDX < N; _IDX++) { THING; }
#define CHARATCELL(x, y) \
	(game.filledmasks[y] >> (game.ncols - 1 - x) & 1) ? 'O' : \
	(game.rejectmasks[y] >> (game.ncols - 1 - x) & 1) ? 'x' : '-'

typedef unsigned long long int Row;

struct Hint {
	int hint;
	struct Hint* next;
};

struct Game {
	struct Hint **rowhints;
	struct Hint **colhints;
	int nrows, ncols;
	int maxrowhints, maxcolhints;
	Row *level; /* level row data as parsed by parselevel */
	Row *filledmasks;
	Row *rejectmasks; /* Row masks of crossed-out cell */
	int posx, posy;
};

static inline Row binStoint(const char *s);
static void inttobinS(char * buffer, Row r);
static inline int rowlength(Row r);
static struct Hint *chainhints(int *hints, int nhints);
static struct Hint *findrowhints(Row r);
static struct Hint *findcolhints(Row *level, int col);
static int mosthints(struct Hint **hintarr, int nheads);
static void printhints(struct Hint *first);
static inline int cellstatus(int x, int y); /* 0=empty, 1=filled, 2=rejected */
static void markcell(int x, int y, int reject);
static void parselevel(const char *path);
static void printcolshints(void);
static void printrowshints(void);
static void printcells(void);
static void printboard(void); // print entire board!!
static void update(void); // change game state according to events and then update board

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
inttobinS(char *buffer, Row r)
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
	while (r >> length)
		length += 1;
	/* 01 is represented as 1, so we use 101 and discard first digit */
	return length - 1;
}

struct Hint *
chainhints(int *hints, int nhints)
{

	struct Hint *first = malloc(sizeof(struct Hint));
	if (!first)
		exit(1);
	first->hint = 0;
	first->next = NULL;
	if (!nhints)
		return first;
	first->hint = hints[nhints - 1];
	struct Hint *this = first;
	for (int i = nhints - 2; i >= 0; i--) {
		struct Hint *next = malloc(sizeof(struct Hint));
		if (next == NULL)
			exit (1);
		next->hint = hints[i];
		next->next = NULL;
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
		if (r >> (game.ncols - 1 - i) & 1) {
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
	int hintsreversed[nhints];
	for (int i = 0; i < nhints; i++)
		hintsreversed[i] = hints[nhints - 1 - i];
	return chainhints(hintsreversed, nhints);
}

static inline int
chainlength(struct Hint *first)
{
	int i = 1;
	for (struct Hint *this = first; this->next; this = this->next)
		i++;
	return i;
}

static inline int
mosthints(struct Hint **hints, int nheads)
{
	int most = 0;
	for (int i = 0; i < nheads; i++)
		if (chainlength(hints[i]) > most)
			most = chainlength(hints[i]);
	return most;
}

static void
printhints(struct Hint *first)
{
	for (struct Hint *this = first;	this; this = this->next)
		printf("%d ", this->hint);
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
		game.rejectmasks[y] ^= (1 << (game.ncols - x - 1));
	else
		game.filledmasks[y] ^= (1 << (game.ncols - x - 1));
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

	game.rowhints    = malloc(sizeof(void *) * nrows);
	game.colhints    = malloc(sizeof(void *) * game.ncols);
	game.level       = malloc(sizeof(Row) * nrows);
	game.filledmasks = malloc(sizeof(Row) * nrows);
	game.rejectmasks = malloc(sizeof(Row) * nrows);

	memcpy(game.rowhints, hints, sizeof(void *) * nrows);
	memcpy(game.level, level, sizeof(void *) * nrows);
	memset(game.filledmasks, 0, sizeof(Row) * nrows);
	memset(game.rejectmasks, 0, sizeof(Row) * nrows);

	for (int i = 0; i < game.ncols; i++)
		game.colhints[i] = findcolhints(game.level, i+1);

	game.maxcolhints = mosthints(game.colhints, game.ncols);
	game.maxrowhints = mosthints(game.rowhints, game.nrows);
}

static void
printcolshints(void)
{
	DONTIMES(putchar('\n'), game.maxcolhints - 1);
	DONTIMES(putchar(' '),  game.maxrowhints * 3 - 1 + 2);
	for (int i = 0; i < game.ncols; i++) {
		fputs("\033[s", stdout);
		struct Hint *this = game.colhints[i];
		for (; this != NULL; this = this->next)
			printf("%*d\033[1A\033[%dD",
			       CHARPERCOL,
			       this->hint,
			       CHARPERCOL);
		printf("\033[u\033[%dC", CHARPERCOL);
	}
	putchar('\n');
	DONTIMES(putchar('-'), game.maxrowhints * 3);
	putchar('+');
	DONTIMES(putchar('-'), game.ncols * CHARPERCOL);
	putchar('\n');
}

static void
printrowshints(void)
{
	DONTIMES(putchar(' '), (game.maxrowhints - 1) * 3);
	for (int i = 0; i < game.nrows; i++) {
		fputs("\033[s", stdout);
		struct Hint *this = game.rowhints[i];
		for (; this != NULL; this = this->next)
			printf("%2d\033[5D", this->hint);
		DONTIMES(putchar('\n'), LINEPERROW);
		printf("\033[u\033[%dB", LINEPERROW);
	}
	fputs("\033[1A\033[3C", stdout);
	for (int i = 0; i < game.nrows * LINEPERROW + game.maxcolhints; i++) {
		if (i == game.nrows * LINEPERROW)
			fputs("\033[s\033[1A", stdout);
		fputs("|\033[1A\033[1D", stdout);
	}
	fputs("\033[u\033[1C\033[1B", stdout);
}

static void
printcells(void)
{
	for (int i = 0; i < game.nrows; i++) {
		fputs("\033[s", stdout);
		DONTIMES(printf(" %c",CHARATCELL(_IDX, i)),
			 game.ncols)
		printf("\033[u\033[%dB", LINEPERROW);
	}
}


int
main (void)
{
	parselevel("level.pic");
	/* fputs("\033[2J\033[H", stdout); */
	printcolshints();
	printrowshints();

	markcell(0, 0, 0);
	markcell(1, 0, 1);


	printcells();

	return 0;
}
