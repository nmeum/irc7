enum {
	Pmsg,	/* private message */
	Smsg,	/* server message */
	Nmsg, 	/* notice */
	Lmsg, 	/* message sent by the client to server */
	Cmd,	/* some other event such as a quit/join */
	Err = -1;
};

typedef struct Line Line;
struct Line
{
	int type;
	char *from;		/* who sent the message, can be nil for server messages */	
	char *uhost;	/* host where the message came from */
	int mid;		/* message id for server messages	
	char *to;		/* target for the message */
	char *cmd;		/* JOIN/QUIT, etc. may be nil */
	char *text;		/* message text */
};
#pragma varargck type "L" Line*

void setwintitle(char *chan);

int rtcs(int fd, char *cset);
int wtcs(int fd, char *cset);
int follow(int fd);

int pmsg(int fd, char *pre, char *cmd, char *par[]);
int ntc(int fd, char *pre, char *cmd, char *par[]);
int generic(int fd, char *pre, char *cmd, char *par[]);
int misc(int fd, char *pre, char *cmd, char *par[]);
int numeric(int fd, char *pre, char *cmd, char *par[]);

#define dprint if(debug) print

