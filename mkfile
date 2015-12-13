</$objtype/mkfile

TARG=\
	irc\
	ircsrv\

BIN=/$objtype/bin

</sys/src/cmd/mkmany

ircman:V:
	cp irc.1 /sys/man/1/irc

install: ircman
