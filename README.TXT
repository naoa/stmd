stmd is a simple wrapper for stemmers such as ChaSen and Snowball.
Text is normalized to NFKC before processed.
It can be accessed in two modes (four ways):

mode S (socket mode)
	In this mode, stmd accepts localsocket and inet-socket.
	When connection is established, stmd reads a stemmer name from
	socket. Next stmd enter the read-process-write loop:
	read from the socket, process text, then write back results
	to the socket. To run stmd in this mode,
		1. pass `-u localsocket' to make stmd listen localsocket
		2. pass `-p inet-port [-b bind-address]' to make stmd listen inet-port,
		   optionally pass a bind-address to make stmd listen a specified interface
	-u and -p may pass to a stmd simultaneously.
	As a variant of this mode, stmd may be called from inetd.
	In this case, stemmer is initialized every time when stmd is called.
	To call stmd from inetd,
		3. pass `in.stmd' to argv[0].

	Usually GETAssoc tries to connect to localsocket and inet-socket in
	order. Therefore, though only a socket is suffice, 
	it is better to run stmd listening a localsocket,
	and as a failsafe mechanism, make inetd serves for inet-socket. 
	(make sure stand-alone stmd NOT to listen inet-socket)	

	If stemmer is `normalizer', stmd write backs a normalized string.
	Otherwise, input string is split up into words. Every tow words
	are separated with a newline character.

mode Q (pipe mode)
	To run stmd in this mode, pass `-q' flag to the program.
	in this mode, `-l stemmer name' may be used to pass stemmer name.
	In this mode, stmd reads text from stdin, process text, 
	then write results to stdout.
	Usually this mode is used by itb2frq to enhance the throughput.

	Unless stemmer name is supplied by `-l' flag,
	stmd try to read stemmer name first.
