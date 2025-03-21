#include <stdlib.h>
#include <stdio.h>

static inline unsigned long long int binStoint(const char *s)
{
        unsigned long long int i = 0;
        while (*s) {
                i <<= 1;
                i += *s++ - '0';
        }
        return i;
}

static inline int rowlength(unsigned long long int i)
{
	int length = 0;
	while (i >> length) {
		length += 1;
	}
	return length - 1; /* first 1 allows for leading 0s in row content*/
}

struct Row {
	int nhints;
	int* hints;
	unsigned long long int content;
};

struct Row * makerow(unsigned long long int content) {
	int hints[rowlength(content)];
	int nhints = 0;
	int chain = 0;
	int count = 0;
	while (count < rowlength(content)) {
		if (content >> count & 1) {
			if (!chain)
				nhints++;
			chain++;
			hints[nhints-1] = chain;
		} else
			chain = 0;
		count++;
	}

	struct Row *row = malloc (sizeof *row);
	int *hintsREAL = malloc (sizeof (int[nhints]));
	for (int i = 0; i < nhints; i++)
		hintsREAL[i] = hints[nhints - i - 1];
	row->nhints = nhints;
	row->hints = hintsREAL;
	row->content = content;

	return row;
}



int main ()
{
	unsigned long long int rowcontent = binStoint("110010110");
	struct Row *row = makerow(rowcontent);
	for (int i = 0; i < row->nhints; i++)
		printf("%d\n", *(row->hints + i));
	printf("%d\n%lld\n", row->nhints, row->content);
	return rowlength(rowcontent);
}

//1 -> 1 current hint is incremented
//1 -> 0 increment nhints
//0 -> 1 nothing happens
//0 -> 0 nothing happens
