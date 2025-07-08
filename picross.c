#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#define MAXROWS    100
#define CHARPERCOL 2
#define LINEPERROW 1
#define RESET      "\033[0m"
#define INVERT     "\033[7m"
#define NEUTRAL    "\033[9m"

#define DONTIMES(THING, N) for (int _IDX = 0; _IDX < N; _IDX++) { THING; }
#define CHARATCELL(x, y) \
	(game.filledmasks[y] >> (game.ncols - 1 - x) & 1) ? 'O' : \
	(game.rejectmasks[y] >> (game.ncols - 1 - x) & 1) ? ' ' : '-'
#define SCRCELL(x, y) scr[y][(x) * 9 + 4]
#define SCRCC1(x, y) (scr[y] + (x) * 9)
#define SCRCC2(x, y) (scr[y] + (x) * 9 + 5)

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
static inline int rowlength(Row r);
static struct Hint *chainhints(int *hints, int nhints);
static struct Hint *findrowhints(Row r);
static struct Hint *findcolhints(Row *level, int col);
static int mosthints(struct Hint **hintarr, int nheads);
static void markcell(int x, int y, int reject);
static void parselevel(const char *path);
static int checkwin(void);
static void makeboard(void);
static void updateboard(void);
static void selectrow(int y);
static void selectcol(int x);
static void move(int x, int y);
static void setupterm();
static void resetterm();

static struct Game game = { .posx = 0, .posy = 0 };
static char **scr;
static int scrh, scrw, scrwchars;
static struct termios inittermstate;

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

static inline int
rowlength(Row r)
{
	int length = 0;
	while (r >> length)
		length += 1;
	/* Rows are represented w/ leading `1' to distinguish eg 1 and 01 */
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
	return chainhints(hints, nhints);
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
	for (int i = 0; i < game.nrows; i++)
		game.filledmasks[i] = 1 << game.ncols;
	game.rejectmasks = malloc(sizeof(Row) * nrows);

	memcpy(game.rowhints, hints, sizeof(void *) * nrows);
	memcpy(game.level, level, sizeof(void *) * nrows);
	memset(game.rejectmasks, 0, sizeof(Row) * nrows);

	for (int i = 0; i < game.ncols; i++)
		game.colhints[i] = findcolhints(game.level, i+1);

	game.maxcolhints = mosthints(game.colhints, game.ncols);
	game.maxrowhints = mosthints(game.rowhints, game.nrows);
}

static int
checkwin (void)
{
	int win = 1;
	for (int i = 0; i < game.nrows; i++)
		if (game.level[i] != game.filledmasks[i]) {
			win = 0;
			break;
		}

	return win;
}

static void
makeboard(void)
{
	scrwchars = game.ncols * CHARPERCOL + game.maxrowhints * 3 + 3;
	scrh = game.nrows * LINEPERROW + game.maxcolhints + 1;
	scrw = scrwchars * 9;

	int rpreboardw = game.maxrowhints * 3 + 3;

	scr = malloc(sizeof(void *) * scrh);
	int rowsize = sizeof(char) * scrw + 1;
	DONTIMES(scr[_IDX] = malloc(rowsize); scr[_IDX][scrw] = '\0',
		 scrh);


	const char *blankcell = "\033[9m \033[9m";

	for (int i = 0; i < scrh; i++) {
		for (int j = 0;
		     j < rpreboardw + game.ncols*CHARPERCOL;
		     j++) {
			memcpy(scr[i] + j * 9, blankcell, sizeof(char) * 9);
		}
	}

	DONTIMES(SCRCELL(_IDX, game.maxcolhints) = '-', scrwchars);
	DONTIMES(SCRCELL(game.maxrowhints * 3 + 1, _IDX) = '|', scrh);
	SCRCELL(game.maxrowhints * 3 + 1, game.maxcolhints) = '+';


	char hintbuf[4];
	struct Hint *hint = NULL;

	for (int i = 0; i < game.ncols; i++) {
		hint = game.colhints[i];
		for (int j = 0; hint; j++) {
			sprintf(hintbuf, "%-*d", CHARPERCOL, hint->hint);
			for (int k = 0; k < CHARPERCOL; k++) {
				SCRCELL(rpreboardw + i * CHARPERCOL + k,
					game.maxcolhints - j - 1)
					= hintbuf[k];
			}
			hint = hint->next;
		}
	}

	for (int i = 0; i < game.nrows; i++) {
		hint = game.rowhints[i];
		for (int j = 0; hint; j++ ) {
			sprintf(hintbuf, "%3d", hint->hint);
			for (int k = 0; k < 3; k++) {
				SCRCELL((game.maxrowhints - j - 1) * 3 + k,
					game.maxcolhints + 1 + i)
					= hintbuf[k];
			}
			hint = hint->next;
		}
	}
}

static void
updateboard(void)
{
	for (int i = 0; i < game.ncols; i++) {
		for (int j = 0; j < game.nrows; j++) {
			SCRCELL(i * CHARPERCOL + game.maxrowhints * 3 + 3,
				j * LINEPERROW + game.maxcolhints + 1)
				= CHARATCELL(i, j);
		}
	}
}

static void
selectrow(int y)
{
	memcpy(scr[game.posy * LINEPERROW + game.maxcolhints + 1], NEUTRAL, 4);

	game.posy = y;
	memcpy(SCRCC1(game.posx * CHARPERCOL + game.maxrowhints * 3 + 3, y * LINEPERROW + game.maxcolhints + 1), NEUTRAL, 4);
	memcpy(SCRCC2(game.posx * CHARPERCOL + game.maxrowhints * 3 + 3 + CHARPERCOL - 1, y * LINEPERROW + game.maxcolhints + 1),
	       NEUTRAL, 4);

	memcpy(scr[y * LINEPERROW + game.maxcolhints + 1], INVERT, 4);
	memcpy(scr[y * LINEPERROW + game.maxcolhints + 1] + scrw - 4, RESET, 4);
}

static void
selectcol(int x)
{
	for (int i = 0; i < scrh; i++) {
		if (i != game.posy * LINEPERROW + game.maxcolhints + 1) {
			memcpy(SCRCC1(game.posx * CHARPERCOL + game.maxrowhints * 3 + 3, i), NEUTRAL, 4);
			memcpy(SCRCC2(game.posx * CHARPERCOL + game.maxrowhints * 3 + 3 + CHARPERCOL - 1, i),
			       NEUTRAL, 4);
		}
	}

	game.posx = x;
	for (int i = 0; i < scrh; i++) {
		if (i != game.posy * LINEPERROW + game.maxcolhints + 1) {
			memcpy(SCRCC1(x * CHARPERCOL + game.maxrowhints * 3 + 3, i), INVERT, 4);
			memcpy(SCRCC2(x * CHARPERCOL + game.maxrowhints * 3 + 3 + CHARPERCOL - 1, i),
			       RESET, 4);
		}
	}
}

static void
move(int x, int y)
{
	if (game.posy + y >= game.nrows)
		selectrow(0);
	else if (game.posy + y < 0)
		selectrow(game.nrows - 1);
	else
		selectrow(game.posy + y);

	if (game.posx + x >= game.ncols)
		selectcol(0);
	else if (game.posx + x < 0)
		selectcol(game.ncols - 1);
	else
		selectcol(game.posx + x);
}

static void
resetterm() {
	tcsetattr(STDIN_FILENO, TCSANOW, &inittermstate);
}

static void
setupterm() {
	struct termios newattr;

	errno = 0;
	if (tcgetattr(STDIN_FILENO, &inittermstate) == -1) {
		if (errno == ENOTTY)
			fputs("Unsupported environment. Please use a terminal "
			      "or a terminal emulator",
			      stderr); // add opt-out CLI flag
		exit(1);
	}

	atexit(resetterm);

	tcgetattr(STDIN_FILENO, &newattr);
	newattr.c_lflag |= ~(ICANON|ECHO);
	newattr.c_cc[VMIN] = 1;
	newattr.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &newattr);
}

int
main (void)
{
	parselevel("./level.pic");
	setupterm();
	makeboard();
	updateboard();
	move(0, 0);
	fputs("\033[2J\033[H", stdout);
	DONTIMES(puts(scr[_IDX]), scrh);

	char input;
	int escapedsequence = 0;

	while (1) {
		read(STDIN_FILENO, &input, 1);
		if (input == '\003' || input == '\004')
			exit(0);
		else {
			switch (input) {
			case '\033':
				escapedsequence = 1;
				break;
			case '[':
				if (escapedsequence)
					puts("Complex inputs are not handled");
				escapedsequence = 0;
				break;
			case 'h':
				move(-1, 0);
				break;
			case 'j':
				move(0, 1);
				break;
			case 'k':
				move(0, -1);
				break;
			case 'l':
				move(1, 0);
				break;
			case ' ':
			        markcell(game.posx, game.posy, 0);
				break;
			case 'x':
				markcell(game.posx, game.posy, 1);
				break;
			case 'm':
				markcell(game.posx, game.posy, 0);
				break;
			case 'q':
				exit(1);
			}
		}

		updateboard();
		fputs("\033[2J\033[H", stdout);
		DONTIMES(puts(scr[_IDX]), scrh);
		if (checkwin()) {
			puts("Looks like we got a WINNER!");
			exit(0);
		}

	}


	return 0;
}
