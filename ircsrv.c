#include <u.h>
#include <libc.h>
#include <auth.h>
#include <libsec.h>

char *post;
char *file;
int ircfd = -1;	// the irc server
int logfd;
int enctls = 0; // ssl/tls
QLock lck;
char *channels[256];
int tchans;

char *server;
char *passwd;
char *nickname;
char *realname;
char *username;
char *mode = "foo";
char *unused = "bar";

void ircsrv(void);
void logger(void);
void die(void*, char*);
void reconnect(void);
void joinhandler(char*);

void
usage(void)
{
	fprint(2, "usage: %s [-e] [-s service] [-f file] [-p pass] nickname [net!]ircserver[!port]\n", argv0);
	exits("usage");
}


void
killall(void)
{
	postnote(PNGROUP, getpid(), "quit");
	while(waitpid() != -1)
		;
	remove(post);
	exits(nil);
}

void
die(void *, char *)
{
	killall();
}

void
main(int argc, char *argv[])
{
	char *tmp;
	int p[2], fd;

	ARGBEGIN{
	case 'f':
		file = EARGF(usage());
		break;
	case 's':
		post = EARGF(usage());
		break;
	case 'r':
		realname = EARGF(usage());
		break;
	case 'e':
		enctls = 1;
		break;
	case 'p':
		passwd = EARGF(usage());	
		/* try to obfuscate the password so ps -a won't see it */
		tmp = passwd;
		passwd = smprint("%s", tmp);
		if(passwd) 
			memset(tmp, '\0', strlen(tmp));
		else
			passwd = tmp;
		break;
	default:
		usage();
	}ARGEND;

	if(argc < 2)
		usage();


	nickname = argv[0];
	server = argv[1];

	username = getuser();

	if(strlen(username) > 4)
		username[4] = '\0';


	if(post == nil)
		post = smprint("/srv/%sirc", username);
	else
		post = smprint("/srv/%s", post);

	if(file == nil)
		file = smprint("/tmp/%sirc", username);

	if((logfd = create(file, OWRITE, 0600 | DMAPPEND)) < 0)
		sysfatal("create(%s): %r", file);

	if((fd = create(post, OWRITE, 0600)) < 0)
		sysfatal("create(%s): %r", post);
	if(pipe(p) == -1)
		sysfatal("pipe: %r");
	fprint(fd, "%d", p[1]);
	close(fd);
	close(p[1]);
	close(0);
	close(1);
	close(2);
	dup(p[0], 0);

	if(rfork(RFMEM|RFFDG|RFREND|RFPROC|RFNOTEG|RFCENVG|RFNOWAIT) == 0) {
		notify(die);
		reconnect();
		switch(rfork(RFPROC|RFMEM)){
		case -1:
			sysfatal("rfork: %r");
		case 0:
			notify(die);
			logger();
			break;
		default:
			ircsrv();
			break;
		}
	}
	exits(nil);
}

long
readln(int fd, void *vp, long len)
{
	char *b = vp;
	while(len > 0 && read(fd, b, 1) > 0){
		if(*b++ == '\n')
			break;
		len--;
	}
	return b - (char*)vp;
}

void
reregister(void)
{
	int n;
	char nbuf[32];

	strncpy(nbuf, nickname, sizeof(nbuf) - 2);
	switch(nbuf[strlen(nbuf) - 1]) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
		nbuf[strlen(nbuf) - 1]++;
		break;
	case '9':
		qlock(&lck);
		fprint(logfd, "can not register nick, bailing out\n");
		qunlock(&lck);
		die(nil, nil);
	default:
		n = strlen(nbuf);
		nbuf[n] = '0';
		nbuf[n+1] = '\0';
		break;
	}
	qlock(&lck);
	fprint(ircfd, "NICK %s\r\n", nbuf);
	fprint(logfd, "NICK %s\r\n", nickname);
	qunlock(&lck);
}

void
reconnect(void)
{
	TLSconn *conn;
	int i;
	if(ircfd >= 0)
		close(ircfd);
	if((ircfd = dial(netmkaddr(server, "tcp", "6667"), nil, nil, nil)) < 0)
		sysfatal("dial %r");
	if(enctls > 0) {
		conn = (TLSconn *)mallocz(sizeof *conn, 1);
        	ircfd = tlsClient(ircfd, conn);
		if (ircfd < 0) { sysfatal ("tls: %r"); }
	}
	if(passwd && strcmp(passwd, ""))
		fprint(ircfd, "PASS %s\r\n", passwd);
	fprint(ircfd, "USER %s %s %s :%s\r\n",
		username, mode, unused, realname);
	fprint(ircfd, "NICK %s\r\n", nickname);
	for(i = 0; i < tchans; i++)
		fprint(ircfd, "JOIN %s\r\n", channels[i]);
}


void
logger(void)
{
	char buf[513];
	char *f[3];
	long n;

	for(;;){
		while((n = readln(ircfd, buf, sizeof(buf)-1)) > 0){
			write(logfd, buf, n);
			buf[n] = 0;
			n = tokenize(buf, f, nelem(f));
			if(n == 3 && *f[0] == ':' && !cistrcmp(f[1], "PING")){
				qlock(&lck);
				fprint(ircfd, "PONG %s\r\n", f[2]);
				fprint(logfd, "PONG %s\r\n", f[2]);
				qunlock(&lck);
			} else if(n == 2 && !cistrcmp(f[0], "PING")){
				qlock(&lck);
				fprint(ircfd, "PONG %s\r\n", f[1]);
				fprint(logfd, "PONG %s\r\n", f[1]);
				qunlock(&lck);
			} else if(n == 3 && atoi(f[1]) == 433) {
				reregister();
			}
		}
		reconnect();
	}
}

void
ircsrv(void)
{
	char buf[512];
	long n;

	while((n = readln(0, buf, sizeof(buf)-1)) > 0){
		joinhandler(buf);
		qlock(&lck);
		if(write(logfd, buf, n) != n)
			fprint(2, "write to irclog: %r\n");
		if(write(ircfd, buf, n) != n)
			fprint(2, "write to ircserver: %r\n");
		qunlock(&lck);
	}
	killall();
}

void
joinhandler(char *buf)
{
	char *toks[4];
	char *crap;

	if(strlen(buf) > 4){
		crap = strdup(buf);
		crap[strlen(crap)-2] = '\0';
		if(tokenize(crap, toks, 4) >= 2)
			if(strncmp(crap, "JOIN", 4) == 0)
				if(tchans < 256){
					channels[tchans] = strdup(toks[1]);
					tchans++;
				}
		free(crap);
	}
}
