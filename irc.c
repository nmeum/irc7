#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>

char help[] =
"cmd		explanation/example\n"
"--------------------------------------------\n"
"/m		privmsg #chan/nick message\n"
"/M		mode #chan +nt\n"
"/j		join #chan\n"
"/p		part #chan\n"
"/q		send parameters raw to the server\n"
"/l		list #chan\n"
"/n		nick newnick\n"
"/N		notice #chan/nick message\n"
"/t		set victim\n"
"/T		topic #chan newtopic\n"
"/W		whois nick\n"
"/w		who nick (a shorter whois)\n";

#define NPAR	14

enum state { Cmd, Prefix, Middle, Trail, Ok };

typedef struct handler Handler;

struct handler {
	char *cmd;
	int (*fun)(int fd, char *pre, char *cmd, char *par[]);
};

QLock lck;
int server_in;
int server_out;
int scr;
char *victim;
char *nick;
char *unick;
int inacme;		/* running in acme? */
int	linewidth; 	/* terminal width in # of characters */

int replay;		/* just print the log ma'am */

void setwintitle(char *chan);

int rtcs(int fd, char *cset);
int wtcs(int fd, char *cset);
int follow(int fd);
void getwidth(void);	/* establish the width of the terminal, from mc.c */

int pmsg(int fd, char *pre, char *cmd, char *par[]);
int ntc(int fd, char *pre, char *cmd, char *par[]);
int generic(int fd, char *pre, char *cmd, char *par[]);
int misc(int fd, char *pre, char *cmd, char *par[]);
int numeric(int fd, char *pre, char *cmd, char *par[]);

Handler handlers[] = {
	{"PRIVMSG", pmsg},
	{"NOTICE", ntc},
	{"JOIN", misc},
	{"PART", misc},
	{"MODE", misc},
	{"QUIT", misc},
	{"TOPIC", misc},
	{"332", numeric},
	{"333", numeric},
	{"352", numeric},
	{"315", numeric},
	{"311", numeric},
	{"319", numeric},
	{"312", numeric},
	{"320", numeric},
	{"317", numeric},
	{"318", numeric},
	{nil, nil}
};

int srvparse(char *line, char **pre, char **cmd, char *par[], int npar);
int usrparse(char *ln, char *cmd, char *par[], int npar);

void
usage(void)
{
	char usage[] = "usage: irc [-S] [-c charset] [-t victim] [-b lines] [-n nick] [-r file] [/srv/irc] [/tmp/irc]\n";
	write(1, usage, sizeof(usage)-1);
	exits("usage");
}

void
setwintitle(char *chan)
{
	int fd;

	// if /dev/acme doesn't exist you're probably in a normal window
	// otherwise you can't set the title in a windowless window
	if ((fd = open("/dev/acme/ctl", OWRITE)) >= 0) {
		fprint(fd, "name -IRC/%s/guide\n", chan);
		close(fd);
		inacme = 1;
	} else if ((fd = open("/dev/label", OWRITE)) >= 0) {
		fprint(fd, "%s", chan);
		close(fd);
	}
}

/* try to find out whether we're running in acme's win -e */
int
testacme(void)
{
	return access("/dev/acme", OREAD) >= 0 ? 1 : 0;
}

void
usrin(void)
{
	char *line, *p;
	char *par[2];
	char cmd;
	int n, i;

	Biobuf kbd;
	Binit(&kbd, 0, OREAD);
	while ((line = Brdstr(&kbd, '\n', 0)) != nil) {
		n = utflen(line);
		if(!inacme) {
			p = malloc(n);
			for (i = 0; i < n; ++i)
				p[i] = '\b';
			write(scr, p, i);
			free(p);
		}
		qlock(&lck);
		if (!usrparse(line, &cmd, par, 2)) {
			switch(cmd) {
			case 'q':	/* quote, just send the params ... */
				if(par[0]) {
					fprint(server_out, "%s %s\r\n", par[0], par[1] ? par[1] : "");
				} else {
					fprint(scr, "/q %s %s: not enough arguments\n", par[0], par[1]);
				}
				break;
			case 'M':
				if(par[0] && par[1]) {
					fprint(server_out, "MODE %s %s\r\n", par[0], par[1]);
				} else {
					fprint(scr, "/M %s %s: not enough arguments\n", par[0], par[1]);
				}
				break;
			case 'm':
				if(par[0] && par[1]) {
					fprint(server_out, "PRIVMSG %s :%s\r\n", par[0], par[1]);
				} else {
					fprint(scr, "/m %s %s: not enough arguments\n", par[0], par[1]);
				}
				break;
			case 't':
				if(par[0] != nil) {
					free(victim);
					victim = strdup(par[0]);
					setwintitle(par[0]);
				}
				fprint(scr, "*** default victim set to '%s'\n", par[0]);
				break;
			case 'T':
				if(par[0] == nil) 
					fprint(server_out, "TOPIC %s\r\n", victim);
				else if(par[1] == nil)
					fprint(server_out, "TOPIC %s\r\n", par[0]);
				else
					fprint(server_out, "TOPIC %s :%s\r\n", par[0], par[1]);
				break;
			case 'j':
				fprint(server_out, "JOIN %s\r\n", par[0] == nil ? victim : par[0]);
				break;
			case 'p':
				fprint(server_out, "PART %s\r\n", par[0] == nil ? victim : par[0]);
				break;
			case 'n':
				if(par[0] != nil) {
					fprint(server_out, "NICK %s\r\n", par[0]);
					free(nick);
					nick = strdup(par[0]);
					unick = strdup(par[0]);
				} else {
					fprint(scr, "%s", help);
				}
				break;
			case 'N':
				if(par[1] != nil)
					fprint(server_out, "NOTICE %s :%s\r\n", par[0] == nil ? victim : par[0], par[1]);
				break;
			case 'W':
				fprint(server_out, "WHOIS %s %s\r\n", par[0] == nil ? victim : par[0], par[0]);
			case 'w':
				fprint(server_out, "WHO %s\r\n", par[0] == nil ? victim : par[0]);
				break;
			case 'l':
				fprint(server_out, "LIST %s\r\n", par[0] == nil ? victim : par[0]);
				break;
			case 'L':
				fprint(server_out, "NAMES %s\r\n", par[0] == nil ? victim : par[0]);
				break;
			case 'f':
				break;
			case 'h':
			case 'H':
				fprint(scr, "%s", help);
				break;
			}
		} else {
			fprint(scr, "%s", help);
		}
		qunlock(&lck);
		free(line);
	}
	exits(0);
}

void
srvin(void)
{
	char *line;
	char *pre, *cmd, *par[NPAR];
	Biobuf srv;
	Binit(&srv, server_in, OREAD);

	while ((line = Brdstr(&srv, '\n', 0)) != nil) {
		if (!srvparse(line, &pre, &cmd, par, NPAR)) {
			Handler *hp = handlers;
			qlock(&lck);
			while (hp->cmd != nil) {
				if (!strcmp(hp->cmd, cmd)) {
					hp->fun(server_out, pre, cmd, par);
					break;
				}
				++hp;
			}
			if (hp->cmd == nil)
				generic(server_out, pre, cmd, par);
			qunlock(&lck);
		}
		free(line);
	}
}

void
replayfile(void)
{
	char *line;
	char *pre, *cmd, *par[NPAR];
	Biobuf srv;
	Binit(&srv, server_in, OREAD);

	while ((line = Brdstr(&srv, '\n', 0)) != nil) {
		if (!srvparse(line, &pre, &cmd, par, NPAR)) {
			Handler *hp = handlers;
			qlock(&lck);
			while (hp->cmd != nil) {
				if (!strcmp(hp->cmd, cmd)) {
					hp->fun(server_out, pre, cmd, par);
					break;
				}
				++hp;
			}
			if (hp->cmd == nil)
				generic(server_out, pre, cmd, par);
			qunlock(&lck);
		}
		free(line);
	}
}

/* 
 * display the last N lines from the conversation
 * if we have a default target only the conversation with
 * that target will be shown
 */
void
seekback(int fd, int lines)
{
	Biobuf srv;
	int found = 0, off;
	char c, *line;

	if(lines < 0)
		return;

	Binit(&srv, fd, OREAD);

	Bseek(&srv, -2, 2);
	while(((off = Boffset(&srv)) > 0) && found < lines) {
		c = Bgetc(&srv);
		Bungetc(&srv);
		if(c == '\n') {
			Bseek(&srv, 1, 1);
			line = Brdstr(&srv, '\n', '\0');
			if(victim) {
				if(cistrstr(line, victim))
					found++;
			} else {
				found++;
			}
			free(line);
		}
		Bseek(&srv, off-1, 0);
	}

	Bterm(&srv);
}

void
main(int argc, char *argv[])
{
	char *charset = nil;
	char buf[32], buf2[32], *out = nil, *in = nil;
	char *user;
	int olduser = 0;
	char *arg;
	int sb = 10;	/* how many lines are we displaying initially */
	int uipid;
	int scroll = 1;
	int wctlfd;

	unick = nil;
	user = getuser();
	ARGBEGIN {
	case 't':
		victim = strdup(EARGF(usage()));
		break;
	case 'b':
		arg = ARGF();
		if(arg != nil && arg[0] != '-') 
			sb = atoi(arg);
		else 
			sb = 0;	/* show all text */
		break;
	case 'c':
		charset = EARGF(usage());
		break;
	case 'r':
		replay = 1;
		sb = 0;
		break;
	case 'o':
		olduser = 1;
		break;
	case 'n':
		unick = strdup(EARGF(usage()));
		break;
	case 'S':
		scroll = 0;
		break;
	default:
		usage();
	} ARGEND;

	switch(argc) {
	case 0:
		break;
	case 1:
		if(replay)
			in = argv[0];
		else 
			out = argv[0];
		break;
	case 2:
		out = argv[0];
		in = argv[1];
		break;
	default:
		usage();
	}

	if(out == nil && in == nil){
		if(olduser == 1 && strlen(user) > 4)
			user[4] = '\0';
		snprint(buf, sizeof buf, "/srv/%sirc", user);
		snprint(buf2, sizeof buf, "/tmp/%sirc", user);
		out = strdup(buf);
		in = strdup(buf2);
	}
	if(unick == nil)
		unick = strdup(user);

	if(scroll == 1){
		if((wctlfd = open("/dev/wctl", OWRITE)) < 0)
			print("irc: could not open /dev/wctl: %r\n");
		else {
			fprint(wctlfd, "scroll\n");
			close(wctlfd);
		}
	}

	if(!replay && (server_out = open(out, OWRITE)) < 0)
			sysfatal("open write: %s %r", out);
	if ((server_in = open(in, OREAD)) < 0)
			sysfatal("open read: %s %r", in);

	inacme = testacme();
	getwidth();

	if(sb)
		seekback(server_in, sb);

	while(read(server_in, buf, 1) > 0)
		if(*buf == '\n')
			break;

	if(victim && cistrncmp(victim, "MSGS", 4)){
		setwintitle(victim);
		fprint(server_out, "JOIN %s\r\n", victim);
	}
	scr = 1;

	server_in = follow(server_in);

	if (charset != nil && strcmp(charset, "utf")) {
		server_out = wtcs(server_out, charset);
		server_in = rtcs(server_in, charset);
	}

	if(replay) {
		replayfile();
	} else {
		if ((uipid = rfork(RFPROC|RFFDG|RFMEM)) == 0)
			srvin();

		usrin();

		postnote(PNPROC, uipid, "kill");
		while (waitpid() != uipid);
	}

	exits(0);
}

int
wtcs(int fd, char *cset)
{
	int totcs[2];
	int pid;

	pipe(totcs);

	pid = fork();

	if (pid == 0) {
		dup(totcs[0], 0);
		dup(fd, 1);
		close(totcs[1]);
		execl("/bin/tcs", "tcs", "-f", "utf", "-t", cset, nil);
		exits("execfailure");
	}
	close(totcs[0]);

	return totcs[1];
}

int
rtcs(int fd, char *cset)
{
	int fromtcs[2];
	int pid;

	pipe(fromtcs);

	pid = fork();

	if (pid == 0){
		dup(fromtcs[1], 1);
		dup(fd, 0);
		close(fromtcs[0]);
		execl("/bin/tcs", "tcs", "-f", cset, "-t", "utf", nil);
		exits("execfailure");
	}
	close(fromtcs[1]);

	return fromtcs[0];
}

int
follow(int fd)
{
	int p[2], pid;
	long n;
	char buf[1024];
	Dir *dp;

	pipe(p);

	pid = fork();
	if (pid == 0){
		dup(p[1], 1);
		dup(fd, 0);
		close(p[0]);
		for(;;){
			while((n = read(0, buf, sizeof(buf))) > 0)
				write(1, buf, n);
			sleep(1000);
			dp = dirfstat(0);
			free(dp);
		}
	}
	close(p[1]);

	return p[0];
}

char *
prenick(char *p)
{
	char *n = p;
	if (p != nil) {
		while (*p != '\0' && *p != '!') ++p;
		if (*p != '!')
			n = nil;
		*p = '\0';
	}
	return n;
}

int
pmsg(int, char *pre, char *, char *par[])
{
	int n = 0;
	char buf[8192];
	//char *c, *tc;
	char *c;
	int privmsg = 0;

/*
 *	if sent to victim, or comes from victim to non-channel, print.
 *	otherwise bail out.
 */
	pre = prenick(pre);
	if(victim) {
		if((cistrncmp(victim, "MSGS", 4) == 0) && *par[0] != '#') {
			/* catch-all for messages, fall through */
		
		} else if(cistrcmp(par[0], victim)){
			if(*par[0] != '#' && cistrcmp(victim, "privmsg") == 0){
				if(cistrcmp(par[0], unick) == 0 &&
					cistrcmp(par[0], victim) != 0)
					privmsg = 1;
			} else if(!pre || cistrcmp(pre, victim) || *par[0] == '#')
				return 0;
		}
	}

	if(!pre)
		sprint(buf, "(%s) ⇐ %s\n", par[0], par[1]);
	else if(privmsg == 1)
		sprint(buf, "(privmsg) %s → %s\n", pre, par[1]);
	else if(*par[0] != '#')
		sprint(buf, "(%s) ⇒ %s\n", pre, par[1]);
	else
		sprint(buf, "(%s) %s → %s\n", par[0], pre, par[1]);
	
	c = buf;
	n += fprint(scr, "%s", c);
	return n;
}

int
ntc(int, char *pre, char *, char *par[])
{
	int n;

/*
 *	if sent to victim, or comes from victim to non-channel, print.
 *	otherwise bail out.
 */
	pre = prenick(pre);
	if(victim && cistrcmp(par[0], victim))
		if(!pre || cistrcmp(pre, victim) || *par[0] == '#')
			return 0;

	if(!pre)
		n = fprint(scr, "[%s] ⇐\t%s\n", par[0], par[1]);
	else if(*par[0] != '#')
		n = fprint(scr, "[%s] ⇒\t%s\n", pre, par[1]);
	else
		n = fprint(scr, "[%s] %s →\t%s\n", par[0], pre, par[1]);
	return n;
}

int
generic(int, char *pre, char *cmd, char *par[])
{
	int i = 0, r;
	char *n = prenick(pre);

/*
 *	don't print crud on screens with victim set
 */
	if(victim)
		return 0;

	if (n != nil) 
		r = fprint(scr, "%s (%s)\t", cmd, n);
	else
		r = fprint(scr, "%s (%s)\t", cmd, par[i++]);

	for (; par[i] != nil; ++i)
		r += fprint(scr, " %s", par[i]);

	r += fprint(scr, "\n");

	return r;
}

int
misc(int, char *pre, char *cmd, char *par[])
{
	int i = 0, r;
	char *n = prenick(pre);

	if(victim != nil)
		if(cistrcmp(victim, "privmsg") == 0)
			return 0;
	if(cistrcmp(cmd,"QUIT"))
		if(victim && par[0] && cistrcmp(par[0], victim))
			return 0;	

	if (n != nil) 
		r = fprint(scr, "%s (%s)\t", cmd, n);
	else
		r = fprint(scr, "%s %s\t", cmd, par[i++]);

	for (; par[i] != nil; ++i)
		r += fprint(scr, " %s", par[i]);

	r += fprint(scr, "\n");

	return r;
}

int
numeric(int, char *pre, char *cmd, char *par[])
{
	int i = 0, r;
	char *n = prenick(pre);

	if(victim != nil)
		if(cistrcmp(victim, "privmsg") == 0)
			return 0;

	if(victim && par[1] && cistrcmp(par[1], victim))
		return 0;

	if (n != nil) 
		r = fprint(scr, "%s (%s)\t", cmd, n);
	else
		r = fprint(scr, "%s (%s)\t", cmd, par[i++]);

	for (; par[i] != nil; ++i)
		r += fprint(scr, " %s", par[i]);

	r += fprint(scr, "\n");

	return r;
}

int
usrparse(char *ln, char *cmd, char *par[], int npar)
{
	enum state st = Cmd;
	int i;

	for(i = 0; i < npar; i++)
		par[i] = nil;

	if (ln[0] == '/' && npar >= 2) { 
		*cmd = ln[1];
		for (i = 1; ln[i] != '\0'; ++i) {
			switch(st) {
			case Cmd:
				if (ln[i] == ' ') {
					ln[i] = '\0';
					par[0] = ln+i+1;
					st = Middle;
				} else if(ln[i] == '\n') {
					/* enable commands with no arguments */
					ln[i] = '\0';
					par[0] = nil;
					par[1] = nil;
					st = Ok;
				}
				break;
			case Middle:
				if (ln[i] == '\r' || ln[i] == '\n') {
					ln[i] = '\0';
					st = Ok;
				}
				if (ln[i] == ' ') {
					ln[i] = '\0';
					par[1] = ln+i+1;
					st = Trail;
				}
				break;
			case Trail:
				if (ln[i] == '\r' || ln[i] == '\n') {
					ln[i] = '\0';
					st = Ok;
				}
				break;
			case Ok:
				if (ln[i] == '\r' || ln[i] == '\n')
					ln[i] = '\0';
				break;
			}
		}
	} else {	/* send line to victim by default */
		st = Ok;
		*cmd = 'm';
		for (i = 0; ln[i] != '\0'; ++i)
			if (ln[i] == '\r' || ln[i] == '\n')
				ln[i] = '\0';
		par[0] = victim;
		par[1] = ln;
	}
	return st == Ok ? 0 : 1;
}

int
srvparse(char *line, char **pre, char **cmd, char *par[], int npar)
{
	int i;
	char *p;
	enum state st = Cmd;

	*pre = *cmd = nil;

	for (*cmd = p = line, i = 0; *p != '\0'; ++p) {
		switch (st) {
		case Cmd:
			if (*p == ':') {
				*p = '\0';
				*pre = p + 1;
				st = Prefix;
			} else if (*p == ' ') {
				*p = '\0';
				par[i] = p + 1;
				st = Middle;
			}
			break;
		case Prefix:
			if (*p == ' ') {
				*p = '\0';
				*cmd = p + 1;
				st = Cmd;
			}
			break;
		case Middle:
			if (*p == '\r' || *p == '\n') {
				*p = '\0';
				st = Ok;
			} else if (*p == ':') {
				*p = '\0';
				par[i] = p + 1;
				st = Trail;
			} else if (*p == ' ') {
				*p = '\0';
				i = (i + 1) % npar;
				par[i] = p + 1;
				st = Middle;
			}
			break;
		case Trail:
			if (*p == '\r' || *p == '\n') {
				*p = '\0';
				st = Ok;
			}
			break;
		case Ok:
			*p = '\0';
			break;
		}
	}
	par[(i + 1) % npar] = nil;
	return st == Ok ? 0 : 1;
}

void	// this garbage is bullshit
getwidth(void)
{
	Font *font;
	int n, fd, mintab;
	char buf[128], *f[10], *p;

	if(inacme){
		if((fd = open("/dev/acme/ctl", OREAD)) < 0)
			return;
		n = read(fd, buf, sizeof buf-1);
		close(fd);
		if(n <= 0)
			return;
		buf[n] = 0;
		n = tokenize(buf, f, nelem(f));
		if(n < 7)
			return;
		if((font = openfont(nil, f[6])) == nil)
			return;
		mintab = stringwidth(font, "0");
		linewidth = atoi(f[5]);
		linewidth = linewidth/mintab;
		return;
	}

	if((p = getenv("font")) == nil)
		return;
	if((font = openfont(nil, p)) == nil)
		return;
	if((fd = open("/dev/window", OREAD)) < 0)
		return;

	n = read(fd, buf, 5*12);
	close(fd);

	if(n < 5*12)
		return;

	buf[n] = 0;

	/* window stucture:
		4 bit left edge
		1 bit gap
		12 bit scrollbar
		4 bit gap
		text
		4 bit right edge
	*/
//	linewidth = atoi(buf+3*12) - atoi(buf+1*12) - (4+1+12+4+4);
	linewidth = 20000;  // update this when screens are more than 20,000 pixels wide
	mintab = stringwidth(font, "0");
	linewidth = linewidth/mintab;
}
