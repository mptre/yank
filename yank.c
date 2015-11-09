#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define KEY_DOWN  0x100
#define KEY_LEFT  0x101
#define KEY_RIGHT 0x102
#define KEY_UP    0x103

/* Terminal capabilities */
#define T_CLR_EOS             "\033[J"
#define T_CURSOR_INVISIBLE    "\033[?25l"
#define T_CURSOR_UP           "\033[%dA"
#define T_CURSOR_VISIBLE      "\033[?25h"
#define T_ENTER_CA_MODE       "\033[?1049h"
#define T_ENTER_STANDOUT_MODE "\033[7m"
#define T_EXIT_CA_MODE        "\033[?1049l"
#define T_EXIT_STANDOUT_MODE  "\033[0m"
#define T_KEY_DOWN            "\033[B"
#define T_KEY_LEFT            "\033[D"
#define T_KEY_RIGHT           "\033[C"
#define T_KEY_UP              "\033[A"
#define T_RESTORE_CURSOR      "\033[u"
#define T_SAVE_CURSOR         "\033[s"

#define CONTROL(c) (c ^ 0x40)
#define MIN(x, y) (x < y ? x : y)

static const char *delim = " ";

static const char **yankargv;

static struct {
	size_t size;
	size_t nmemb;
	char *v;
} in;

static struct {
	size_t size;
	size_t nmemb;
	size_t *v;
} lines;

static struct {
	size_t nmemb;
	const char *v;
} sel;

static struct {
	int in;
	int out;
	int ca;              /* use alternate screen */
	unsigned int height;
	unsigned int width;
	struct termios attr;
} tty;

static void args(int, const char **);
static int field(const char *, size_t, int, size_t *, size_t *);
static void input(void);
static int intersect(int, int, int, int);
static int isdelim(const char *);
static ssize_t rune(const char *s, size_t, int);
static void yank(void);

static void tdraw(const char *, size_t, int, int);
static void tend(void);
static int tgetc(void);
static void tmain(void);
static void tprintf(const char *, int);
static void tputs(const char *);
static void treset(void);
static void tsetup(void);
static void twrite(const char *, size_t);

static ssize_t xwrite(int, const char *, size_t);

void
args(int argc, const char **argv)
{
	size_t n;
	int c, i;

	while ((c = getopt(argc, (char * const *) argv, "lvxd:")) != -1) {
		switch (c) {
		case 'd':
			delim = optarg;
			break;
		case 'l':
			delim = "";
			break;
		case 'v':
			puts("yank " VERSION);
			exit(0);
		case 'x':
			tty.ca = 1;
			break;
		default:
		usage:
			fputs("usage: yank "
			      "[-lx | -v] "
			      "[-d delim] "
			      "[-- command [argument ...]]\n", stderr);
			exit(1);
		}
	}
	if (optind < argc && strncmp(argv[optind - 1] , "--", 3))
		goto usage;

	/* Ensure space for yank command and null terminator. */
	n = (argc - optind + 2)*sizeof(const char *);
	yankargv = malloc(n);
	if (!yankargv)
		perror("malloc");
	memset(yankargv, 0, n);
	yankargv[0] = YANKCMD;
	for (i = optind; i < argc; i++)
		yankargv[i - optind] = argv[i];
}

/*
 * Returns the next unicode rune offset relative to s + offset in the direction
 * given by inc.
 */
ssize_t
rune(const char *s, size_t offset, int inc)
{
	ssize_t i;

	for (i = offset + inc;
	     i + inc >= 0 && s[i] && (s[i] & 0xC0) == 0x80;
	     i += inc)
		/* NOP */;

	return i - offset;
}

/*
 * Writes the next field to start and stop relative to s + offset in the
 * direction given by inc.
 */
int
field(const char *s, size_t offset, int inc, size_t *start, size_t *stop)
{
	ssize_t i, j, r;

	r = 0;
	i = offset;
	for (;;) {
		if (i < 0 || !s[i])
			return 0;

		if (!isdelim(&s[i]))
			break;
		r = rune(s, i, inc);
		i += r;
	}
	i -= r < -1 ? r - inc : 0;

	j = i;
	for (;;) {
		r = rune(s, j, inc);
		j += r;
		if (j < 0 || !s[j] || isdelim(&s[j]))
			break;
	}
	j -= r < -1 ? r : inc;

	*start = i;
	*stop  = j;
	return 1;
}

void
input(void)
{
	int n;

	in.size = BUFSIZ;
	in.nmemb = 0;
	in.v = malloc(in.size);
	if (!in.v)
		perror("malloc");

	for (;;) {
		n = read(0, in.v + in.nmemb, in.size - in.nmemb);
		if (n < 0) {
			perror("read");
			break;
		}
		if (!n)
			break;
		in.nmemb += n;

		if (in.size < in.nmemb + 1) {
			in.size *= 2;
			in.v = realloc(in.v, in.size);
			if (!in.v)
				perror("realloc");
		}
	}
	memset(in.v + in.nmemb, 0, in.size - in.nmemb);
}

/*
 * Returns nonzero if [x1, x2] and [y1, y2] intersects.
 */
int
intersect(int x1, int y1, int x2, int y2)
{
	int m;

	while (x1 <= y1) {
		m = x1 + (y1 - x1)/2;
		if (x2 <= m && m <= y2)
			return 1;
		if (m < x2)
			x1 = m + 1;
		else
			y1 = m - 1;
	}

	return 0;
}

int
isdelim(const char *s)
{
	size_t i, n;

	if (*s == '\n' || *s == '\r' || *s == '\t')
		return 1;

	n = rune(s, 0, 1);
	for (i = 0; delim[i]; i += rune(delim, i, 1)) {
		if (!strncmp(s, &delim[i], n))
			return 1;
	}
	return 0;
}

void
yank(void)
{
	int fd[2];
	int s;
	pid_t pid;

	if (!sel.v)
		exit(1);

	if (!isatty(1)) {
		if (xwrite(1, sel.v, sel.nmemb) < 0)
			perror("write");
		exit(0);
	}

	if (pipe(fd) < 0)
		perror("pipe");
	if (dup2(fd[0], 0) < 0)
		perror("dup2");
	if (close(fd[0]) < 0)
		perror("close");
	if (xwrite(fd[1], sel.v, sel.nmemb) < 0)
		perror("write");
	if (close(fd[1]) < 0)
		perror("close");
	pid = fork();
	switch (pid) {
	case -1:
		perror("fork");
		exit(1);
	case 0:
		execvp(yankargv[0], (char * const *) yankargv);
		s = errno;
		perror(yankargv[0]);
		_exit(126 + (s == ENOENT));
	default:
		waitpid(pid, &s, 0);
		if (WIFSIGNALED(s))
			exit(128 + WTERMSIG(s));
		if (WIFEXITED(s))
			exit(WEXITSTATUS(s));
	}
}

ssize_t
xwrite(int fd, const char *s, size_t nmemb)
{
	ssize_t r;
	size_t n = nmemb;

	do {
		r = write(fd, s, n);
		if (r < 0)
			return r;
		n -= r;
		s += r;
	} while (n);

	return nmemb;
}

void
tdraw(const char *s, size_t nmemb, int start, int stop)
{
	twrite(s, start);
	tputs(T_ENTER_STANDOUT_MODE);
	twrite(s + start, stop - start + 1);
	tputs(T_EXIT_STANDOUT_MODE);
	twrite(s + stop + 1, nmemb - stop);
}

void
tprintf(const char *format, int x)
{
	char s[32];
	int n;

	n = snprintf(s, sizeof(s), format, x);

	twrite(s, n);
}

void
tputs(const char *s)
{
	size_t n = strlen(s);

	twrite(s, n);
}

void
twrite(const char *s, size_t nmemb)
{
	if (xwrite(tty.out, s, nmemb) < 0)
		perror("write");
}

void
tsetup(void)
{
	struct termios attr;
	struct winsize ws;
	char *s1, *s2;
	size_t d, n;

	tty.in = open("/dev/tty", O_RDONLY);
	if (!tty.in)
		perror("open");
	if (ioctl(tty.in, TIOCGWINSZ, &ws) < 0) {
		perror("ioctl");
		tty.height = 24;
		tty.width = 80;
	} else {
		tty.height = ws.ws_row;
		tty.width = ws.ws_col;
	}

	lines.size = tty.height + 1;
	lines.v = calloc(lines.size, sizeof(size_t));
	if (!lines.v)
		perror("calloc");
	lines.v[lines.nmemb++] = 0;
	n = MIN(tty.height*tty.width, in.nmemb);
	s1 = s2 = in.v;
	while (lines.v[lines.nmemb] < in.nmemb && lines.nmemb < lines.size) {
		if (s1 == s2) {
			s2 = memchr(s1, '\n', n);
			if (s2) {
				if (lines.size - lines.nmemb > 1)
					s2++;
			} else {
				s2 = in.v + in.nmemb;
			}
		}

		d = MIN(s2 - s1, tty.width);
		lines.v[lines.nmemb++] += d;
		lines.v[lines.nmemb] = lines.v[lines.nmemb - 1];
		n -= d;
		s1 += d;
	}
	memset(in.v + lines.v[lines.nmemb], 0, in.nmemb - lines.v[lines.nmemb]);

	tcgetattr(tty.in, &tty.attr);
	memcpy(&attr, &tty.attr, sizeof(struct termios));
	attr.c_lflag &= ~(ICANON|ECHO|ISIG);
	tcsetattr(tty.in, TCSANOW, &attr);

	tty.out = open("/dev/tty", O_WRONLY);
	if (!tty.out)
		perror("open");

	if (tty.ca)
		tputs(T_ENTER_CA_MODE);
	tputs(T_CURSOR_INVISIBLE);
	/* Emit the number of lines and save the cursor position. */
	for (n = 0; n < lines.nmemb; n++)
		tputs("\n");
	tputs(T_SAVE_CURSOR);
	treset();
}

void
tend(void)
{
	treset();
	tputs(T_CLR_EOS);
	tputs(T_CURSOR_VISIBLE);
	if (tty.ca)
		tputs(T_EXIT_CA_MODE);
	tcsetattr(tty.in, TCSANOW, &tty.attr);
	close(tty.in);
	close(tty.out);
}

void
treset(void)
{
	tputs(T_RESTORE_CURSOR);
	tprintf(T_CURSOR_UP, lines.nmemb);
}

int
tgetc(void)
{
	char buf[3];
	ssize_t n;

	n = read(tty.in, buf, sizeof(buf));
	if (n < 0) {
		perror("read");
		return 0;
	}
	if (n > 2) {
		if (!strncmp(T_KEY_UP, buf, n))
			return KEY_UP;
		if (!strncmp(T_KEY_RIGHT, buf, n))
			return KEY_RIGHT;
		if (!strncmp(T_KEY_DOWN, buf, n))
			return KEY_DOWN;
		if (!strncmp(T_KEY_LEFT, buf, n))
			return KEY_LEFT;
		return 0;
	}

	return buf[0];
}

void
tmain(void)
{
	size_t d, o, start, stop, s, t, x, y;
	int c, i;

	if (field(in.v, 0, 1, &start, &stop))
		tdraw(in.v, lines.v[lines.nmemb - 1], start, stop);
	else
		twrite(in.v, lines.v[lines.nmemb - 1]);
	for (;;) {
		c = tgetc();
		switch (c) {
		case '\n':
			sel.nmemb = stop - start + 1;
			sel.v = in.v + start;
			/* FALLTHROUGH */
		case CONTROL('C'):
		case CONTROL('D'):
			return;
		case CONTROL('A'):
			o = 0;
			/* FALLTHROUGH */
		if (0) {
		case CONTROL('N'):
		case KEY_RIGHT:
			o = stop + rune(in.v, stop, 1);
		}
			if (!field(in.v, o, 1, &start, &stop))
				continue;
			break;
		case CONTROL('E'):
			o = lines.v[lines.nmemb - 1] - 1;
			/* FALLTHROUGH */
		if (0) {
		case CONTROL('P'):
		case KEY_LEFT:
			o = start + rune(in.v, start, -1);
		}
			if (!field(in.v, o, -1, &stop, &start))
				continue;
			break;
		case KEY_DOWN:
			i = 0;
			while (lines.v[i + 1] <= stop)
				i++;
			d = lines.v[i];
			o = lines.v[++i];
			if (!field(in.v, o, 1, &s, &t))
				continue;
			while (lines.v[i] < s)
				i++;
		if (0) {
		case KEY_UP:
			i = 0;
			while (lines.v[i + 1] <= start)
				i++;
			d = o = lines.v[i];
			o += rune(in.v, o, -1);
			if (!field(in.v, o, -1, &t, &s))
				continue;
			while (lines.v[i] > s)
				i--;
		}
			o = lines.v[i];
			for (;;) {
				if (!field(in.v, o, 1, &x, &y)
				    || y >= lines.v[i + 1])
					break;

				s = x;
				t = y;
				if (intersect(start - d, stop - d,
					      s - lines.v[i], t - lines.v[i]))
					break;
				o = t + rune(in.v, t, 1);
			}
			start = s;
			stop = t;
			break;
		default:
			continue;
		}
		treset();
		tdraw(in.v, lines.v[lines.nmemb - 1], start, stop);
	}
}

int
main(int argc, const char *argv[])
{
	args(argc, argv);
	input();
	tsetup();
	tmain();
	tend();
	yank();
	return 0;
}
