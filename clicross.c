#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#define MAXROWS    100
#define RESET      "\033[0m"
#define ENBOLDEN   "\033[1m"
#define INVERT     "\033[7m"
#define NEUTRAL    "\033[9m"
#define CLEAR      "\033[2J"
#define GOTOTOP    "\033[H"

#define DONTIMES(thing, n)				      \
	for (int TIMESDONE = 0; TIMESDONE < n; TIMESDONE++) { \
		thing;					      \
	}
#define CHARATCELL(x, y)					\
	(filledmasks[y] >> (ncols - 1 - x) & 1) ? 'O' :		\
	(rejectmasks[y] >> (ncols - 1 - x) & 1) ? ' ' : '-'
#define SCRCELL(x, y) scr[y][(x) * 9 + 4]
#define SCRCC1(x, y) (scr[y] + (x) * 9)
#define SCRCC2(x, y) (scr[y] + (x) * 9 + 5)
#define BRDROW(y) ((y) * lineperrow + maxcolhints + 1)
#define BRDCOL(x) ((x) * charpercol + maxrowhints * 3 + 3)
#define BINDKEY(key, function) case key: function; break

typedef unsigned long long int Row;

struct Hint {
	int hint;
	struct Hint* next;
};

static inline Row binStoint(const char *s);
static inline int rowlen(Row r);
static struct Hint *chainhints(int *hints, int nhints);
static struct Hint *findrowhints(Row r);
static struct Hint *findcolhints(Row *level_l, int col);
static int mosthints(struct Hint **hintarr, int nheads);
static void markcell(int x, int y, int reject);
static void parselevel(const char *path);
static int checkwin(void);
static void makebrd(void);
static void updatebrd(void);
static void selrow(int y);
static void selcol(int x);
static void move(int x, int y);
static void setupterm(void);
static void resetterm(void);

static int charpercol = 2;
static int lineperrow = 1;
static struct Hint **rowhints;
static struct Hint **colhints;
static int nrows, ncols;
static int maxrowhints, maxcolhints;
static Row *level; /* level row data as parsed by parselevel */
static Row *filledmasks;
static Row *rejectmasks; /* Row masks of crossed-out cell */
static int posx, posy;
static char **scr;
static int scrh, scrw;
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
rowlen(Row r)
{
	int length = 0;
	while (r >> length++);
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
	int hints[ncols / 2 + 1];
	int nhints = 0;
	for (int i = 0, chain = 0; i < ncols; i++) {
		if (r >> (ncols - 1 - i) & 1) {
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
	int hints[nrows / 2 + 1]; /* max. possible */
	int nhints = 0;
	for (int i = 0, chain = 0; i < nrows; i++) {
		if (level[i] >> (ncols - col - 1) & 1) {
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
	return ((filledmasks[y] >> (ncols - x)) & 1)
		+ ((rejectmasks[y] >> (ncols - x)) & 1) * 2;
}

static inline void
markcell(int x, int y, int reject)
{
	if (reject)
		rejectmasks[y] ^= (1 << (ncols - x - 1));
	else
		filledmasks[y] ^= (1 << (ncols - x - 1));
}

static void
parselevel(const char* levelpath)
{
	FILE *levelf = fopen(levelpath, "r");
	if (!levelf)
		exit(1);
	struct Hint *hints[MAXROWS];
	Row leveltmp[MAXROWS];
	char *buffer = NULL;
	char *line = NULL;
	ssize_t llength = 0;
	size_t n = 0;
	getline(&buffer, &n, levelf);
	for (; (llength = getline(&buffer, &n, levelf)) != -1; nrows++) {
		if (nrows == 0) {
			/* 3: 1st and last char (see level syntax) and \n */
			ncols = llength - 3;
			/* 1 + 1: leading '1' and terminating '\0'*/
			line = malloc(sizeof(char *) * (llength - 3 + 1 + 1));
			if (!line)
				exit(1);
			line[(llength - 3 + 1 + 1) - 1] = '\0';
			line[0] = '1';
			line++;
		} else if (llength - 3 > ncols) {
			printf("Warning: line %d is too long. "
			       "Ignoring excess characters...\n",
			       nrows + 1);
		} else if (llength - 3 < ncols) {
			printf("Error: line %d is too short (%ld). "
			       "Exiting...\n",
			       nrows + 1, llength);
			exit(1);
		}
		strncpy(line, buffer + 1, ncols);
		hints[nrows] = findrowhints(binStoint(line - 1));
		leveltmp[nrows] = binStoint(line - 1);
	}
	free(buffer);
	free(--line);

	--nrows; /* ignore last row (see level syntax) */

	rowhints    = malloc(sizeof(void *) * nrows);
	colhints    = malloc(sizeof(void *) * ncols);
	level       = malloc(sizeof(Row) * nrows);
	filledmasks = malloc(sizeof(Row) * nrows);
	for (int i = 0; i < nrows; i++)
		filledmasks[i] = 1 << ncols;
	rejectmasks = malloc(sizeof(Row) * nrows);

	memcpy(rowhints, hints, sizeof(void *) * nrows);
	memcpy(level, leveltmp, sizeof(void *) * nrows);
	memset(rejectmasks, 0, sizeof(Row) * nrows);

	for (int i = 0; i < ncols; i++)
		colhints[i] = findcolhints(level, i);

	maxcolhints = mosthints(colhints, ncols);
	maxrowhints = mosthints(rowhints, nrows);
}

static int
checkwin (void)
{
	struct Hint *testhinthead;
	struct Hint *testhint; // remember to free values assigned to this var
	struct Hint *truehint;
	int colwin = 1;
	int rowwin = 1;

	// abstract these two for-loops;
	for (int i = 0; i < ncols; i++) {
		truehint = colhints[i];
		testhinthead = findcolhints(filledmasks, i);
		testhint = testhinthead;
		for (int j = 0;
		     testhint && truehint;
		     j++, testhint = testhint->next, truehint = truehint->next
		     ) {
			if (testhint->hint == truehint->hint) {
				// abstract this as well
				memcpy(SCRCC1(BRDCOL(i), maxcolhints-j-1),
				       ENBOLDEN, 4);
				memcpy(SCRCC2(BRDCOL(i+1) - 1, maxcolhints-j-1),
				       RESET, 4);
			} else {
				colwin = 0;
				memcpy(SCRCC1(BRDCOL(i), maxcolhints-j-1),
				       NEUTRAL, 4);
				memcpy(SCRCC2(BRDCOL(i+1) - 1, maxcolhints-j-1),
				       NEUTRAL, 4);
			}
		}
		if (testhint || truehint)
			colwin = 0;
		free(testhinthead);
	}

	for (int i = 0; i < nrows; i++) {
		truehint = rowhints[i];
		testhinthead = findrowhints(filledmasks[i]);
		testhint = testhinthead;
		for (int j = 0;
		     testhint && truehint;
		     j++, testhint = testhint->next, truehint = truehint->next
		     ) {
			if (testhint->hint == truehint->hint) {
				memcpy(SCRCC1((maxrowhints - 1 -j) * 3, BRDROW(i)),
				       ENBOLDEN, 4);
				memcpy(SCRCC2((maxrowhints - j) * 3 - 1, BRDROW(i)),
				       RESET, 4);
			} else {
				rowwin = 0;
				memcpy(SCRCC1((maxrowhints - 1 -j) * 3, BRDROW(i)),
				       NEUTRAL, 4);
				memcpy(SCRCC2((maxrowhints - j) * 3 - 1, BRDROW(i)),
				       NEUTRAL, 4);
			}
		}
		if (testhint || truehint)
			rowwin = 0;
		free(testhinthead);
	}
	return colwin && rowwin;
}

static void
makebrd(void)
{
	int scrwchars = ncols * charpercol + maxrowhints * 3 + 3;
	scrh = nrows * lineperrow + maxcolhints + 1;
	scrw = scrwchars * 9;

	int rpreboardw = maxrowhints * 3 + 3;

	scr = malloc(sizeof(void *) * scrh);
	int rowsize = sizeof(char) * scrw + 1;
	DONTIMES(scr[TIMESDONE] = malloc(rowsize); scr[TIMESDONE][scrw] = '\0',
		 scrh);


	const char *blankcell = "\033[9m \033[9m";

	for (int i = 0; i < scrh; i++) {
		for (int j = 0;
		     j < scrwchars;
		     j++) {
			memcpy(scr[i] + j * 9, blankcell, sizeof(char) * 9);
		}
	}

	DONTIMES(SCRCELL(TIMESDONE, maxcolhints) = '-', scrwchars);
	DONTIMES(SCRCELL(maxrowhints * 3 + 1, TIMESDONE) = '|', scrh);
	SCRCELL(maxrowhints * 3 + 1, maxcolhints) = '+';


	char hintbuf[4];
	struct Hint *hint = NULL;

	for (int i = 0; i < ncols; i++) {
		hint = colhints[i];
		for (int j = 0; hint; j++) {
			sprintf(hintbuf, "%-*d", charpercol, hint->hint);
			for (int k = 0; k < charpercol; k++) {
				SCRCELL(rpreboardw + i * charpercol + k,
					maxcolhints - j - 1)
					= hintbuf[k];
			}
			hint = hint->next;
		}
	}

	for (int i = 0; i < nrows; i++) {
		hint = rowhints[i];
		for (int j = 0; hint; j++ ) {
			sprintf(hintbuf, "%3d", hint->hint);
			for (int k = 0; k < 3; k++) {
				SCRCELL((maxrowhints - j - 1) * 3 + k,
					maxcolhints + 1 + i)
					= hintbuf[k];
			}
			hint = hint->next;
		}
	}
}

static void
updatebrd(void)
{
	for (int i = 0; i < ncols; i++) {
		for (int j = 0; j < nrows; j++) {
			SCRCELL(BRDCOL(i), BRDROW(j)) = CHARATCELL(i, j);
		}
	}
}

static void
selrow(int y)
{
	int newry = BRDROW(y);
	int cellx = BRDCOL(posx);

	memcpy(SCRCC1(BRDCOL(0),BRDROW(posy)), NEUTRAL, 4);

	posy = y;
	memcpy(SCRCC1(cellx, newry), NEUTRAL, 4);
	memcpy(SCRCC2(cellx + charpercol - 1, newry), NEUTRAL, 4);

	memcpy(SCRCC1(BRDCOL(0), newry), INVERT, 4);
	memcpy(scr[newry] + scrw - 4, RESET, 4);
}

static void
selcol(int x)
{
	int oldcx = BRDCOL(posx);
	int newcx = BRDCOL(x);

	for (int i = maxcolhints + 1; i < scrh; i++) {
		if (i != BRDROW(posy)) {
			memcpy(SCRCC1(oldcx, i), NEUTRAL, 4);
			memcpy(SCRCC2(oldcx + charpercol - 1, i), NEUTRAL, 4);
		}
	}

	posx = x;
	for (int i = maxcolhints + 1; i < scrh; i++) {
		if (i != BRDROW(posy)) {
			memcpy(SCRCC1(newcx, i), INVERT, 4);
			memcpy(SCRCC2(newcx + charpercol - 1, i),
			       RESET, 4);
		}
	}
}

static void
move(int x, int y)
{
	if (posy + y >= nrows)
		selrow(0);
	else if (posy + y < 0)
		selrow(nrows - 1);
	else
		selrow(posy + y);

	if (posx + x >= ncols)
		selcol(0);
	else if (posx + x < 0)
		selcol(ncols - 1);
	else
		selcol(posx + x);
}

static void
resetterm(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &inittermstate);
}

static void
setupterm(void) {
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
main (int argc, char *argv[])
{
	if (argc == 1) {
		puts("Level scanning and interactive selection will be"
		     "implemented in a later update.");
		exit(1);
	} else
		parselevel(argv[1]);

	setupterm();
	makebrd();
	updatebrd();
	puts("moving...");
	move(0, 0);
	fputs(CLEAR GOTOTOP, stdout);
	DONTIMES(puts(scr[TIMESDONE]), scrh);

	char input;
	int esc = 0;

	while (1) {
		read(STDIN_FILENO, &input, 1);
		if (input == '\003' || input == '\004')
			exit(0);
		else {
			switch (input) {
				BINDKEY('\033', esc = 1);
				BINDKEY('[',
					if (esc)
						puts("Complex inputs are not handled");
					esc = 0);
				BINDKEY('h', move(-1, 0));
				BINDKEY('j', move(0, 1));
				BINDKEY('k', move(0, -1));
				BINDKEY('l', move(1, 0));
				BINDKEY(' ', markcell(posx, posy, 0));
				BINDKEY('x', markcell(posx, posy, 1));
				BINDKEY('m', markcell(posx, posy, 0));
				BINDKEY('q', exit(1));
			}
		}

		updatebrd();
		fputs("\033[H", stdout);
		DONTIMES(puts(scr[TIMESDONE]), scrh);
		if (checkwin()) {
			puts("Looks like we got a WINNER!");
			exit(0);
		}

	}


	return 0;
}
