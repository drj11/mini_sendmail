/* mini_sendmail - accept email on behalf of real sendmail
**
** Copyright(r) 1999 by Jef Poskanzer <jef@mail.acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
** Copyright 2008-2012 by Marian Marinov <mm@yuhu.biz>
** The software have been havily modified to fit more complex needs.
** The above licence rules still remain the same.
**
*/


/* These defines control some optional features of the program.  If you
** don't want the features you can undef the symbols; some of them mean
** a significant savings in executable size.
*/

// #define DO_RECEIVED	/* whether to add a "Received:" header */
// #define DO_GETLOGIN /* whether to use getlogin before getpwuid */
#define DO_MINUS_SP	/* whether to implement the -s and -p flags */
#define DO_GETPWUID	/* whether to use getpwuid() */
#define DO_DNS		/* whether to do a name lookup on -s, or just IP# */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <time.h>
#ifdef DO_GETPWUID
#include <pwd.h>
#endif /* DO_GETPWUID */

#include "version.h"


/* Defines. */
#define SMTP_PORT 25
#define DEFAULT_TIMEOUT 60
// #define RECEPIENT_DEBUG

/* Globals. */
static char* argv0;
//static char* fake_from;
#ifdef RECEPIENT_DEBUG
static char* fake_to;
#endif
static int parse_message, verbose, debug;
#ifdef DO_MINUS_SP
static char* server;
static short port;
#endif /* DO_MINUS_SP */
static int timeout;
static int sockfd1, sockfd2;
static FILE* sockrfp;
static FILE* sockwfp;
static int got_a_recipient;
static int skip_quoted_text;


/* Forwards. */
//static void usage( int argc, char** argv, char** envp );
//static void print_env( char** envp );
//static void print_argv( int argc, char** argv );
static char* slurp_message( void );
#ifdef DO_RECEIVED
static char* make_received( char* from, char* username, char* hostname );
#endif /* DO_RECEIVED */
static void parse_for_recipients( char* message, char** envp );
static void add_recipient( char* recipient, int len, char** envp );
static int open_client_socket( void );
static int read_response( void );
static void send_command( char* command );
static void send_data( char* data );
static void send_done( void );
static void sigcatch( int sig );
static void show_error( char* cause );
static int has_from( char* message);
static int has_date( char* message);
static char *headers_end( char* message );
static char *find_to( char *message , int start );
static char *find_cc( char *message, int start );
static char *find_bcc( char *message, int start );


int main( int argc, char** argv , char** envp) {
#ifdef RECEPIENT_DEBUG
    int argn = 1;
#endif
	int status;
	int i, h, j, from_count = 0, idx = 0;
    char* message;
#ifdef DO_RECEIVED
    char* received;
#endif /* DO_RECEIVED */
    char* username;
	char **ep;
	char xuser[50] = "";
	char xopt[5000] = "";
    char hostname[500] = "";
    char from[1000] = "";
// #ifdef RECEPIENT_DEBUG
	char to[1000] = "";
// #endif
	char to_buf[1000] = "";
	char *to_ptr = to_buf;
    char buf[2000] = "";
	int has_from_header = 0;
	int has_date_header = 0;
	char date_header[50];
	char from_header[1010];

    /* Parse args. */
    argv0 = argv[0];
//     char *fake_from = from;
#ifdef RECEPIENT_DEBUG
	fake_to = (char*) 0;
#endif
    parse_message = 0;
#ifdef DO_MINUS_SP
    server = "127.0.0.1";
    port = SMTP_PORT;
#endif /* DO_MINUS_SP */
    verbose = 0;
    debug = 0;
    timeout = DEFAULT_TIMEOUT;
	skip_quoted_text = 0;

	char *safe_env_lst[] = {
		"USER",
		"SCRIPT_FILENAME",
		"REQUEST_URI",
		"PWD",
		"REMOTE_ADDR",
		"MESSAGE_ID"
	};

/*
-fname		DONE
-f name 	DONE
-f name user@domain
-f <name> user@domain
-f '' user@domain
-f "" user@domain
*/
	for (i=0; i < argc; i++) {
			if ( strncmp( argv[i], "-d", 2 ) == 0 )
				debug = 1;
			else if ( strncmp( argv[i], "-c", 2 ) == 0 )
				skip_quoted_text = 1;
			else if ( strncmp( argv[i], "-V", 2 ) == 0 ) {
				printf("Version: %s\n", VERSION);
				return(0);
			} else if ( strncmp( argv[i], "-i", 2 ) == 0 ||
				strncmp( argv[i], "-oi", 3 ) == 0 ||
				strncmp( argv[i], "-n", 2 ) == 0 ||
				strcmp( argv[i], "--") == 0 ) {
				if ( debug )
					printf("ARGV[%d](%s): Ignored!\n", i, argv[i]);	/* ignore */
			} else if ( strncmp( argv[i], "--help", 6 ) == 0 ) {
#ifdef DO_MINUS_SP
	#ifdef DO_DNS
					char* spflag = "[-s<server>] [-p<port>] ";
	#else /* DO_DNS */
					char* spflag = "[-s<server_ip>] [-p<port>] ";
	#endif /* DO_DNS */
#else /* DO_MINUS_SP */
					char* spflag = "";
#endif /* DO_MINUS_SP */
					printf("Usage: %s [-f name] [-v] [-d] [-t] [-T] %s\n", argv[0], spflag);
					printf("  -i, -oi, -n and -- are ignored\n");
					printf("  -d debug\n");
					printf("  -v verbose\n");
					printf("  -V version\n");
					printf("  -s server\n");
					printf("  -p port\n");
					printf("  -c skip quoted text\n");
					printf("  -T timeout (default: 60)\n");
					printf("  -t parse the whole message searching for To: Cc: Bcc:\n");
					printf("  -f can be used in any format if the last part of that is the email address\n");
					printf("Version: %s\n", VERSION);
					return(0);
			} else if ( strncmp( argv[i], "-t", 2 ) == 0 )
				parse_message = 1;
			else if ( strncmp( argv[i], "-T", 2 ) == 0 && argv[i][2] != '\0' )
				timeout = atoi( &(argv[i][2]) );
			else if ( strncmp( argv[i], "-v", 2 ) == 0 )
				verbose = 1;
			else if ( strncmp( argv[i], "-f", 2 ) == 0) {
				if ( argv[i][2] != '\0' ) {
					if ( strlen(argv[i]) < 998 ) {
						for ( h=2; h <= strlen(argv[i]); h++)
							from[h-2] = argv[i][h];
						if ( debug )
							printf("FROM0: %s\n", from);
					}
				} else if ( strncmp( argv[i+1], "-", 1) != 0 ) {
					for (h=i+1; h < argc; h++) {
						if ( argv[h] != NULL ) {
							// Ako imam oshte edin argument sled tozi i toi ne zapochva s -
							// to tozi ne e recipient
							if ( argv[h+1] == NULL ) {
								// Tui kato nqmam argumenti sled tekushtqt argument
								// priemame che tova e recipient :)
								// ako tozi argument naistina sushtestvuva
								if ( ( strlen(to) + strlen(argv[h]) ) < 1000 ) {
									for ( j=0; j <= strlen(argv[h]); j++)
										to[j] = argv[h][j];
									if ( debug )
										printf("TO0: %s (%s)\n", to, argv[h]);
								} else {
									(void) fprintf( stderr, "%s: TO argument too long\n", argv0 );
									return(1);
								}
							} else {
								if (strlen(from) + strlen(argv[h]) < 1000) {
									for ( j=0; j <= strlen(argv[h]); j++)
										from[j] = argv[h][j];
									if ( debug )
										printf("FROM1: %s\t(%s), from_count: %d\n", from, argv[h], from_count);
								} else {
									(void) fprintf( stderr, "%s: FROM argument too long\n", argv0 );
									return(1);
								}
								// Ako sledvashtiqt agument ne zapochva s -
								// i ne e posleden, to neka go dobavim kum from-a
 								if ( strncmp( argv[h+1], "-", 1) != 0 && argv[h+2] != NULL ) {
								// Ako cqlostnata duljina na tekushtiqt string + cmd
								// argument-a sa po-malki ot 1000 to dobavi tekushtiqt
								// cmd argument kum from
									if (strlen(from) + strlen(argv[h+1]) < 1000) {
										for ( j=0; j <= strlen(argv[h+1]); j++)
											from[j] = argv[h+1][j];
										if ( debug )
											printf("FROM2: %s\t(%s), from_count: %d\n", from, argv[h+1], from_count);
									} else {
										(void) fprintf( stderr, "%s: FROM argument too long\n", argv0 );
										return(1);
									}
								}
							}
						}
					}
				}
			}
#ifdef DO_MINUS_SP
			else if ( strncmp( argv[i], "-s", 2 ) == 0 && argv[i][2] != '\0' )
				server = &(argv[i][2]);
			else if ( strncmp( argv[i], "-p", 2 ) == 0 && argv[i][2] != '\0' )
				port = atoi( &(argv[i][2]) );
#endif /* DO_MINUS_SP */
				if ( ! got_a_recipient && i == argc-1 && parse_message != 1 ) {
					got_a_recipient++;
					strcat(to_buf, argv[i]);
					if ( debug )
						printf("This has to be the TO %s\n", argv[i]);
				}
				if ( debug )
					printf("ARGV[%d]: |%s| %lu\n", i, argv[i], (unsigned long)strlen(argv[i]));
	}

	if ( timeout == 0 )
		timeout = DEFAULT_TIMEOUT;

	if ( debug )
#ifdef DO_MINUS_SP
		printf("parse_message: %d\nserver: %s port: %d\ntimeout: %d verbose: %d\n",
			parse_message, server, port, timeout, verbose);
#else
		printf("parse_message: %d\ntimeout: %d verbose: %d\n",
			parse_message, timeout, verbose);
#endif

#ifdef DO_GETLOGIN
    username = getlogin();
    if ( username == (char*) 0 ) {
#endif
#ifdef DO_GETPWUID
		struct passwd* pw = getpwuid( getuid() );
		if ( pw == (struct passwd*) 0 ) {
			(void) fprintf( stderr, "%s: can't determine username\n", argv0 );
			exit( 1 );
		}
		username = pw->pw_name;
#endif /* DO_GETPWUID */
#ifdef DO_GETLOGIN
		(void) fprintf( stderr, "%s: can't determine username\n", argv0 );
		exit( 1 );
	}
#endif
    if ( gethostname( hostname, sizeof(hostname) - 1 ) < 0 )
		show_error( "gethostname" );
	if ( strlen(from) < 2 ) {
		if ( debug )
			printf("Ko tursish sq tuka?\n");
 		(void) snprintf( from, sizeof(from), "%s@%s", username, hostname );
	} else {
		// Ako nqmame @ znachi trqbva da fake-nem from-a
		if ( strchr( from, '@' ) == (char*) 0 ) {
			if ( debug )
				printf("Sho go zamestvash toq email sega??\n");
			h = strlen(from);
			from[h] = '@';
			h++;
			for (i=0; i<=strlen(hostname); i++)
				from[i+h] = hostname[i];
		}
	}
    /* Strip off any angle brackets in the from address. */
    while ( from[0] == '<' )
		(void) strcpy( from, &from[1] );
    while ( from[strlen(from)-1] == '>' )
		from[strlen(from)-1] = '\0';
    message = slurp_message();
#ifdef DO_RECEIVED
    received = make_received( from, username, hostname );
#endif /* DO_RECEIVED */
	has_from_header = has_from(message);
	has_date_header = has_date(message);

	if (! has_from_header) {
		snprintf(from_header, sizeof(from_header), "From: %s\n", from);
	}

	if (! has_date_header) {
		time_t t;
		struct tm *tmp;

		t = time(NULL);
		tmp = localtime(&t);

		strftime(date_header, sizeof(date_header), "Date: %a, %d %b %Y %T %z\n", tmp);
	}

    (void) signal( SIGALRM, sigcatch );

    (void) alarm( timeout );
    sockfd1 = open_client_socket();

    sockfd2 = dup( sockfd1 );
    sockrfp = fdopen( sockfd1, "r" );
    sockwfp = fdopen( sockfd2, "w" );

    /* The full SMTP protocol is spelled out in RFC821, available at
    ** http://www.faqs.org/rfcs/rfc821.html
    ** The only non-obvious wrinkles:
    **  - The commands are terminated with CRLF, not newline.
    **  - Newlines in the data file get turned into CRLFs.
    **  - Any data lines beginning with a period get an extra period prepended.
    */

    status = read_response();
    if ( status != 220 ) {
		(void) fprintf( stderr,  "%s: unexpected initial greeting %d\n", argv0, status );
		exit( 1 );
	}

    (void) snprintf( buf, sizeof(buf), "HELO %s", hostname );
    send_command( buf );
    status = read_response();
    if ( status != 250 ) {
		(void) fprintf( stderr,  "%s: unexpected response %d to HELO command\n", argv0, status );
		exit( 1 );
	}

    (void) snprintf( buf, sizeof(buf), "MAIL FROM: %s", from );
    send_command( buf );
    status = read_response();
    if ( status != 250 ) {
		(void) fprintf( stderr,  "%s: unexpected response %d to MAIL FROM command\n", argv0, status );
		exit( 1 );
	}

	if ( got_a_recipient ) {
		if ( debug )
			printf("Sending first found recipient: %s\n", to_buf);
		add_recipient( to_ptr, 0, envp );
	}
	// TO
	// Check the recipient for @ and only if we have it add the recipient
	if ( strchr( to, '@' ) ) {
		j = strlen( to );
		if ( debug )
			printf("TO: %s(%d)\n", to, j);
		h=0;
		for (i=0; i <= j; i++) {
			if ( to[i] == ' ' && to[i+1] != '\0' )
				h=i-1;
		}
		from_count=0;
		for (i=0; i <= j; i++) {
			if ( to[i+h] != '"' &&
				to[i+h] != '<' &&
				to[i+h] != '>' &&
				to[i+h] != ' ' ) {
 				to_buf[from_count] = to[i+h];
				from_count++;
			}
		}
		if ( debug )
			printf("TO2: %s\n", to_buf);
		add_recipient( to_buf, strlen( to_buf ), envp );
	}
#ifdef RECEPIENT_DEBUG
    for ( ; argn < argc; ++argn ) {

		if ( fake_to == (char*) 0 )
			(void) snprintf( to, sizeof(to), "%s@%s", argv[argn], hostname );
		else
			if ( strchr( fake_to, '@' ) == (char*) 0 )
				(void) snprintf( to, sizeof(to), "%s@%s", fake_to, hostname );
		else
			(void) snprintf( to, sizeof(to), "%s", fake_to );
		fprintf( stderr, "RCP: %s\n", fake_to );
		add_recipient( argc, argv, envp );

	}
#endif /* RECEPIENT_DEBUG */
    if ( parse_message ) {
		parse_for_recipients( message, envp );
		if ( debug )
			(void) fprintf( stderr, "%s\n", message );
	}
    if ( ! got_a_recipient ) {
		(void) fprintf( stderr,  "%s: no recipients found\n", argv0 );
		exit( 1 );
	}

    send_command( "DATA" );
    status = read_response();
    if ( status != 354 ) {
		(void) fprintf( stderr,  "%s: unexpected response %d to DATA command\n", argv0, status );
		exit( 1 );
	}

#ifdef DO_RECEIVED
    send_data( received );
#endif /* DO_RECEIVED */
	if (strlen(username) <= 20)
		sprintf( xuser, "X-SG-User: %s\n", username);

 	sprintf( xopt, "%s", "X-SG-Opt: ");
	for (ep = envp; *ep; ep++)
		for (idx = 0; idx<=5; idx++)
			if (safe_env_lst[idx] && !strncmp(*ep, safe_env_lst[idx], strlen(safe_env_lst[idx])))
				sprintf( xopt, "%s %s ", xopt, *ep);

	if (! has_from_header)
		send_data( from_header );
	if (! has_date_header)
		send_data( date_header );
 	send_data( xuser );
	send_data( xopt );
    send_data( message );
    send_done();
    status = read_response();
    if ( status != 250 ) {
		(void) fprintf( stderr,  "%s: unexpected response %d to DATA\n", argv0, status );
		exit( 1 );
	}

    send_command( "QUIT" );
    status = read_response();
    if ( status != 221 )
		(void) fprintf( stderr,  "%s: unexpected response %d to QUIT command - ignored\n", argv0, status );

    (void) close( sockfd1 );
    (void) close( sockfd2 );

    exit( 0 );
}

/*
static void print_env(char** envp) {
	int a = 0;
    fprintf( stderr, "The environment is as follows:\n");
    while (envp[a] != NULL) {
		fprintf( stderr, "   %s\n", envp[a++]);
	}
}

static void print_argv(int argc, char** argv) {
	int a;
	printf("There are %d arguments:\n", argc-1);
	for (a=1; a<argc; a++)
		fprintf( stderr, "   Argument %2d: %s\n", a, argv[a]);
}
static void usage( int argc, char** argv, char** envp ) {

#ifdef DO_MINUS_SP
	#ifdef DO_DNS
    char* spflag = "[-s<server>] [-p<port>] ";
	#else // DO_DNS
    char* spflag = "[-s<server_ip>] [-p<port>] ";
	#endif // DO_DNS 
#else // DO_MINUS_SP 
    char* spflag = "";
#endif // DO_MINUS_SP 
    (void) fprintf( stderr, "usage:  %s [-f<name>] [-t] %s[-T<timeout>] [-v] [address ...]\n", argv0, spflag );
	if ( debug ) {
		print_argv ( argc, argv );
		print_env( envp );
	}
    exit( 1 );
}
*/

static char* slurp_message( void ) {
    char* message;
    int message_size, message_len;
    int c;

    message_size = 5000;
    message = (char*) malloc( message_size );
    if ( message == (char*) 0 )	{
		(void) fprintf( stderr, "%s: out of memory\n", argv0 );
		exit( 1 );
	}
    message_len = 0;

    for (;;) {
		c = getchar();
		if ( c == EOF )
	    	break;
		if ( message_len + 1 >= message_size ) {
	    	message_size *= 2;
	    	message = (char*) realloc( (void*) message, message_size );
	    	if ( message == (char*) 0 ) {
				(void) fprintf( stderr, "%s: out of memory\n", argv0 );
				exit( 1 );
			}
	    }
		message[message_len++] = c;
	}
    message[message_len] = '\0';

    return message;
}

#ifdef DO_RECEIVED
static char* make_received( char* from, char* username, char* hostname ) {
    int received_size;
    char* received;
    time_t t;
    struct tm* tmP;
    char timestamp[100];

    t = time( (time_t*) 0 );
    tmP = localtime( &t );
    (void) strftime( timestamp, sizeof(timestamp), "%a, %d %b %Y %T %Z", tmP );
    received_size =
	500 + strlen( from ) + strlen( hostname ) * 2 + strlen( VERSION ) +
	strlen( timestamp ) + strlen( username );
    received = (char*) malloc( received_size );
    if ( received == (char*) 0 ) {
		(void) fprintf( stderr, "%s: out of memory\n", argv0 );
		exit( 1 );
	}
    (void) snprintf( received, received_size, "Received: (from %s)\n\tby %s (%s);\n\t%s\n\t(sender %s@%s)\n",
		from, hostname, VERSION, timestamp, username, hostname );
    return received;
}
#endif /* DO_RECEIVED */

static void parse_for_recipients( char* message, char** envp ) {
	int i = 0;
	char *pos;

	while ((pos = find_to(message, i))) {
		add_recipient(pos, 3, envp);
		i = pos - message + 3;
	}

	i = 0;
	while ((pos = find_cc(message, i))) {
		add_recipient(pos, 3, envp);
		i = pos - message + 3;
	}

	i = 0;
	while ((pos = find_bcc(message, i))) {
		add_recipient(pos, 4, envp);
		i = pos - message + 4;
	}
}

// Finds the first To:, to: or TO: in the headers and returns a char * to it.
// Starts searching from start.
// Returns NULL if no to header is found.
static char *find_to( char *message, int start ) {
	static char * to_strings[4] = { "To:", "TO:", "to:", NULL };

	char *end_of_headers = headers_end(message);
	char *pos = end_of_headers;
	char *start_pos = message + start;
	char *tmp;

	int i;

	if (start_pos >= end_of_headers)
		return NULL;

	for (i = 0; to_strings[i]; i++)
		if ((tmp = strstr(start_pos, to_strings[i])) &&
				(tmp == start_pos || *(tmp - 1) == '\n') &&
				tmp < pos) {
			pos = tmp;
		}

	if (pos < end_of_headers)
		return pos;

	return NULL;
}

// Finds the first Cc:, CC: or cc: in the headers and returns a char * to it.
// Starts searching from start.
// Returns NULL if no to header is found.
static char *find_cc( char *message, int start ) {
	static char * cc_strings[4] = { "Cc:", "CC:", "cc:", NULL };

	char *end_of_headers = headers_end(message);
	char *pos = end_of_headers;
	char *start_pos = message + start;
	char *tmp;

	int i;

	if (start_pos >= end_of_headers)
		return NULL;

	for (i = 0; cc_strings[i]; i++)
		if ((tmp = strstr(start_pos, cc_strings[i])) &&
				(tmp == start_pos || *(tmp - 1) == '\n') &&
				tmp < pos) {
			pos = tmp;
		}

	if (pos < end_of_headers)
		return pos;

	return NULL;
}

// Finds the first BCC:, bcc: or Bcc: in the headers and returns a char * to it.
// Starts searching from start.
// Returns NULL if no to header is found.
static char *find_bcc( char *message, int start ) {
	static char * bcc_strings[4] = { "BCC:", "bcc:", "Bcc:", NULL };

	char *end_of_headers = headers_end(message);
	char *pos = end_of_headers;
	char *start_pos = message + start;
	char *tmp;

	int i;

	if (start_pos >= end_of_headers)
		return NULL;

	for (i = 0; bcc_strings[i]; i++)
		if ((tmp = strstr(start_pos, bcc_strings[i])) &&
				(tmp == start_pos || *(tmp - 1) == '\n') &&
				tmp < pos) {
			pos = tmp;
		}

	if (pos < end_of_headers)
		return pos;

	return NULL;
}

static void add_recipient( char* message, int chars_to_remove, char** envp ) {
	char buffer[1000];
	char buf[1000];
	char *space_check = NULL;
	char *to_buf = buffer;
	int len = strlen(message) - chars_to_remove;
	int sec_check = 0;
	int status;
	int found_char = 0;
	int skip_to_comma = 0;
	int dont_skip_quotes = 1;

	// Clear the buffer
 	memset(buffer, 0x00, sizeof(buffer));
	memset(buf, 0x00, sizeof(buf));
	message += chars_to_remove;

	// Walking trough the message, char by char
	while (len >= 0) {
/*
  We have to match lines like these:
	To: "mm@siteground.com" <mm@siteground.com>
	To: "mm@siteground.com" <mm@siteground.com>, m.m@siteground.com
*/
		if ( *message == ',' || *message == '\n' || *message == '\r' || *message == '\0' ) {
			// clear the skip flag
			if ( *message == ',' )
				skip_to_comma = 0;
			// do not print if the buffer is empty or containing :(To:, Bcc:, etc.)
			if ( strlen(buffer) > 0 && strchr(buffer, ':') == NULL) {
				(void) snprintf( buf, sizeof(buf), "RCPT TO: %s", buffer );
				send_command( buf );
				status = read_response();
				if ( status != 250  && status != 251 )
					(void) fprintf( stderr,  "%s: unexpected response %d to RCPT TO command\n", argv0, status );
				memset(buffer, 0x00, sizeof(buffer));
				memset(buf, 0x00, sizeof(buf));
				sec_check=0;
			}
			// clear the buffer
			to_buf = buffer;
			// If this is the end of the line we stop searching for recipients
			if ( *message == '\n' ) break;
		} else {
			// possible hacking attempt
			if ( sec_check > 999 )
				exit( 12 );
			// skip every character until we find comma
			if ( skip_to_comma ) {
				// goto next character in the message
				message++;
				len--;
				continue;
			}
			if ( found_char == 0 && *message == '@' )
				found_char = 1;
			if ( found_char && *message == ' ' ) {
				space_check = message;
				space_check++;
				if (*space_check != ',' && *space_check != ' ' )
					found_char = 1;
				else {
					skip_to_comma = 1;
					found_char = 0;
				}
				// goto next character in the message
				message++;
				len--;
				continue;
			}
			// copy the current character into the TO buffer
			if ( *message == '"' || *message == '\'') {
				if (skip_quoted_text) {
					if (dont_skip_quotes)
						dont_skip_quotes=0;
					else
						dont_skip_quotes=1;
				}
			} else {
				if (dont_skip_quotes) {
					*to_buf = *message;
					// goto next character in the TO buffer
					to_buf++;
					sec_check++;
				}
			}
		}
//	"mm@siteground.com" <mm@siteground.com>, m.m@siteground.com, "mm@siteground.com" <mm@siteground.com>, m.m@siteground.com
		// goto next character in the message
		message++;
		len--;
	}
	got_a_recipient++;
}


#if defined(AF_INET6) && defined(IN6_IS_ADDR_V4MAPPED)
#define USE_IPV6
#endif

static int open_client_socket( void ) {
#ifdef USE_IPV6
    struct sockaddr_in6 sa;
#else /* USE_IPV6 */
    struct sockaddr_in sa;
#endif /* USE_IPV6 */
    int sa_len, sock_family, sock_type, sock_protocol;
    int sockfd;
    sock_type = SOCK_STREAM;
    sock_protocol = 0;
    sa_len = sizeof(sa);
    (void) memset( (void*) &sa, 0, sa_len );

#ifdef USE_IPV6

    {
#	ifdef DO_MINUS_SP
    struct sockaddr_in sa4;
    struct addrinfo hints;
    char portstr[10];
    int gaierr;
    struct addrinfo* ai;
    struct addrinfo* ai2;
    struct addrinfo* aiv4;
    struct addrinfo* aiv6;
#	endif /* DO_MINUS_SP */

    sock_family = PF_INET6;

#	ifdef DO_MINUS_SP
    (void) memset( (void*) &sa4, 0, sizeof(sa4) );
    if ( inet_pton( AF_INET, server, (void*) &sa4.sin_addr ) == 1 )	{
		sock_family = PF_INET;
		sa4.sin_port = htons( port );
		sa_len = sizeof(sa4);
		(void) memmove( &sa, &sa4, sa_len );
	} else if ( inet_pton( AF_INET6, server, (void*) &sa.sin6_addr ) != 1 ) {
#		ifdef DO_DNS
	(void) memset( &hints, 0, sizeof(hints) );
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	(void) snprintf( portstr, sizeof(portstr), "%d", port );
	if ( (gaierr = getaddrinfo( server, portstr, &hints, &ai )) != 0 ) {
	    (void) fprintf( stderr, "%s: getaddrinfo %s - %s\n", argv0, server, gai_strerror( gaierr ) );
	    exit( 1 );
	}

	/* Find the first IPv4 and IPv6 entries. */
	aiv4 = (struct addrinfo*) 0;
	aiv6 = (struct addrinfo*) 0;
	for ( ai2 = ai; ai2 != (struct addrinfo*) 0; ai2 = ai2->ai_next ) {
	    switch ( ai2->ai_family ) {
			case PF_INET:
				if ( aiv4 == (struct addrinfo*) 0 )
			    	aiv4 = ai2;
			break;
			case PF_INET6:
				if ( aiv6 == (struct addrinfo*) 0 )
		    		aiv6 = ai2;
			break;
		}
	}

	/* If there's an IPv4 address, use that, otherwise try IPv6. */
	if ( aiv4 != (struct addrinfo*) 0 ) {
	    if ( sizeof(sa) < aiv4->ai_addrlen ) {
			(void) fprintf( stderr, "%s - sockaddr too small (%lu < %lu)\n", server, (unsigned long) sizeof(sa), (unsigned long) aiv4->ai_addrlen );
			exit( 1 );
		}
	    sock_family = aiv4->ai_family;
	    sock_type = aiv4->ai_socktype;
	    sock_protocol = aiv4->ai_protocol;
	    sa_len = aiv4->ai_addrlen;
	    (void) memmove( &sa, aiv4->ai_addr, sa_len );
	    goto ok;
	}
	if ( aiv6 != (struct addrinfo*) 0 ) {
	    if ( sizeof(sa) < aiv6->ai_addrlen ) {
			(void) fprintf( stderr, "%s - sockaddr too small (%lu < %lu)\n", server, (unsigned long) sizeof(sa), (unsigned long) aiv6->ai_addrlen );
			exit( 1 );
		}
	    sock_family = aiv6->ai_family;
	    sock_type = aiv6->ai_socktype;
	    sock_protocol = aiv6->ai_protocol;
	    sa_len = aiv6->ai_addrlen;
	    (void) memmove( &sa, aiv6->ai_addr, sa_len );
	    goto ok;
	}

	(void) fprintf(
	    stderr, "%s: no valid address found for host %s\n", argv0, server );
	exit( 1 );

	ok:
		freeaddrinfo( ai );
#		else /* DO_DNS */
        (void) fprintf( stderr, "%s: bad server IP address %s\n", argv0, server );
		exit( 1 );
#		endif /* DO_DNS */
	}
#	else /* DO_MINUS_SP */
    sa.sin6_addr = in6addr_any;
    sa.sin6_port = htons( SMTP_PORT );
#	endif /* DO_MINUS_SP */

    sa.sin6_family = sock_family;

    }

#else /* USE_IPV6 */

    {
#	ifdef DO_MINUS_SP
    struct hostent *he;
#	else /* DO_MINUS_SP */
    char local_addr[4] = { 127, 0, 0, 1 };
#	endif /* DO_MINUS_SP */

    sock_family = PF_INET;

#	ifdef DO_MINUS_SP
    sa.sin_addr.s_addr = inet_addr( server );
    sa.sin_port = htons( port );
    if ( (int32_t) sa.sin_addr.s_addr == -1 ) {
#		ifdef DO_DNS
		he = gethostbyname( server );
		if ( he == (struct hostent*) 0 ) {
		    (void) fprintf( stderr, "%s: server name lookup of '%s' failed - %s\n", argv0, server, hstrerror( h_errno ) );
	    	exit( 1 );
		}
		sock_family = he->h_addrtype;
		(void) memmove( &sa.sin_addr, he->h_addr, he->h_length );
#		else /* DO_DNS */
		(void) fprintf( stderr, "%s: bad server IP address %s\n", argv0, server );
		exit( 1 );
#		endif /* DO_DNS */
	}
#	else /* DO_MINUS_SP */
   		(void) memmove( &sa.sin_addr, local_addr, sizeof(local_addr) );
	   	sa.sin_port = htons( SMTP_PORT );
#	endif /* DO_MINUS_SP */
    	sa.sin_family = sock_family;
    }
#endif /* USE_IPV6 */

    sockfd = socket( sock_family, sock_type, sock_protocol );
    if ( sockfd < 0 )
		show_error( "socket" );

    if ( connect( sockfd, (struct sockaddr*) &sa, sa_len ) < 0 )
		show_error( "connect" );

    return sockfd;
}

static int read_response( void ) {
    char buf[10000];
    char* cp;
    int status;
    for (;;) {
		(void) alarm( timeout );
		if ( fgets( buf, sizeof(buf), sockrfp ) == (char*) 0 ) {
			(void) fprintf( stderr, "%s: unexpected EOF\n", argv0 );
			exit( 1 );
		}
		if ( verbose )
			(void) fprintf( stderr, ">>>> %s", buf );
		for ( status = 0, cp = buf; *cp >= '0' && *cp <= '9'; ++cp )
			status = 10 * status + ( *cp - '0' );
		if ( *cp == ' ' )
			break;
		if ( *cp != '-' ) {
			(void) fprintf(
			stderr,  "%s: bogus reply syntax - '%s'\n", argv0, buf );
			exit( 1 );
		}
	}
    return status;
}

static void send_command( char* command ) {
    (void) alarm( timeout );
    if ( verbose )
		(void) fprintf( stderr, "<<<< %s\n", command );
	(void) fprintf( sockwfp, "%s\r\n", command );
	(void) fflush( sockwfp );
}

static void send_data( char* data ) {
    int bol;
	char *start = data;

    for ( bol = 1; *data != '\0'; ++data ) {
		if ( bol && *data == '.' )
			putc( '.', sockwfp );
		bol = 0;
		if ( *data == '\n' ) {
			if (data != start && *(data - 1) != '\r')
				putc( '\r', sockwfp );
			bol = 1;
		}
		putc( *data, sockwfp );
	}
    if ( ! bol )
		(void) fputs( "\r\n", sockwfp );
}

static void send_done( void ) {
    (void) fputs( ".\r\n", sockwfp );
    (void) fflush( sockwfp );
}

static void sigcatch( int sig ) {
    (void) fprintf( stderr, "%s: timed out\n", argv0 );
    exit( 1 );
}

static void show_error( char* cause ) {
    char buf[5000];

    (void) snprintf( buf, sizeof(buf), "%s: %s", argv0, cause );
    perror( buf );
    exit( 1 );
}

/* Returns a pointer to the first occurance of an empty line in the message.
 * This should be exactly the place where headers end.
 *
 * Caches the results for faster answers to consecutive requests for the
 * same message.
 */
static char *headers_end( char* message ) {
	static char *end_markers[4] = { "\n\n", "\n\r\n", NULL };
	static char *cache = (char *) -1;
	static char *cached_message = NULL;

	char *headers_end;
	char *message_end;
	char *tmp;
	int i;

	if (cache != (char *)-1 && cached_message == message)
		return cache;

	message_end = headers_end = message + strlen(message);
	for (i = 0; end_markers[i]; i++) {
		tmp = strstr(message, end_markers[i]);
		if (tmp && tmp < headers_end)
			headers_end = tmp;
	}


	if (headers_end == message_end)
		headers_end = message;

	cache = headers_end;
	cached_message = message;

	return headers_end;
}

static int has_from( char * message) {
	char *pos;
	if ((pos = strstr(message, "From:")) &&
			(pos == message || *(pos-1) == '\n' || *(pos-1)=='\r' ) &&
			pos < headers_end(message)) {
		return 1;
	}
	return 0;
}

static int has_date( char * message) {
	char *pos;
	if ((pos = strstr(message, "Date:")) &&
			(pos == message || *(pos-1) == '\n' || *(pos-1)=='\r' ) &&
			pos < headers_end(message)) {
		return 1;
	}
	return 0;
}
